/*
Copyright 2015 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

#include "osxpmem.h"
#include <stdlib.h>
#include <libgen.h>
#include <sys/sysctl.h>
#include <mach-o/dyld.h>
#include <yaml-cpp/yaml.h>
#include <fts.h>

namespace aff4 {


AFF4Status PmemMetadata::LoadMetadata(std::string sysctl_name) {
  size_t metalen = buf_.size();
  pmem_meta_t *meta = get_meta();

  // Get the required size of the meta struct (it varies).
  sysctlbyname(sysctl_name.c_str(), 0, &metalen, 0, 0);

  // Allocate the required number of bytes.
  buf_.resize(metalen);
  meta = get_meta();

  int error = sysctlbyname(sysctl_name.c_str(), meta, &metalen, 0, 0);
  // We need at least one struct full of data.
  if (error == 0 && metalen > sizeof(pmem_meta_t)) {
    return STATUS_OK;
  }

  if (meta->pmem_api_version != MINIMUM_PMEM_API_VERSION) {
      resolver_->logger->error(
          "Pmem driver version incompatible. Reported {} required: {}",
          meta->pmem_api_version, static_cast<int>(MINIMUM_PMEM_API_VERSION));
    return INCOMPATIBLE_TYPES;
  }

  return STATUS_OK;
}

pmem_meta_t *PmemMetadata::get_meta() {
  return reinterpret_cast<pmem_meta_t *>(&buf_[0]);
}

// Extracting records from the pmem_meta_t struct is pretty tricky
// since it is a variable length struct. We just copy them into a
// vector so we can iterate over them easier.
std::vector<pmem_meta_record_t> PmemMetadata::get_records() {
  std::vector<pmem_meta_record_t> result;
  pmem_meta_t *meta = get_meta();
  size_t meta_size = get_meta_size();
  size_t meta_index = meta->records_offset;

  for (int i=0; i< meta->record_count; i++) {
    // Cast the record and watch out for overflow.
    if (meta_index + sizeof(pmem_meta_record_t) > meta_size)
      break;

    pmem_meta_record_t *record = reinterpret_cast<pmem_meta_record_t *>
      (reinterpret_cast<char *>(meta) + meta_index);

    result.push_back(*record);
    meta_index += record->size;
  }

  return result;
}

size_t PmemMetadata::get_meta_size() {
  return buf_.size();
};

std::string OSXPmemImager::DumpMemoryInfoToYaml() {
  pmem_meta_t *meta = metadata.get_meta();
  YAML::Emitter out;
  YAML::Node node;

  node["Imager"] = "OSXPmem " PMEM_VERSION;
  YAML::Node registers_node;
  registers_node["CR3"] = meta->cr3;
  node["Registers"] = registers_node;

  node["kernel_poffset"] = meta->kernel_poffset;
  node["kaslr_slide"] = meta->kaslr_slide;
  node["kernel_version"] = meta->kernel_version;
  node["kernel_version_poff"] = meta->version_poffset;

  YAML::Node runs;

  for (auto record: metadata.get_records()) {
    if (record.type == pmem_efi_range_type &&
        metadata.efi_readable(record.efi_range.efi_type)) {
      YAML::Node run;
      run["start"] = record.efi_range.start;
      run["length"] = record.efi_range.length;
      run["purpose"] = record.purpose;
      runs.push_back(run);
    }
  }

  node["Runs"] = runs;

  out << node;
  return out.c_str();
}

AFF4Status OSXPmemImager::ImagePhysicalMemory() {
  resolver.logger->info("Imaging memory");
  std::string format = GetArg<TCLAP::ValueArg<std::string>>("format")->getValue();

  AFF4Status res;

  res = InstallDriver();
  if (res != STATUS_OK)
    return res;

  // When the output volume is raw - we image in raw or elf format.
  if (volume_type == "raw") {
      return WriteRawVolume_();
  }

  std::string output_path = GetArg<TCLAP::ValueArg<std::string>>("output")->getValue();

  // When the output volume is raw - we image in raw or elf format.
  if (volume_type == "raw") {
      output_volume_backing_urn = URN::NewURNFromFilename(output_path);
      if (output_path == "-") {
          output_volume_backing_urn = URN("builtin://stdout");
      }

      resolver.Set(output_volume_backing_urn, AFF4_STREAM_WRITE_MODE,
                   new XSDString("truncate"));

      if (format == "elf") {
          return WriteElfFormat_(output_volume_backing_urn, output_volume_backing_urn);
      }
      return WriteRawFormat_(output_volume_backing_urn, output_volume_backing_urn);
  }

  URN output_urn;
  res = GetOutputVolumeURN(&output_volume_urn);
  if (res != STATUS_OK)
    return res;

  // We image memory into this map stream.
  URN map_urn = output_volume_urn.Append(device_name);

  AFF4ScopedPtr<AFF4Volume> volume = resolver.AFF4FactoryOpen<AFF4Volume>(
      output_volume_urn);

  // This is a physical memory image.
  resolver.Set(map_urn, AFF4_CATEGORY, new URN(AFF4_MEMORY_PHYSICAL));

  if (format == "map") {
    res = WriteMapObject_(map_urn, output_volume_urn);
  } else if (format == "raw") {
    res = WriteRawFormat_(map_urn, output_volume_urn);
  } else if (format == "elf") {
    res = WriteElfFormat_(map_urn, output_volume_urn);
  }

  if (res != STATUS_OK) {
    return res;
  }

  // Write the information.yaml file for the memory stream.
  AFF4ScopedPtr<AFF4Stream> information_stream = volume->CreateMember(
      map_urn.Append("information.yaml"));

  if (!information_stream) {
      resolver.logger->error("Unable to create memory information yaml.");
    return IO_ERROR;
  }

  if (information_stream->Write(DumpMemoryInfoToYaml()) < 0)
    return IO_ERROR;

  actions_run.insert("memory");

  // Also capture these files by default.
  if (inputs.size() == 0) {
      resolver.logger->info("Adding default file collections.");
    inputs.push_back("/boot/*");
  }

  return res;
}


AFF4Status OSXPmemImager::CreateMap_(AFF4Map *map, aff4_off_t *length) {
  AFF4ScopedPtr<FileBackedObject> device_stream = resolver.AFF4FactoryOpen
    <FileBackedObject>(device_urn);

  if (!device_stream) {
      resolver.logger->error("Unable to open {} - Are you root?", device_name);
      return IO_ERROR;
  }

  auto records = metadata.get_records();

  for (auto record: metadata.get_records()) {
    if (record.type == pmem_efi_range_type &&
        metadata.efi_readable(record.efi_range.efi_type)) {
      map->AddRange(record.efi_range.start,
                    record.efi_range.start,
                    record.efi_range.length,
                    device_urn);
      *length += record.efi_range.length;
    }
  }

  if (map->Size() == 0) {
      resolver.logger->info("No ranges found.");
      return NOT_FOUND;
  }

  return STATUS_OK;
}


AFF4Status OSXPmemImager::ParseArgs() {
  AFF4Status result = PmemImager::ParseArgs();

  // Sanity checks.
  if (result == CONTINUE && Get("load-driver")->isSet() &&
      Get("unload-driver")->isSet()) {
      resolver.logger->critical(
          "You cannot specify both the -l and -u options together.");
    return INVALID_INPUT;
  }

  std::string device = GetArg<TCLAP::ValueArg<std::string>>("device")->getValue();

  device_name = aff4_sprintf("/dev/%s", device.c_str());
  sysctl_name = aff4_sprintf("kern.%s_info", device.c_str());
  device_urn = URN::NewURNFromFilename(device_name);

  driver_urn = URN::NewURNFromFilename(
    GetArg<TCLAP::ValueArg<std::string>>("driver")->getValue());

  return result;
}

AFF4Status OSXPmemImager::ProcessArgs() {
  AFF4Status result = CONTINUE;

  // If load-driver was issued we break here.
  if (result == CONTINUE && Get("load-driver")->isSet())
    result = InstallDriver();

  // If load-driver was issued we break here.
  if (result == CONTINUE && Get("unload-driver")->isSet())
    result = UninstallDriver();

  if (result == CONTINUE)
    result = PmemImager::ProcessArgs();

  return result;
}

AFF4Status OSXPmemImager::UninstallDriver() {
  std::string driver_path = get_driver_path();
  resolver.logger->info("Unloading driver {}",  driver_path);
  std::string argv = aff4_sprintf("/sbin/kextunload %s", driver_path.c_str());
  if (system(argv.c_str()) != 0) {
      resolver.logger->error("Unable to unload driver at {}", driver_path);
    return IO_ERROR;
  }
  driver_installed_ = false;

  return STATUS_OK;
}

std::string OSXPmemImager::get_driver_path() {
    // Device does not exist, try to load it ourselves.
    char path[1024 * 4];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        resolver.logger->error("Executable path too long.");
        return "";
    }

    return aff4_sprintf("%s/MacPmem.kext", dirname(path));
};


void OSXPmemImager::fix_path(const std::string& path, mode_t mode) {
    if (lchown(path.c_str(), 0, 0) != 0) {
        resolver.logger->info("Cannot chown {} ({}). Driver may not load.",
                              path, strerror(errno));
    }

    if (chmod(path.c_str(), 07700 & mode) != 0) {
        resolver.logger->info("Cannot chmod {} ({}). Driver may not load.",
                              path, strerror(errno));
    }
}


AFF4Status OSXPmemImager::fix_file_permissions(const std::string& path) {
    fix_path(path, 0700);
    char *path_name[] = {const_cast<char*>(path.c_str()), nullptr};
    FTS* top_level = fts_open(path_name, FTS_XDEV | FTS_LOGICAL, nullptr);
    if (!top_level) {
        resolver.logger->error("{}: {}", path, strerror(errno));
        return IO_ERROR;
    }

    while(fts_read(top_level)) {
        FTSENT *child = fts_children(top_level, 0);

        while(child) {
            std::string child_path = child->fts_accpath;
            child_path.append("/");
            child_path.append(child->fts_name);
            fix_path(child_path, child->fts_statp->st_mode);
            child = child->fts_link;
        }
    }

    return STATUS_OK;
}


AFF4Status OSXPmemImager::InstallDriver() {
  AFF4ScopedPtr<FileBackedObject> device_stream = resolver.AFF4FactoryOpen
    <FileBackedObject>(device_urn);

  if (!device_stream) {
    std::string driver_path = get_driver_path();
    RETURN_IF_ERROR(fix_file_permissions(driver_path));

    std::string argv = aff4_sprintf("/sbin/kextload %s", driver_path.c_str());
    if (system(argv.c_str()) != 0) {
        resolver.logger->error("Unable to load driver at {}", driver_path);
      return IO_ERROR;
    }
    resolver.logger->info("Loading driver from {}", driver_path);
    driver_installed_ = true;
  }

  // Try to load the metadata from the driver now.
  AFF4Status res = metadata.LoadMetadata(sysctl_name);
  if (res != STATUS_OK)
    return res;

  return STATUS_OK;
}


OSXPmemImager::~OSXPmemImager() {
  if (driver_installed_) {
    if (Get("load-driver")->isSet()) {
      resolver.logger->info("Memory access driver left loaded since you specified "
                            "the -l flag.");
    } else
      UninstallDriver();
  }
}

} // namespace aff4
