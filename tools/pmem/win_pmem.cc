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

#include <functional>
#include <string>

#include "aff4/aff4_io.h"

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

static std::string GetDriverName(DataStore &resolver) {
  switch (_GetSystemArch()) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      return "winpmem_64.sys";
      break;

    case PROCESSOR_ARCHITECTURE_INTEL:
      return "winpmem_32.sys";
      break;

    default:
        resolver.logger->critical("I dont know what arch I am running on?");
        abort();
  }
}

AFF4Status WinPmemImager::GetMemoryInfo(PmemMemoryInfo *info) {
  // We issue a DeviceIoControl() on the raw device handle to get the metadata.
  DWORD size;

  memset(info, 0, sizeof(*info));

  AFF4ScopedPtr<FileBackedObject> device_stream = resolver.AFF4FactoryOpen
      <FileBackedObject>(device_urn);

  if (!device_stream) {
      resolver.logger->error("Can not open device {}", device_urn);
      return IO_ERROR;
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
  if (!DeviceIoControl(device_stream->fd, PMEM_CTRL_IOCTRL, &acquisition_mode,
                       sizeof(acquisition_mode), NULL, 0, &size, NULL)) {
      resolver.logger->error(
          "Failed to set acquisition mode: {}", GetLastErrorMessage());
      return IO_ERROR;
  } else {
      resolver.logger->info("Setting acquisition mode {}", acquisition_mode);
  }

  // Get the memory ranges.
  if (!DeviceIoControl(device_stream->fd, PMEM_INFO_IOCTRL, NULL, 0,
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

  std::string Read(size_t length) {
      std::string buffer(length, 0);
      DWORD bytes_read = buffer.size();

      if (!ReadFile(stdout_rd, &buffer[0], bytes_read, &bytes_read, NULL)) {
          return "";
      }

      readptr += bytes_read;
      return buffer;
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

  URN fcat_urn = URN::NewURNFromFilename(fcat_path);
  resolver.logger->info("fcat_urn {}", fcat_urn);
  AFF4Status res = ExtractFile_(imager_urn.Append("fcat.exe"),
                                fcat_urn);

  if (res != STATUS_OK)
    return res;

  // Remember to clean up when done.
  to_be_removed.push_back(fcat_urn);

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

    res = CreateChildProcess(resolver, command_line, stdout_wr);
    if (res != STATUS_OK) {
      to_be_removed.clear();

      return res;
    }

    resolver.logger->info("Preparing to run {}", command_line.c_str());
    std::string buffer(BUFF_SIZE, 0);
    URN volume_urn;
    AFF4Status res = GetOutputVolumeURN(&volume_urn);
    if (res != STATUS_OK)
      return res;

    URN pagefile_urn = volume_urn.Append(
        URN::NewURNFromFilename(pagefile_path).Path());

    resolver.logger->info("Output will go to {}",
                          pagefile_urn.SerializeToString());

    AFF4ScopedPtr<AFF4Stream> output_stream = GetWritableStream_(
        pagefile_urn, volume_urn);

    if (!output_stream)
      return IO_ERROR;

    resolver.Set(pagefile_urn, AFF4_CATEGORY, new URN(AFF4_MEMORY_PAGEFILE));
    resolver.Set(pagefile_urn, AFF4_MEMORY_PAGEFILE_NUM,
                 new XSDInteger(pagefile_number));


    DefaultProgress progress(&resolver);
    _PipedReaderStream reader_stream(&resolver, stdout_rd);
    res = output_stream->WriteStream(&reader_stream, &progress);
    if (res != STATUS_OK)
      return res;
  }

  actions_run.insert("pagefile");
  return CONTINUE;
}

AFF4Status WinPmemImager::CreateMap_(AFF4Map *map, aff4_off_t *length) {
  PmemMemoryInfo info;
  AFF4Status res = GetMemoryInfo(&info);
  if (res != STATUS_OK)
    return res;

  // Copy the memory to the output.
  for (unsigned int i = 0; i < info.NumberOfRuns; i++) {
    PHYSICAL_MEMORY_RANGE range = info.Runs[i];

    resolver.logger->info("Dumping Range {} (Starts at {:#08x}, length {:#08x}",
                          i, range.start, range.length);
    map->AddRange(range.start, range.start, range.length, device_urn);
    *length += range.length;
  }

  return STATUS_OK;
}


AFF4Status WinPmemImager::WriteMapObject_(
    const URN &map_urn, const URN &output_urn) {

  std::vector<std::string> unreadable_pages;

  // Get the device object
  AFF4ScopedPtr<AFF4Stream> device_stream = resolver.AFF4FactoryOpen<
      AFF4Stream>(device_urn);

  if (!device_stream)
    return IO_ERROR;

  // Create the map object.
  AFF4ScopedPtr<AFF4Map> map_stream = AFF4Map::NewAFF4Map(
      &resolver, map_urn, output_urn);

  if (!map_stream)
    return IO_ERROR;

  // Read data from the memory device and write it directly to the
  // map's backing urn.
  URN data_stream_urn;
  RETURN_IF_ERROR(map_stream->GetBackingStream(data_stream_urn));

  AFF4ScopedPtr<AFF4Stream> data_stream = resolver.AFF4FactoryOpen<
      AFF4Stream>(data_stream_urn);

  if (!data_stream) {
      return IO_ERROR;
  }

  // Set the user's preferred compression method on the data stream.
  resolver.Set(data_stream_urn, AFF4_IMAGE_COMPRESSION, new URN(
      CompressionMethodToURN(compression)));


  // Get the ranges from the memory device.
  PmemMemoryInfo info;
  AFF4Status res = GetMemoryInfo(&info);
  if (res != STATUS_OK)
    return res;

  aff4_off_t total_length = 0;

  // How much data do we expect?
  for (unsigned int i = 0; i < info.NumberOfRuns; i++) {
    total_length += info.Runs[i].length;
  }

  DefaultProgress progress(&resolver);
  progress.length = total_length;

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
        device_stream->Seek(j, SEEK_SET);

        // Report the data read from the source.
        if (!progress.Report(j)) {
                return ABORTED;
        }

        AFF4Status res = device_stream->ReadBuffer(buf, &buffer_len);
        if (res == STATUS_OK) {
            auto data_stream_offset = data_stream->Tell();

            // Append the data to the end of the data stream.
            data_stream->Write(buf, buffer_len);
            map_stream->AddRange(j, data_stream_offset, buffer_len, data_stream_urn);
        } else {
            // This will happen when windows is running in VSM mode -
            // some of the physical pages are not actually accessible.

            // One of the pages in the range is unreadable - repeat
            // the read for each page.
            for (aff4_off_t k = j; k < j + to_read; k += PAGE_SIZE) {
                buffer_len = PAGE_SIZE;

                device_stream->Seek(k, SEEK_SET);
                AFF4Status res = device_stream->ReadBuffer(buf, &buffer_len);
                if (res == STATUS_OK) {
                    auto data_stream_offset = data_stream->Tell();

                    // Append the data to the end of the data stream.
                    data_stream->Write(buf, buffer_len);
                    map_stream->AddRange(k, data_stream_offset, buffer_len, data_stream_urn);
                } else {
                    resolver.logger->debug("Reading failed at offset {:x}: {}",
                                           k, GetLastErrorMessage());

                    // Error occured - map the error stream.
                    map_stream->AddRange(
                        k,
                        unreadable_offset,
                        PAGE_SIZE,
                        AFF4_IMAGESTREAM_UNREADABLE);

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
  resolver.Close(data_stream);
  resolver.Close(map_stream);

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

  // First ensure that the driver is loaded.
  res = InstallDriver();
  if (res != CONTINUE)
    return res;

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
  URN map_urn = output_volume_urn.Append("PhysicalMemory");

  AFF4ScopedPtr<AFF4Volume> volume = resolver.AFF4FactoryOpen<AFF4Volume>(
      output_volume_urn);

  // Write the information into the image.
  AFF4ScopedPtr<AFF4Stream> information_stream = volume->CreateMember(
      map_urn.Append("information.yaml"));

  if (!information_stream) {
      resolver.logger->error("Unable to create memory information yaml.");
      return IO_ERROR;
  }

  PmemMemoryInfo info;
  res = GetMemoryInfo(&info);
  if (res != STATUS_OK)
    return res;

  if (information_stream->Write(DumpMemoryInfoToYaml(info)) < 0)
    return IO_ERROR;

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
AFF4Status WinPmemImager::ExtractFile_(URN input_file, URN output_file) {
  // We extract our own files from the private resolver.
  AFF4ScopedPtr<AFF4Stream> input_file_stream = private_resolver.AFF4FactoryOpen
      <AFF4Stream>(input_file);

  if (!input_file_stream) {
      resolver.logger->critical("Unable to extract the correct driver ({}) - "
                                "maybe the binary is damaged?", input_file);
      abort();
  }

  private_resolver.Set(output_file, AFF4_STREAM_WRITE_MODE,
                       new XSDString("truncate"));

  AFF4ScopedPtr<AFF4Stream> outfile = private_resolver.AFF4FactoryOpen
      <AFF4Stream>(output_file);

  if (!outfile) {
      resolver.logger->critical("Unable to create driver file.");
      abort();
  }

  resolver.logger->info("Extracted {} to {}", input_file, output_file);

  // These files should be small so dont worry about progress.
  ProgressContext empty_progress(&resolver);
  AFF4Status res = input_file_stream->CopyToStream(
      *outfile, input_file_stream->Size(), &empty_progress);

  if (res == STATUS_OK)
    // We must make sure to close the file or we will not be able to load it
    // while we hold a handle to it.
    res = private_resolver.Close<AFF4Stream>(outfile);

  if (res != STATUS_OK) {
      resolver.logger->error("Unable to extract {}", input_file);
  }

  return res;
}


AFF4Status WinPmemImager::_InstallDriver(std::string driver_path) {
  // Now install the driver.
  UninstallDriver();   // First ensure the driver is not already installed.

  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
  if (!scm) {
      resolver.logger->error("Can not open SCM. Are you administrator?");
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
    // windows 7 they can not be loaded.
    URN filename_urn = URN::NewURNFromFilename(driver_path);
    RETURN_IF_ERROR(ExtractFile_(
        imager_urn.Append("att_" + GetDriverName(resolver)),   // Driver URN relative to imager.
        filename_urn));   // Where to store the driver.

    // Remove this file when we are done.
    to_be_removed.push_back(filename_urn);

    res = _InstallDriver(filename_urn.ToFilename());
    if (res != STATUS_OK) {
        resolver.logger->info("Trying to load non-attestation signed driver.");
        driver_path = _GetTempPath(resolver);
        if (driver_path.size() == 0)
            return IO_ERROR;

        URN filename_urn = URN::NewURNFromFilename(driver_path);
        RETURN_IF_ERROR(
            ExtractFile_(
                imager_urn.Append(GetDriverName(resolver)),   // Driver URN relative to imager.
                filename_urn));   // Where to store the driver.

        to_be_removed.push_back(filename_urn);
        RETURN_IF_ERROR(_InstallDriver(filename_urn.ToFilename()));
    }
  } else {
      // Use the driver the user told us to.
      driver_path = GetArg<TCLAP::ValueArg<std::string>>("driver")->getValue();
      RETURN_IF_ERROR(_InstallDriver(driver_path));
  }

  // Remember this so we can safely unload it.
  driver_installed_ = true;

  resolver.logger->info("Loaded Driver {}", driver_path);
  device_urn = URN::NewURNFromFilename("\\\\.\\" + device_name);

  // We need write mode for issuing IO controls. Note the driver will refuse
  // write unless it is also switched to write mode.
  resolver.Set(device_urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));

  AFF4ScopedPtr<FileBackedObject> device_stream = resolver.AFF4FactoryOpen
      <FileBackedObject>(device_urn);

  if (!device_stream) {
      resolver.logger->error("Unable to open device: {}", GetLastErrorMessage());
    return IO_ERROR;
  }

  // Now print some info about the driver.
  PmemMemoryInfo info;
  res = GetMemoryInfo(&info);
  if (res != STATUS_OK)
    return res;

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
  AFF4ScopedPtr<AFF4Stream> device = resolver.AFF4FactoryOpen<AFF4Stream>(
      device_urn);
  if (device.get()) {
      resolver.Close(device);
  }

  actions_run.insert("unload-driver");
  return CONTINUE;
}


AFF4Status WinPmemImager::handle_driver() {
    resolver.logger->info("Extracting WinPmem drivers from binary.");
  // We need to load the AFF4 volume attached to our own executable.
  HMODULE hModule = GetModuleHandleW(NULL);
  CHAR path[MAX_PATH];
  GetModuleFileNameA(hModule, path, MAX_PATH);

  AFF4ScopedPtr<ZipFile> volume = ZipFile::NewZipFile(
      &private_resolver, URN::NewURNFromFilename(path));

  if (!volume) {
      resolver.logger->critical(
          "Unable to extract drivers. Maybe the executable is damaged?");
      abort();
  }

  resolver.logger->info("Openning driver AFF4 volume: {}", volume->urn);

  imager_urn = volume->urn;

  return CONTINUE;
}


AFF4Status WinPmemImager::ParseArgs() {
  AFF4Status result = PmemImager::ParseArgs();

  resolver.logger->info("This is {} version {}", GetName(), GetVersion());

  // Configure our private resolver like the global one.
  private_resolver.logger->set_level(resolver.logger->level());
  private_resolver.logger->set_pattern("%Y-%m-%d %T %L %v");

  if (result == CONTINUE) {
      result = handle_driver();
  }

  // Sanity checks.
  if (result == CONTINUE && Get("load-driver")->isSet() &&
      Get("unload-driver")->isSet()) {
      resolver.logger->error("You can not specify both the -l and -u options together.");
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
