/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

#include "linux_pmem.h"
#include "elf.h"
#include <regex>

namespace aff4 {

// Return the physical offset of all the system ram mappings.
AFF4Status LinuxPmemImager::ParseIOMap_(std::vector<ram_range> *ram) {
    resolver.logger->info("Will parse /proc/iomem");
    ram->clear();

    AFF4Flusher<FileBackedObject> iomap;
    RETURN_IF_ERROR(NewFileBackedObject(&resolver, "/proc/iomem", "read", iomap));

    std::string data = iomap->Read(0x10000);
    std::regex RAM_regex("(([0-9a-f]+)-([0-9a-f]+) : System RAM)");
    auto begin = std::sregex_iterator(data.begin(), data.end(), RAM_regex);
    auto end = std::sregex_iterator();
    for (std::sregex_iterator i = begin; i != end; ++i) {
        aff4_off_t start = strtoll((*i)[1].str().c_str(), nullptr, 16);
        aff4_off_t end = strtoll((*i)[2].str().c_str(), nullptr, 16);
        resolver.logger->info("System RAM {:x} - {:x}", start, end);

        ram->push_back({start, end-start});
    }

    if (ram->size() == 0) {
        resolver.logger->critical("/proc/iomap has no System RAM.");
        return IO_ERROR;
    }

    return STATUS_OK;
}


AFF4Status LinuxPmemImager::CreateMap_(AFF4Map *map, aff4_off_t *length) {
  resolver.logger->info("Processing /proc/kcore");

  // The start address of each physical memory range.
  std::vector<ram_range> physical_range_start;
  RETURN_IF_ERROR(ParseIOMap_(&physical_range_start));

  *length = 0;
  AFF4Flusher<FileBackedObject> kcore;
  RETURN_IF_ERROR(NewFileBackedObject(
                      &resolver, "/proc/kcore", "read", kcore));

  Elf64_Ehdr header;
  if (kcore->ReadIntoBuffer(
          reinterpret_cast<char *>(&header),
          sizeof(header)) != sizeof(header)) {
      resolver.logger->critical("Unable to read /proc/kcore - Are you root?");
      return IO_ERROR;
  }

  // Check the header for sanity.
  if (header.ident[0] != ELFMAG0 ||
      header.ident[1] != ELFMAG1 ||
      header.ident[2] != ELFMAG2 ||
      header.ident[3] != ELFMAG3 ||
      header.ident[4] != ELFCLASS64 ||
      header.ident[5] != ELFDATA2LSB ||
      header.ident[6] != EV_CURRENT ||
      header.type     != ET_CORE ||
      header.machine  != EM_X86_64 ||
      header.version  != EV_CURRENT ||
      header.phentsize != sizeof(Elf64_Phdr)) {
      resolver.logger->error("Unable to parse /proc/kcore - Are you root?");
      return INVALID_INPUT;
  }

  // Read the physical headers.
  kcore->Seek(header.phoff, SEEK_SET);

  // The index in physical_range_start vector we are currently seeking.
  int physical_range_start_index = 0;

  std::vector<Elf64_Phdr> segments;

  for (int i = 0; i < header.phnum; i++) {
    Elf64_Phdr pheader;
    if (kcore->ReadIntoBuffer(
            reinterpret_cast<char *>(&pheader),
            sizeof(pheader)) != sizeof(pheader)) {
      return IO_ERROR;
    }

    if (pheader.type != PT_LOAD)
      continue;

    if (pheader.memsz != pheader.filesz)
      continue;

    segments.push_back(pheader);
  }

  if (segments.size() == 0) {
      resolver.logger->info("No ranges found in /proc/kcore");
      return NOT_FOUND;
  }

  // Physical Memory ranges seem to be first so sorting by virtual
  // address brings them to the front.
  std::sort(segments.begin(), segments.end(),
            [](const Elf64_Phdr x, const Elf64_Phdr y) {
                return x.vaddr < y.vaddr;
            });

  Elf64_Phdr first_run = segments[0];
  struct ram_range first_system_ram = physical_range_start[0];
  aff4_off_t range_start = first_run.vaddr - first_system_ram.start;

  for (const auto& pheader: segments) {
    // The kernel maps all physical memory regions inside its own
    // virtual address space. This virtual address space, in turn is
    // exported via /proc/kcore.

    // Each header has three pieces of relevant information:

    // File offset - The offset inside /proc/kcore where this region starts.

    // Virtual Address - The virtual address inside kernel memory
    // where the memory is mapped.

    // Physical Address - The physical address where the Virtual
    // address region is mapped by the kernel. (NOTE: On old kernels
    // this is always 0).

    // Therefore we search the exported ELF regions for the one which
    // is mapping the next required physical range. We then create an
    // AFF4 mapping between the physical memory region to the
    // /proc/kcore file address to enable reading the image.
    if (pheader.paddr != 0 && pheader.paddr != static_cast<Elf64_Addr>(
            physical_range_start[physical_range_start_index].start)) {
        resolver.logger->info("Skipped range {:x} - {:x} @ {:x}",
                              pheader.vaddr, pheader.memsz, pheader.off);
        continue;
    }
    resolver.logger->info("Found range {:x}/{:x} @ {:x}/{:x}",
                          pheader.paddr, pheader.memsz, pheader.vaddr,
                          pheader.off);
    map->AddRange(pheader.paddr,
                  pheader.off,
                  pheader.memsz,
                  kcore.get());

    physical_range_start_index++;
    if (physical_range_start_index >= physical_range_start.size())
        break;
  }

  if (map->Size() == 0) {
      resolver.logger->info("No ranges found in /proc/kcore");
      return NOT_FOUND;
  }

  // The kcore needs to outlive this method.
  map->GiveTarget(std::move(AFF4Flusher<AFF4Stream>(kcore.release())));

  return STATUS_OK;
}


AFF4Status LinuxPmemImager::ImagePhysicalMemory() {
  resolver.logger->info("Imaging memory");
  std::string format = GetArg<TCLAP::ValueArg<std::string>>("format")->getValue();
  std::string output_path = GetArg<TCLAP::ValueArg<std::string>>("output")->getValue();

  AFF4Volume *output_volume;
  RETURN_IF_ERROR(GetCurrentVolume(&output_volume));

  // We image memory into this map stream.
  URN map_urn = output_volume->urn.Append("proc/kcore");

  // This is a physical memory image.
  resolver.Set(map_urn, AFF4_CATEGORY, new URN(AFF4_MEMORY_PHYSICAL));

  if (format == "map") {
      RETURN_IF_ERROR(WriteMapObject_(map_urn));
  } else if (format == "raw") {
      RETURN_IF_ERROR(WriteRawFormat_(map_urn));
  } else if (format == "elf") {
      RETURN_IF_ERROR(WriteElfFormat_(map_urn));
  }

  resolver.Set(map_urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE),
               /* replace = */ false);

  actions_run.insert("memory");

  // Also capture these files by default.
  if (inputs.size() == 0) {
      resolver.logger->info("Adding default file collections.");
      inputs.push_back("/boot/*");

      // These files are essential for proper analysis when KASLR is enabled.
      inputs.push_back("/proc/iomem");
      inputs.push_back("/proc/kallsyms");
  }

  return process_input();
}

} // namespace aff4
