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

#include "pmem.h"
#include "elf.h"

#ifdef _WIN32
# include "win_pmem.h"
#define IMAGER_CLASS WinPmemImager

#elif defined(__linux__)
# include "linux_pmem.h"
#define IMAGER_CLASS LinuxPmemImager

#elif defined(__APPLE__) && defined(__MACH__)
# include "osxpmem.h"
#define IMAGER_CLASS OSXPmemImager
#endif

#include <signal.h>

namespace aff4 {

AFF4Status PmemImager::Initialize() {
  AFF4Status res = BasicImager::Initialize();
  if (res != STATUS_OK)
    return res;

  return STATUS_OK;
}


AFF4Status PmemImager::ParseArgs() {
  // Process the baseclass first.
  AFF4Status result = BasicImager::ParseArgs();

  if (result == CONTINUE) {
      // Add the memory namespace to the resolver.
      resolver.namespaces.push_back(
          std::pair<std::string, std::string>(
              "memory", AFF4_MEMORY_NAMESPACE));
  }

  // Check the requested format.
  std::string format = GetArg<TCLAP::ValueArg<std::string>>("format")->getValue();
  if (result == CONTINUE && format != "map" && format != "elf" &&
      format != "raw") {
      resolver.logger->error("Format {} not supported.", format);
      return INVALID_INPUT;
  }

  volume_type = GetArg<TCLAP::ValueArg<std::string>>(
      "volume_format")->getValue();
  if (result == CONTINUE && volume_type != "aff4" && volume_type != "raw") {
      resolver.logger->error("Volume format {} not supported.", volume_type);
      return INVALID_INPUT;
  }

  return result;
}


AFF4Status PmemImager::ProcessArgs() {
  // Add the memory namespace to the resolver.
  resolver.namespaces.push_back(
      std::pair<std::string, std::string>("memory", AFF4_MEMORY_NAMESPACE));

  AFF4Status result = BasicImager::ProcessArgs();
  if (result != CONTINUE) {
    return result;
  }

  // We automatically image memory if no other actions were given (unless the -m
  // flag was explicitly given).
  if ((actions_run.size() > 0) && !Get("acquire-memory")->isSet())
    return STATUS_OK;

  // The user forgot to specify an output file.
  if (!Get("output")->isSet()) {
      resolver.logger->error("You need to specify an output file with --output.");
    return INVALID_INPUT;
  }

  // Image physical memory.
  return ImagePhysicalMemory();
}

AFF4Status PmemImager::handle_pagefiles() {
    pagefiles = GetArg<TCLAP::MultiArgToNextFlag>(
        "pagefile")->getValue();

  return CONTINUE;
}

AFF4Status PmemImager::handle_compression() {
    std::string format = GetArg<TCLAP::ValueArg<std::string>>(
        "format")->getValue();

    std::string compression_setting = GetArg<TCLAP::ValueArg<std::string>>(
        "compression")->getValue();

  if (format == "elf" || format == "raw") {
    resolver.logger->info(
        "Output format is {} - compression disabled.", format );
    compression = AFF4_IMAGE_COMPRESSION_ENUM_STORED;

    return CONTINUE;
  }

  return BasicImager::handle_compression();
}


// Write an ELF stream.
AFF4Status PmemImager::WriteElfFormat_(const URN &output_urn) {
    resolver.logger->info("Will write in ELF format.");

    AFF4Flusher<AFF4Stream> header(new StringIO(&resolver));

    // Create a temporary map for WriteStream() API.
    AFF4Map memory_layout(&resolver);
    aff4_off_t total_length = 0;

    RETURN_IF_ERROR(CreateMap_(&memory_layout, &total_length));

    std::vector<Range> ranges = memory_layout.GetRanges();

    resolver.logger->info("There are {} ranges", ranges.size());

    // Now create a transformed map based on the ranges calculated.
    AFF4Map temp_map(&resolver);

    // Write the ELF header.
    Elf64_Ehdr elf_header;
    elf_header.ident[0] = ELFMAG0;
    elf_header.ident[1] = ELFMAG1;
    elf_header.ident[2] = ELFMAG2;
    elf_header.ident[3] = ELFMAG3;
    elf_header.ident[4] = ELFCLASS64;
    elf_header.ident[5] = ELFDATA2LSB;
    elf_header.ident[6] = EV_CURRENT;

    elf_header.type = ET_CORE;
    elf_header.machine = EM_X86_64;
    elf_header.version = EV_CURRENT;

    elf_header.phoff    = sizeof(Elf64_Ehdr);
    elf_header.phentsize = sizeof(Elf64_Phdr);
    elf_header.ehsize = sizeof(Elf64_Ehdr);
    elf_header.phentsize = sizeof(Elf64_Phdr);

    elf_header.phnum = ranges.size();
    elf_header.shentsize = sizeof(Elf64_Shdr);
    elf_header.shnum = 0;

    header->Write(reinterpret_cast<char *>(&elf_header), sizeof(elf_header));

    // Where we start writing data: End of ELF header plus one physical header per
    // range.
    uint64 file_offset = (sizeof(Elf64_Ehdr) +
                          ranges.size() * sizeof(Elf64_Phdr));

    // Map the header into the output stream.
    temp_map.AddRange(0, 0, file_offset, header.get());

    for (auto range : ranges) {
        Elf64_Phdr pheader = {};

        pheader.type = PT_LOAD;
        pheader.paddr = range.map_offset;
        pheader.memsz = range.length;
        pheader.align = 1;
        pheader.flags = PF_R;
        pheader.off = file_offset;
        pheader.filesz = range.length;

        // Write the program header into the ELF header stream.
        if (header->Write(reinterpret_cast<char *>(&pheader),
                          sizeof(pheader)) < 0) {
            return IO_ERROR;
        }

        // Map this range.
        temp_map.AddRange(file_offset, range.map_offset, range.length,
                          memory_layout.targets[range.target_id]);

        // Move the file offset by the size of this run.
        file_offset += range.length;
    }

    // Create the output object.
    AFF4Flusher<AFF4Stream> target_stream;
    RETURN_IF_ERROR(GetWritableStream_(output_urn, target_stream));

    VolumeManager progress(&resolver, this);
    progress.ManageStream(target_stream.get());

    progress.length = file_offset;

    // Now write the map into the image.
    return target_stream->WriteStream(&temp_map, &progress);
}

AFF4Status PmemImager::WriteRawFormat_(const URN &target_urn) {
    resolver.logger->info("Will write in raw format.");

    // Create a temporary map for WriteStream() API.
    AFF4Map temp_stream(&resolver);
    aff4_off_t total_length = 0;

    RETURN_IF_ERROR(CreateMap_(&temp_stream, &total_length));

    // Create the map object.
    AFF4Flusher<AFF4Stream> target_stream;
    RETURN_IF_ERROR(GetWritableStream_(target_urn, target_stream));

    VolumeManager progress(&resolver, this);
    progress.ManageStream(target_stream.get());

    progress.length = total_length;

    // Now write the map into the image.
    return target_stream->WriteStream(&temp_stream, &progress);
}

AFF4Status PmemImager::WriteMapObject_(const URN &map_urn) {
    resolver.logger->info("Will write in AFF4 map format.");

    // Create a temporary map for WriteStream() API.
    AFF4Map temp_stream(&resolver);
    aff4_off_t total_length = 0;

    RETURN_IF_ERROR(CreateMap_(&temp_stream, &total_length));

    // Set the user's preferred compression method.
    resolver.Set(map_urn.Append("data"), AFF4_IMAGE_COMPRESSION, new URN(
                     CompressionMethodToURN(compression)));

    // Create the map object.
    AFF4Volume *volume;
    RETURN_IF_ERROR(GetCurrentVolume(&volume));

    AFF4Flusher<AFF4Image> data_stream;
    RETURN_IF_ERROR(AFF4Image::NewAFF4Image(
                        &resolver, map_urn.Append("data"),
                        volume, data_stream));

    AFF4Flusher<AFF4Map> map_stream;
    RETURN_IF_ERROR(
        AFF4Map::NewAFF4Map(
            &resolver, map_urn, volume, data_stream.get(),
            map_stream));

    VolumeManager progress(&resolver, this);
    progress.ManageStream(map_stream.get());

    progress.length = total_length;

    // Now write the map into the image.
    return map_stream->WriteStream(&temp_stream, &progress);
}

AFF4Status PmemImager::GetWritableStream_(
    const URN &output_urn,
    AFF4Flusher<AFF4Stream> &result) {

    // Raw containers should just be flat files.
    if (volume_type == "raw") {
        std::string output_path = GetArg<TCLAP::ValueArg<std::string>>(
            "output")->getValue();

        if (output_path == "-") {
            RETURN_IF_ERROR(AFF4Stdout::NewAFF4Stdout(
                                &resolver, result));

        } else {
            RETURN_IF_ERROR(NewFileBackedObject(
                                &resolver, output_path, "truncate",
                                result));
        }

        resolver.logger->info(
            "Destination volume will be a flat file {}.",
            result->urn);

        return STATUS_OK;
    }

    AFF4Volume *volume;
    RETURN_IF_ERROR(GetCurrentVolume(&volume));

    // If the user asked for raw or no compression just write a normal stream.
    std::string format = GetArg<TCLAP::ValueArg<std::string>>("format")->getValue();
    if (format == "raw" || format == "elf") {
        // These formats are not compressed.
        return volume->CreateMemberStream(output_urn, result);
    } else {
        RETURN_IF_ERROR(AFF4Image::NewAFF4Image(
                            &resolver, output_urn,
                            volume,
                            result));
    }

    return STATUS_OK;
}



AFF4Status PmemImager::process_input() {
    if (volume_type != "aff4") {
        resolver.logger->info(
            "Output volume is not an AFF4 file. Cannot capture additional streams. "
            "Choose an AFF4 volume to capture additional streams.");
        return STATUS_OK;
    }

    return BasicImager::process_input();
}


#ifdef _WIN32
PmemImager::~PmemImager() {
  // Remove all files that need to be removed.
    for (std::string &filename : to_be_removed) {
        if (!DeleteFile(filename.c_str())) {
            resolver.logger->info("Unable to delete {}: {}", filename,
                                  GetLastErrorMessage());

        } else {
            resolver.logger->info("Removed {}", filename);
        }
    }
}
#else
PmemImager::~PmemImager() {
}
#endif

} // namespace aff4

int main(int argc, char* argv[]) {
    aff4::IMAGER_CLASS imager;
    aff4::AFF4Status res = imager.Run(argc, argv);
    if (res == aff4::STATUS_OK || res == aff4::CONTINUE)
          return 0;

    imager.resolver.logger->error("Imaging failed with error: {}",
                                  AFF4StatusToString(res));

    return res;
}
