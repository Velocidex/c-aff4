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

#include "aff4/aff4_base.h"
#include "aff4/aff4_file.h"
#include "aff4/aff4_directory.h"
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif


namespace aff4 {

/***************************************************************
FileBackedObject implementation.
****************************************************************/

static std::string _GetFilename(DataStore* resolver, const URN& urn) {
    XSDString filename;
    if (resolver->Get(urn, AFF4_FILE_NAME, filename) != STATUS_OK) {
        // Only file:// URNs are supported.
        if (urn.Scheme() == "file") {
            return urn.ToFilename();
        }
    }

    return filename.SerializeToString();
}

// Recursively create intermediate directories.
AFF4Status _CreateIntermediateDirectories(
    DataStore *resolver, std::vector<std::string> components) {
    std::string path = PATH_SEP_STR;

#ifdef _WIN32
    // On windows leading \\ means a device - we do not want to create
    // any intermediate directories for devices.
    if (components.size() > 2 &&
        components[0] == "" && components[1] == "") {
        return STATUS_OK;
    }

    // On windows we do not want a leading \ (e.g. C:\windows not \C:\Windows)
    path = "";
#endif

    for (auto component : components) {
        path = path + component + PATH_SEP_STR;
        resolver->logger->debug("Creating intermediate directories {}",
                                path);

        if (AFF4Directory::IsDirectory(path, /* must_exist= */ true)) {
            continue;
        }

        // Directory does not exist - Try to make it.
        if (AFF4Directory::MkDir(resolver, path) == STATUS_OK) {
            continue;
        }

        resolver->logger->error(
            "Unable to create intermediate directory: {}",
            GetLastErrorMessage());
        return IO_ERROR;
    }

    return STATUS_OK;
}

AFF4Status _CreateIntermediateDirectories(DataStore *resolver, std::string dir_name) {
    return _CreateIntermediateDirectories(resolver, split(dir_name, PATH_SEP));
}


// Windows files are read through the CreateFile() API so that devices can be
// read.
#if defined(_WIN32)

AFF4Status FileBackedObject::LoadFromURN() {
    DWORD desired_access = GENERIC_READ;
    DWORD creation_disposition = OPEN_EXISTING;

    XSDString mode("read");

    // The Resolver might have the correct filename for this URN.
    filename = _GetFilename(resolver, urn);
    if (filename.size() == 0) {
        return INVALID_INPUT;
    }

    resolver->logger->debug("Opening file {}", filename);

    std::vector<std::string> directory_components = split(filename, PATH_SEP);
    directory_components.pop_back();

    // Attribute is optional so if it is not there we just go with false.
    resolver->Get(urn, AFF4_STREAM_WRITE_MODE, mode);

    if (mode == "truncate") {
        creation_disposition = CREATE_ALWAYS;
        desired_access |= GENERIC_WRITE;

        // Next call will append.
        resolver->Set(urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"),
                      /* replace = */ true);
        properties.writable = true;

        // Only create directories if we are allowed to.
        AFF4Status res = _CreateIntermediateDirectories(resolver, directory_components);
        if (res != STATUS_OK) {
            return res;
        }

    } else if (mode == "append") {
        creation_disposition = OPEN_ALWAYS;
        desired_access |= GENERIC_WRITE;
        properties.writable = true;

        // Only create directories if we are allowed to.
        AFF4Status res = _CreateIntermediateDirectories(resolver, directory_components);
        if (res != STATUS_OK) {
            return res;
        }
    }

    fd = CreateFile(filename.c_str(),
                    desired_access,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr,
                    creation_disposition,
                    FILE_ATTRIBUTE_NORMAL,
                    nullptr);

    if (fd == INVALID_HANDLE_VALUE) {
        resolver->logger->error(
            "Cannot open file {} : {}", filename,
            GetLastErrorMessage());

        return IO_ERROR;
    }

    LARGE_INTEGER tmp;

    // Now deduce the size of the stream.
    if (GetFileSizeEx(fd, &tmp)) {
        size = tmp.QuadPart;
    } else {
        // The file may be a raw device so we need to issue an ioctl to see how
        // large it is.
        GET_LENGTH_INFORMATION lpOutBuffer;
        DWORD lpBytesReturned;
        if (DeviceIoControl(
                    fd,                // handle to device
                    IOCTL_DISK_GET_LENGTH_INFO,    // dwIoControlCode
                    nullptr,                          // lpInBuffer
                    0,                             // nInBufferSize
                    &lpOutBuffer,                     // output buffer
                    sizeof(GET_LENGTH_INFORMATION),
                    (LPDWORD) &lpBytesReturned,    // number of bytes returned
                    nullptr)) {
            size = lpOutBuffer.Length.QuadPart;
        } else {
            // We dont know the size - seek relative to the end will fail now.
            size = -1;
            properties.sizeable = false;
        }
    }

    return STATUS_OK;
}

std::string FileBackedObject::Read(size_t length) {
    std::string result(length, '\0');
    if (ReadBuffer(&result[0], &length) != STATUS_OK) {
      return "";
    }
    result.resize(length);
    return result;
}

AFF4Status FileBackedObject::ReadBuffer(char* data, size_t *length) {
    DWORD buf_length = (DWORD)*length;

    if (properties.seekable) {
        LARGE_INTEGER tmp;
        tmp.QuadPart = readptr;
        if (!SetFilePointerEx(fd, tmp, &tmp, FILE_BEGIN)) {
            resolver->logger->info("Failed to seek: {}", GetLastErrorMessage());
            return IO_ERROR;
        }
    }

    if (!ReadFile(fd, data, buf_length, &buf_length, nullptr)) {
        resolver->logger->warn("Reading failed at {:x}: {}", readptr,
                                GetLastErrorMessage());

        return IO_ERROR;
    }

    *length = buf_length;
    readptr += *length;

    return STATUS_OK;
}

AFF4Status FileBackedObject::Write(const char* data, size_t length) {
    // Dont even try to write on files we are not allowed to write on.
    if (!properties.writable) {
        return IO_ERROR;
    }

    if (properties.seekable) {
        LARGE_INTEGER tmp;
        tmp.QuadPart = readptr;
        SetFilePointerEx(fd, tmp, &tmp, FILE_BEGIN);
    }

    DWORD tmp = length;
    while (!WriteFile(fd, data, tmp, &tmp, nullptr)) {
        resolver->logger->warn("Unable to write to disk. Is it full? "
                  "Please try to free space to continue.");
        Sleep(1000);
    }

    readptr += tmp;
    if (size >= 0 && readptr > size) {
        size = readptr;
    }

    return STATUS_OK;
}

AFF4Status FileBackedObject::Truncate() {
    if (!properties.seekable) {
        return IO_ERROR;
    }

    LARGE_INTEGER tmp;
    tmp.QuadPart = 0;

    SetFilePointerEx(fd, tmp, &tmp, FILE_BEGIN);
    if (SetEndOfFile(fd) == 0) {
        return IO_ERROR;
    }

    return STATUS_OK;
}

FileBackedObject::~FileBackedObject() {
    CloseHandle(fd);
}

// On other systems the posix open() API is used.
#else

AFF4Status FileBackedObject::LoadFromURN() {
    int flags = O_RDONLY | O_BINARY;

    XSDString mode("read");

    // The Resolver might have the correct filename for this URN.
    filename = _GetFilename(resolver, urn);
    if (filename.size() == 0) {
        return INVALID_INPUT;
    }

    std::vector<std::string> directory_components = split(filename, PATH_SEP);
    directory_components.pop_back();

    // Attribute is optional so if it is not there we just go with false.
    resolver->Get(urn, AFF4_STREAM_WRITE_MODE, mode);

    if (mode == "truncate") {
        flags |= O_CREAT | O_TRUNC | O_RDWR;

        // Next call will append.
        resolver->Set(urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
        properties.writable = true;

        // Only create directories if we are allowed to.
        AFF4Status res = _CreateIntermediateDirectories(resolver, directory_components);
        if (res != STATUS_OK) {
            return res;
        }

    } else if (mode == "append") {
        flags |= O_CREAT | O_RDWR;
        properties.writable = true;

        // Only create directories if we are allowed to.
        AFF4Status res = _CreateIntermediateDirectories(resolver, directory_components);
        if (res != STATUS_OK) {
            return res;
        }
    }

    resolver->logger->debug("Opening file {}", filename);

    fd = open(filename.c_str(), flags,
              S_IRWXU | S_IRWXG | S_IRWXO);

    if (fd < 0) {
        resolver->logger->error("Cannot open file {}: {}", filename,
                                GetLastErrorMessage());
        return IO_ERROR;
    }

    // If this fails we dont know the size - this can happen e.g. with devices. In
    // this case seeks relative to the end will fail.
    size = lseek(fd, 0, SEEK_END);
    if (size < 0) {
        properties.sizeable = false;
    }

    // Detect if the file is seekable (e.g. a pipe).
    if (lseek(fd, 0, SEEK_CUR) < 0) {
        properties.seekable = false;
    }

    return STATUS_OK;
}

std::string FileBackedObject::Read(size_t length) {
    std::unique_ptr<char[]> result(new char[length]);
    int res;

    lseek(fd, readptr, SEEK_SET);
    res = read(fd, result.get(), length);
    if (res < 0) {
        return "";
    }

    readptr += res;

    return std::string(result.get(), res);
}

AFF4Status FileBackedObject::ReadBuffer(char* data, size_t *length) {
    if (!properties.writable) {
        return IO_ERROR;
    }

    lseek(fd, readptr, SEEK_SET);
    int res = read(fd, data, *length);
    if (res >= 0) {
        readptr += res;
        *length = res;
    } else {
        return IO_ERROR;
    }

    return STATUS_OK;
}

AFF4Status FileBackedObject::Write(const char* data, size_t length) {
    if (!properties.writable) {
        return IO_ERROR;
    }

    // Since all file operations are synchronous this object cannot be dirty.
    if (properties.seekable) {
        lseek(fd, readptr, SEEK_SET);
    }

    int res = write(fd, data, length);
    if (res >= 0) {
        readptr += res;
    } else {
        return IO_ERROR;
    }

    if (readptr > size) {
        size = readptr;
    }

    return STATUS_OK;
}

AFF4Status FileBackedObject::Truncate() {
    if (ftruncate(fd, 0) != 0) {
        return IO_ERROR;
    }

    RETURN_IF_ERROR(Seek(0, SEEK_SET));
    size = 0;

    return STATUS_OK;
}


FileBackedObject::~FileBackedObject() {
    if (fd >= 0) {
        close(fd);
    }
}

#endif


// For file:// URN schemes we need to instantiate different objects, depending
// on a stat() of the target. So we register a specialized factory function.
class AFF4FileRegistrer {
  public:
    explicit AFF4FileRegistrer(std::string name) {
        GetAFF4ClassFactory()->RegisterFactoryFunction(
        name, [this](DataStore *d, const URN *urn) -> AFF4Object * {
            return GetObject(d, urn);
        });
    }

    AFF4Object* GetObject(DataStore* resolver, const URN* urn) {
        if (AFF4Directory::IsDirectory(*urn)) {
            return new AFF4Directory(resolver, *urn);
        }
        XSDString mode;

        return new FileBackedObject(resolver);
    }
};

// The FileBackedObject will be invoked for file:// style urns.
AFF4FileRegistrer file1("file");
AFF4Registrar<FileBackedObject> file2(AFF4_FILE_TYPE);


AFF4Status AFF4BuiltInStreams::LoadFromURN() {
    auto type = urn.Domain();

    // Right now we only support writing to stdout.
    if (type == "stdout") {
        properties.seekable = false;
        properties.writable = true;
        properties.sizeable = false;

#ifdef _WIN32
        // On windows stdout is set to text mode, we need to force it
        // to binary mode or else it will corrupt the output (issue
        // #31)
        fd = _fileno( stdout );
        _setmode (fd, _O_BINARY);
#else
        fd = STDOUT_FILENO;
#endif

        return STATUS_OK;
    }

    resolver->logger->error("Unsupported builtin stream {}", urn);
    return IO_ERROR;
}

AFF4Status AFF4BuiltInStreams::Write(const char* data, size_t length) {
    int res = write(fd, data, length);
    if (res >= 0) {
        readptr += res;
    } else {
        return IO_ERROR;
    }

    if (readptr > size) {
        size = readptr;
    }

    return STATUS_OK;
}

AFF4Status AFF4BuiltInStreams::Truncate() {
    return IO_ERROR;
}


AFF4Status AFF4BuiltInStreams::Seek(aff4_off_t offset, int whence) {
    if (whence == SEEK_END && offset == 0) {
        return STATUS_OK;
    }

    if (whence == SEEK_SET && offset == 0) {
        return STATUS_OK;
    }

    return IO_ERROR;
}

AFF4Registrar<AFF4BuiltInStreams> builtin("builtin");


void aff4_file_init() {}

} // namespace aff4
