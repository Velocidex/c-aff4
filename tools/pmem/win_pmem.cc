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
#include "windows.h"
#undef ERROR

#include "win_pmem.h"
#include "resources/winpmem/resources.cc"

#include <functional>
#include <string>

#include "aff4/aff4_io.h"
#include "aff4/aff4_symstream.h"

#include <yaml-cpp/yaml.h>

#define PAGE_SIZE (1<<12)

// For efficiency we read several pages at the time.
#define BUFF_SIZE (PAGE_SIZE * 256)

namespace aff4 {

/* Some utility functions. */
static AFF4Status CreateChildProcess(
    DataStore &resolver,
    const std::string &command, HANDLE stdout_wr) {

  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;
  BOOL bSuccess = FALSE;

  // Set up members of the PROCESS_INFORMATION structure.
  ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

  // Set up members of the STARTUPINFO structure.
  // This structure specifies the STDIN and STDOUT handles for redirection.
  ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
  siStartInfo.cb = sizeof(STARTUPINFO);
  siStartInfo.hStdInput = NULL;
  siStartInfo.hStdOutput = stdout_wr;
  siStartInfo.hStdError = stdout_wr;
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

  resolver.logger->info("Launching {}", command);

  // Create the child process.
  bSuccess = CreateProcess(
      NULL,
      const_cast<char *>(command.c_str()),       // command line
      NULL,          // process security attributes
      NULL,          // primary thread security attributes
      TRUE,          // handles are inherited
      0,             // creation flags
      NULL,          // use parent's environment
      NULL,          // use parent's current directory
      &siStartInfo,  // STARTUPINFO pointer
      &piProcInfo);  // receives PROCESS_INFORMATION

  // If an error occurs, exit the application.
  if (!bSuccess) {
      resolver.logger->error(
          "Unable to launch process: {}", GetLastErrorMessage());
    return IO_ERROR;
  }

  // Close handles to the child process and its primary thread.
  // Some applications might keep these handles to monitor the status
  // of the child process, for example.
  CloseHandle(piProcInfo.hProcess);
  CloseHandle(piProcInfo.hThread);
  CloseHandle(stdout_wr);

  return STATUS_OK;
}

static std::string _GetTempPath(DataStore &resolver) {
  CHAR path[MAX_PATH + 1];
  CHAR filename[MAX_PATH];

  // Extract the driver somewhere temporary.
  if (!GetTempPath(MAX_PATH, path)) {
      resolver.logger->error("Unable to determine temporary path.");
      return "";
  }

  // filename is now the random path.
  GetTempFileNameA(path, "pmem", 0, filename);

  return filename;
}

static DWORD _GetSystemArch() {
  SYSTEM_INFO sys_info;
  ZeroMemory(&sys_info, sizeof(sys_info));

  GetNativeSystemInfo(&sys_info);

  return sys_info.wProcessorArchitecture;
}

const unsigned char *GetDriverAtt(DataStore &resolver, size_t *file_size) {
  switch (_GetSystemArch()) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        *file_size = sizeof(att_winpmem_64);
        return att_winpmem_64;

    case PROCESSOR_ARCHITECTURE_INTEL:
        *file_size = sizeof(att_winpmem_32);
        return att_winpmem_32;

    default:
        resolver.logger->critical("I dont know what arch I am running on?");
        abort();
  }
}

const unsigned char *GetDriverNonAtt(DataStore &resolver, size_t *file_size) {
  switch (_GetSystemArch()) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        *file_size = sizeof(winpmem_64);
        return winpmem_64;

    case PROCESSOR_ARCHITECTURE_INTEL:
        *file_size = sizeof(winpmem_32);
        return winpmem_32;

    default:
        resolver.logger->critical("I dont know what arch I am running on?");
        abort();
  }
}


AFF4Status WinPmemImager::GetMemoryInfo(PmemMemoryInfo *info) {
  // We issue a DeviceIoControl() on the raw device handle to get the metadata.
  DWORD size;

  memset(info, 0, sizeof(*info));

  if (!device) {
      auto device_path = "\\\\.\\" + device_name;

      // We need write mode for issuing IO controls. Note the driver will refuse
      // write unless it is also switched to write mode.
      if (NewFileBackedObject(&resolver, device_path,
                              "append", device) != STATUS_OK) {
          resolver.logger->error("Cannot open device {}", device_path);
          return IO_ERROR;
      }

      // We do not want to cache anything here since we implement
      // paged reading to work around VSM IO Errors.
      device->cache_block_size = 0;
  }

  // Set the acquisition mode.
  if (acquisition_mode == PMEM_MODE_AUTO) {
    // For 64 bit systems we use PTE remapping.
    if (_GetSystemArch() == PROCESSOR_ARCHITECTURE_AMD64) {
      acquisition_mode = PMEM_MODE_PTE;
    } else {
      acquisition_mode = PMEM_MODE_IOSPACE;
    }
  }

  // Set the acquisition mode.
  if (!DeviceIoControl(device->fd, PMEM_CTRL_IOCTRL, &acquisition_mode,
                       sizeof(acquisition_mode), NULL, 0, &size, NULL)) {
      resolver.logger->error(
          "Failed to set acquisition mode: {}", GetLastErrorMessage());
      return IO_ERROR;
  } else {
      resolver.logger->info("Setting acquisition mode {}", acquisition_mode);
  }

  // Get the memory ranges.
  if (!DeviceIoControl(device->fd, PMEM_INFO_IOCTRL, NULL, 0,
                       reinterpret_cast<char *>(info),
                       sizeof(*info), &size, NULL)) {
      resolver.logger->error("Failed to get memory geometry: {}",
                             GetLastErrorMessage());
    return IO_ERROR;
  }

  return STATUS_OK;
}

static void print_memory_info_(
    DataStore *resolver, const PmemMemoryInfo &info) {
  StringIO output_stream;

  output_stream.sprintf("CR3: 0x%010llX\n %d memory ranges:\n", info.CR3,
                        info.NumberOfRuns);

  for (unsigned int i = 0; i < info.NumberOfRuns; i++) {
    output_stream.sprintf("Start 0x%08llX - Length 0x%08llX\n",
                          info.Runs[i].start, info.Runs[i].length);
  }

  resolver->logger->info(output_stream.buffer.c_str());
}


static std::string DumpMemoryInfoToYaml(const PmemMemoryInfo &info) {
  YAML::Emitter out;
  YAML::Node node;

  node["Imager"] = "WinPmem " PMEM_VERSION;
  YAML::Node registers_node;
  registers_node["CR3"] = info.CR3;
  node["Registers"] = registers_node;

  node["NtBuildNumber"] = info.NtBuildNumber;
  node["KernBase"] = info.KernBase;
  node["NtBuildNumberAddr"] = info.NtBuildNumberAddr;
  YAML::Node runs;
  for (size_t i = 0; i < info.NumberOfRuns; i++) {
    YAML::Node run;
    run["start"] = info.Runs[i].start;
    run["length"] = info.Runs[i].length;

    runs.push_back(run);
  }

  node["Runs"] = runs;

  out << node;
  return out.c_str();
}

// A private helper class to read from a pipe.
class _PipedReaderStream: public AFF4Stream {
 protected:
  HANDLE stdout_rd;

 public:
  explicit _PipedReaderStream(DataStore *resolver, HANDLE stdout_rd):
      AFF4Stream(resolver),
      stdout_rd(stdout_rd)
  {}

  AFF4Status ReadBuffer(char* data, size_t* length) override {
      DWORD bytes_read = 0;

      if (!ReadFile(stdout_rd, data, *length, &bytes_read, NULL)) {
          return STATUS_OK; // FIXME?
      }

      readptr += bytes_read;
      *length = bytes_read;

      return STATUS_OK;
  }

  virtual ~_PipedReaderStream() {
    CloseHandle(stdout_rd);
  }
};


AFF4Status WinPmemImager::ImagePageFile() {
  int pagefile_number = 0;

  // If the user did not specify pagefiles then do nothing.
  if (pagefiles.size() == 0)
    return CONTINUE;

  std::string fcat_path = _GetTempPath(resolver);
  if (fcat_path.size() == 0)
    return IO_ERROR;

  resolver.logger->info("fcat_urn {}", fcat_path);
  RETURN_IF_ERROR(ExtractFile_(fcat, sizeof(fcat), fcat_path));

  // Remember to clean up when done.
  to_be_removed.push_back(fcat_path);

  for (const std::string& pagefile_path : pagefiles) {
    // Now shell out to fcat and copy to the output.
    SECURITY_ATTRIBUTES saAttr;
    HANDLE stdout_rd = NULL;
    HANDLE stdout_wr = NULL;

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT.
    if (!CreatePipe(&stdout_rd, &stdout_wr, &saAttr, 0)) {
        resolver.logger->error("StdoutRd CreatePipe");
        return IO_ERROR;
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.
    SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);
    std::string command_line = aff4_sprintf(
        "%s %s \\\\.\\%s", fcat_path.c_str(),
        // path component.
        pagefile_path.substr(3, pagefile_path.size()).c_str(),
        // Drive letter.
        pagefile_path.substr(0, 2).c_str());

    RETURN_IF_ERROR(
        CreateChildProcess(resolver, command_line, stdout_wr));

    resolver.logger->info("Preparing to run {}", command_line.c_str());
    std::string buffer(BUFF_SIZE, 0);

    AFF4Volume* volume;
    RETURN_IF_ERROR(GetCurrentVolume(&volume))

    URN pagefile_urn = volume->urn.Append(
        URN::NewURNFromFilename(pagefile_path).Path());

    resolver.logger->info("Output will go to {}",
                          pagefile_urn.SerializeToString());

    AFF4Flusher<AFF4Stream> pagefile;
    RETURN_IF_ERROR(GetWritableStream_(pagefile_urn, pagefile));

    resolver.Set(pagefile->urn, AFF4_CATEGORY, new URN(AFF4_MEMORY_PAGEFILE));
    resolver.Set(pagefile->urn, AFF4_MEMORY_PAGEFILE_NUM,
                 new XSDInteger(pagefile_number));


    VolumeManager progress(&resolver, this);
    progress.ManageStream(pagefile.get());

    _PipedReaderStream reader_stream(&resolver, stdout_rd);
    RETURN_IF_ERROR(pagefile->WriteStream(&reader_stream, &progress));
  }

  actions_run.insert("pagefile");

  return CONTINUE;
}

AFF4Status WinPmemImager::CreateMap_(AFF4Map *map, aff4_off_t *length) {
  PmemMemoryInfo info;
  RETURN_IF_ERROR(GetMemoryInfo(&info));

  // Copy the memory to the output.
  for (unsigned int i = 0; i < info.NumberOfRuns; i++) {
    PHYSICAL_MEMORY_RANGE range = info.Runs[i];

    resolver.logger->info("Dumping Range {} (Starts at {:#08x}, length {:#08x}",
                          i, range.start, range.length);
    map->AddRange(range.start, range.start, range.length, device.get());
    *length += range.length;
  }

  return STATUS_OK;
}


AFF4Status WinPmemImager::WriteMapObject_(const URN &map_urn) {
  std::vector<std::string> unreadable_pages;

  AFF4Volume *volume;
  RETURN_IF_ERROR(GetCurrentVolume(&volume));

  // Create the backing stream;
  AFF4Flusher<AFF4Stream> data_stream;
  RETURN_IF_ERROR(AFF4Image::NewAFF4Image(
                      &resolver, map_urn.Append("data"),
                      volume, data_stream));

  // Create the map object.
  AFF4Flusher<AFF4Map> map_stream;
  RETURN_IF_ERROR(
      AFF4Map::NewAFF4Map(&resolver, map_urn, volume,
                          data_stream.get(), map_stream));

  // Set the user's preferred compression method on the data stream.
  resolver.Set(data_stream->urn, AFF4_IMAGE_COMPRESSION, new URN(
                   CompressionMethodToURN(compression)));

  // Get the ranges from the memory device.
  PmemMemoryInfo info;
  RETURN_IF_ERROR(GetMemoryInfo(&info));

  aff4_off_t total_length = 0;

  // How much data do we expect?
  for (unsigned int i = 0; i < info.NumberOfRuns; i++) {
    total_length += info.Runs[i].length;
  }

  VolumeManager progress(&resolver, this);
  progress.length = total_length;
  progress.ManageStream(map_stream.get());

  // Now copy the data one page at the time to be able to properly trap IO errors.
  for (unsigned int i = 0; i < info.NumberOfRuns; i++) {
    PHYSICAL_MEMORY_RANGE range = info.Runs[i];

    resolver.logger->info("Dumping Range {} (Starts at {:#08x}, length {:#08x}",
                          i, range.start, range.length);

    progress.start = range.start;
    progress.last_offset = range.start;

    char buf[BUFF_SIZE];
    aff4_off_t unreadable_offset = 0;

    for (aff4_off_t j=range.start; j<range.start + range.length; j += BUFF_SIZE) {
        size_t to_read = std::min((aff4_off_t)(BUFF_SIZE),
                                  (aff4_off_t)(range.start + range.length - j));
        size_t buffer_len = to_read;
        device->Seek(j, SEEK_SET);

        // Report the data read from the source.
        if (!progress.Report(j)) {
                return ABORTED;
        }

        AFF4Status res = device->ReadBuffer(buf, &buffer_len);
        if (res == STATUS_OK) {
            auto data_stream_offset = data_stream->Tell();

            // Append the data to the end of the data stream.
            data_stream->Write(buf, buffer_len);
            map_stream->AddRange(j, data_stream_offset, buffer_len, data_stream.get());
        } else {
            // This will happen when windows is running in VSM mode -
            // some of the physical pages are not actually accessible.

            AFF4Flusher<AFF4Stream> unreadable(
                new AFF4SymbolicStream(&resolver, AFF4_IMAGESTREAM_UNREADABLE,
                                       "UNREADABLEDATA"));

            // One of the pages in the range is unreadable - repeat
            // the read for each page.
            for (aff4_off_t k = j; k < j + to_read; k += PAGE_SIZE) {
                buffer_len = PAGE_SIZE;

                device->Seek(k, SEEK_SET);
                AFF4Status res = device->ReadBuffer(buf, &buffer_len);
                if (res == STATUS_OK) {
                    auto data_stream_offset = data_stream->Tell();

                    // Append the data to the end of the data stream.
                    data_stream->Write(buf, buffer_len);
                    map_stream->AddRange(k, data_stream_offset, buffer_len, data_stream.get());
                } else {
                    resolver.logger->debug("Reading failed at offset {:x}: {}",
                                           k, GetLastErrorMessage());

                    // Error occured - map the error stream.
                    map_stream->AddRange(
                        k,
                        unreadable_offset,
                        PAGE_SIZE,
                        unreadable.get());

                    // We need to map consecutive blocks in the
                    // unreadable stream to allow the map to merge
                    // these into single ranges. If we e.g. always map
                    // to the start of the unreadable stream, the map
                    // will contain an entry for each page and wont be
                    // able to merge them.
                    unreadable_offset += PAGE_SIZE;
                    unreadable_pages.push_back(aff4_sprintf(" %#08llx", (k / PAGE_SIZE)));
                }
            }
        }
    }

  }

  // Free some memory ASAP.
  if (unreadable_pages.size() > 0) {
      resolver.logger->error(
          "There were {} page read errors in total.", unreadable_pages.size());

      std::string message;
      for (int i=0; i<unreadable_pages.size(); i++) {
          message += unreadable_pages[i];
      }

      resolver.logger->info("Unreadable pages: {}", message);
  }

  return STATUS_OK;
}


// We image memory in the order of volatility - first the physical RAM, then the
// pagefile then any files that may be required.
AFF4Status WinPmemImager::ImagePhysicalMemory() {
  AFF4Status res;
  std::string format = GetArg<TCLAP::ValueArg<std::string>>("format")->getValue();

  std::string volume_format = GetArg<TCLAP::ValueArg<std::string>>(
      "volume_format")->getValue();

  if (volume_format == "raw" && format == "map") {
      format = "raw";
  }

  // First ensure that the driver is loaded.
  res = InstallDriver();
  if (res != CONTINUE)
    return res;

  std::string output_path = GetArg<TCLAP::ValueArg<std::string>>("output")->getValue();

  // We image memory into this map stream.
  AFF4Volume *volume;
  RETURN_IF_ERROR(GetCurrentVolume(&volume));

  URN map_urn = volume->urn.Append("PhysicalMemory");

  // Write the information into the image.
  {
      AFF4Flusher<AFF4Stream> information_stream;
      RETURN_IF_ERROR(volume->CreateMemberStream(
                          map_urn.Append("information.yaml"),
                          information_stream));

      PmemMemoryInfo info;
      RETURN_IF_ERROR(GetMemoryInfo(&info));

      if (information_stream->Write(DumpMemoryInfoToYaml(info)) < 0)
          return IO_ERROR;
  }

  if (format == "map") {
    res = WriteMapObject_(map_urn);
  } else if (format == "raw") {
    res = WriteRawFormat_(map_urn);
  } else if (format == "elf") {
    res = WriteElfFormat_(map_urn);
  }

  if (res != STATUS_OK) {
    return res;
  }

  // This is a physical memory image.
  resolver.Set(map_urn, AFF4_CATEGORY, new URN(AFF4_MEMORY_PHYSICAL));

  actions_run.insert("memory");

  // Now image the pagefiles.
  res = ImagePageFile();
  if (res != CONTINUE)
    return res;

  // Also capture these by default.
  if (inputs.size() == 0) {
      resolver.logger->info("Adding default file collections.");
      inputs.push_back("C:\\Windows\\SysNative\\drivers\\*.sys");

      // Used to bootstrap kernel GUID detection.
      inputs.push_back("C:\\Windows\\SysNative\\ntoskrnl.exe");
  }

  res = process_input();
  return res;
}

// Extract the driver file from our own volume.
AFF4Status WinPmemImager::ExtractFile_(
    const unsigned char input_file[],
    size_t input_file_length,
    std::string output_file) {

    private_resolver.Set(output_file, AFF4_STREAM_WRITE_MODE,
                         new XSDString("truncate"));

    AFF4Flusher<FileBackedObject> output;
    RETURN_IF_ERROR(NewFileBackedObject(
                        &resolver, output_file, "truncate", output));

    resolver.logger->info("Extracted {} bytes into {}", input_file_length, output_file);

    // These files should be small so dont worry about progress.
    AFF4Status res = output->Write((const char *)input_file, input_file_length);
    if (res != STATUS_OK) {
        resolver.logger->error("Unable to extract {}: {}", output_file, res);
    }

    return res;
}


AFF4Status WinPmemImager::_InstallDriver(std::string driver_path) {
  // Now install the driver.
  UninstallDriver();   // First ensure the driver is not already installed.

  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
  if (!scm) {
      resolver.logger->error("Cannot open SCM. Are you administrator?");
      return IO_ERROR;
  }

  // First try to create the service.
  SC_HANDLE service = CreateService(
      scm,
      service_name.c_str(),
      service_name.c_str(),
      SERVICE_ALL_ACCESS,
      SERVICE_KERNEL_DRIVER,
      SERVICE_DEMAND_START,
      SERVICE_ERROR_NORMAL,
      driver_path.c_str(),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL);

  // Maybe the service is already there - try to open it instead.
  if (GetLastError() == ERROR_SERVICE_EXISTS) {
    service = OpenService(scm, service_name.c_str(),
                          SERVICE_ALL_ACCESS);
  }

  if (!service) {
    CloseServiceHandle(scm);
    return IO_ERROR;
  }

  if (!StartService(service, 0, NULL)) {
    if (GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        resolver.logger->error(
            "Error: StartService(), Cannot start the driver: {}",
            GetLastErrorMessage());
      CloseServiceHandle(service);
      CloseServiceHandle(scm);

      return IO_ERROR;
    }
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);

  return STATUS_OK;
}

AFF4Status WinPmemImager::InstallDriver() {
  std::string driver_path;
  AFF4Status res;

  // We need to extract the driver somewhere temporary.
  if (!Get("driver")->isSet()) {
    driver_path = _GetTempPath(resolver);
    if (driver_path.size() == 0)
      return IO_ERROR;

    // It is not clear to me when to use attestation signed drivers
    // and when not to - We try to load the attestation signed ones
    // first, and if that fails we try the other ones. On modern win10
    // systems the attestation signed drivers are required, while on
    // windows 7 they cannot be loaded.
    size_t file_size;
    const unsigned char* file_contents = GetDriverAtt(resolver, &file_size);
    RETURN_IF_ERROR(ExtractFile_(
                        file_contents,
                        file_size,
                        driver_path));   // Where to store the driver.

    // Remove this file when we are done.
    to_be_removed.push_back(driver_path);

    res = _InstallDriver(driver_path);
    if (res != STATUS_OK) {
        resolver.logger->info("Trying to load non-attestation signed driver.");
        driver_path = _GetTempPath(resolver);
        if (driver_path.size() == 0)
            return IO_ERROR;

        size_t file_size;
        const unsigned char* file_contents = GetDriverNonAtt(resolver, &file_size);
        RETURN_IF_ERROR(ExtractFile_(
                            file_contents,
                            file_size,
                            driver_path));   // Where to store the driver.

        to_be_removed.push_back(driver_path);
        RETURN_IF_ERROR(_InstallDriver(driver_path));
    }
  } else {
      // Use the driver the user told us to.
      driver_path = GetArg<TCLAP::ValueArg<std::string>>("driver")->getValue();
      RETURN_IF_ERROR(_InstallDriver(driver_path));
  }

  // Remember this so we can safely unload it.
  driver_installed_ = true;

  resolver.logger->info("Loaded Driver {}", driver_path);

  // Now print some info about the driver.
  PmemMemoryInfo info;
  RETURN_IF_ERROR(GetMemoryInfo(&info));

  print_memory_info_(&resolver, info);

  actions_run.insert("load-driver");
  return CONTINUE;
}


AFF4Status WinPmemImager::UninstallDriver() {
  SC_HANDLE scm, service;
  SERVICE_STATUS ServiceStatus;

  scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);

  if (!scm)
    return IO_ERROR;

  service = OpenService(scm, service_name.c_str(), SERVICE_ALL_ACCESS);

  if (service) {
    ControlService(service, SERVICE_CONTROL_STOP, &ServiceStatus);
  }

  DeleteService(service);
  CloseServiceHandle(service);
  CloseServiceHandle(scm);
  resolver.logger->info("Driver Unloaded.");

  // Close the handle to the device so we can remove it.
  device.release();

  actions_run.insert("unload-driver");
  return CONTINUE;
}


AFF4Status WinPmemImager::ParseArgs() {
  AFF4Status result = PmemImager::ParseArgs();

  resolver.logger->info("This is {} version {}", GetName(), GetVersion());

  // Configure our private resolver like the global one.
  private_resolver.logger->set_level(resolver.logger->level());
  private_resolver.logger->set_pattern("%Y-%m-%d %T %L %v");

  // Sanity checks.
  if (result == CONTINUE && Get("load-driver")->isSet() &&
      Get("unload-driver")->isSet()) {
      resolver.logger->error("You cannot specify both the -l and -u options together.");
    return INVALID_INPUT;
  }

  if (result == CONTINUE && Get("pagefile")->isSet())
    result = handle_pagefiles();

  if (result == CONTINUE && Get("mode")->isSet())
    result = handle_acquisition_mode();

  return result;
}

AFF4Status WinPmemImager::ProcessArgs() {
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

WinPmemImager::~WinPmemImager() {
  // Unload the driver if we loaded it and the user specifically does not want
  // it to be left behind.
  if (driver_installed_) {
    if (Get("load-driver")->isSet()) {
      resolver.logger->warn(
          "Memory access driver left loaded since you specified "
          "the -l flag.");
    } else {
      UninstallDriver();
    }
  }
}

AFF4Status WinPmemImager::handle_acquisition_mode() {
    std::string mode = GetArg<TCLAP::ValueArg<std::string>>("mode")->getValue();

  if (mode == "MmMapIoSpace") {
    acquisition_mode = PMEM_MODE_IOSPACE;
  } else if (mode == "PhysicalMemory") {
    acquisition_mode = PMEM_MODE_PHYSICAL;
  } else if (mode == "PTERemapping") {
    acquisition_mode = PMEM_MODE_PTE;
  } else {
      resolver.logger->error("Invalid acquisition mode specified: {}", mode);
    return IO_ERROR;
  }

  return CONTINUE;
}

AFF4Status WinPmemImager::handle_pagefiles() {
    std::vector<std::string> pagefile_args = GetArg<TCLAP::MultiArgToNextFlag>(
      "pagefile")->getValue();

  for (auto it : pagefile_args) {
    char path[MAX_PATH];

    if (GetFullPathName(it.c_str(), MAX_PATH, path, NULL) == 0) {
        resolver.logger->error("GetFullPathName failed: {}", GetLastErrorMessage());
      return IO_ERROR;
    }

    pagefiles.push_back(path);
  }

  return CONTINUE;
}

} // namespace aff4
