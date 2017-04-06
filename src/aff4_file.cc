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

#include "aff4_base.h"
#include "aff4_file.h"
#include "aff4_directory.h"
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <glog/logging.h>


#ifndef O_BINARY
#define O_BINARY 0
#endif


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
AFF4Status _CreateIntermediateDirectories(std::vector<std::string> components) {
    std::string path = PATH_SEP_STR;

#ifdef _WIN32
    // On windows we do not want a leading \ (e.g. C:\windows not \C:\Windows)
    path = "";
#endif

    for (auto component : components) {
        path = path + component + PATH_SEP_STR;
        LOG(INFO) << "Creating intermediate directories " << path;

        if (AFF4Directory::IsDirectory(path)) {
            continue;
        }

        // Directory does not exist - Try to make it.
        if (AFF4Directory::MkDir(path) == STATUS_OK) {
            continue;
        }

        LOG(ERROR) << "Unable to create intermediate directory: " <<
                   GetLastErrorMessage();
        return IO_ERROR;
    }

    return STATUS_OK;
}

AFF4Status _CreateIntermediateDirectories(std::string dir_name) {
    return _CreateIntermediateDirectories(split(dir_name, PATH_SEP));
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

    LOG(INFO) << "Opening file " << filename;

    vector<string> directory_components = split(filename, PATH_SEP);
    directory_components.pop_back();

    // Attribute is optional so if it is not there we just go with false.
    resolver->Get(urn, AFF4_STREAM_WRITE_MODE, mode);

    if (mode == "truncate") {
        creation_disposition = CREATE_ALWAYS;
        desired_access |= GENERIC_WRITE;

        // Next call will append.
        resolver->Set(urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
        properties.writable = true;

        // Only create directories if we are allowed to.
        AFF4Status res = _CreateIntermediateDirectories(directory_components);
        if (res != STATUS_OK) {
            return res;
        }

    } else if (mode == "append") {
        creation_disposition = OPEN_ALWAYS;
        desired_access |= GENERIC_WRITE;
        properties.writable = true;

        // Only create directories if we are allowed to.
        AFF4Status res = _CreateIntermediateDirectories(directory_components);
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
        LOG(ERROR) << "Can not open file " << filename << " :" <<
                   GetLastErrorMessage();

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

string FileBackedObject::Read(size_t length) {
    DWORD buffer_size = length;
    unique_ptr<char[]> result(new char[length]);

    if (properties.seekable) {
        LARGE_INTEGER tmp;
        tmp.QuadPart = readptr;
        if (!SetFilePointerEx(fd, tmp, &tmp, FILE_BEGIN)) {
            LOG(INFO) << "Failed to seek:" << GetLastErrorMessage();
        }
    }

    if (!ReadFile(fd, result.get(), buffer_size, &buffer_size, nullptr)) {
        LOG(INFO) << "Reading failed " << readptr << ": " <<
                  GetLastErrorMessage();

        return "";
    }

    readptr += buffer_size;

    return string(result.get(), buffer_size);
}


int FileBackedObject::Write(const char* data, int length) {
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
        std::cout << "Unable to write to disk. Is it full? "
                  "Please try to free space to continue.\r";
        Sleep(1000);
    }

    readptr += tmp;
    if (size >= 0 && readptr > size) {
        size = readptr;
    }

    return tmp;
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
        AFF4Status res = _CreateIntermediateDirectories(directory_components);
        if (res != STATUS_OK) {
            return res;
        }

    } else if (mode == "append") {
        flags |= O_CREAT | O_RDWR;
        properties.writable = true;

        // Only create directories if we are allowed to.
        AFF4Status res = _CreateIntermediateDirectories(directory_components);
        if (res != STATUS_OK) {
            return res;
        }
    }

    LOG(INFO) << "Opening file " << filename;

    fd = open(filename.c_str(), flags,
              S_IRWXU | S_IRWXG | S_IRWXO);

    if (fd < 0) {
        LOG(ERROR) << "Can not open file " << filename << " :" <<
                   GetLastErrorMessage();
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

int FileBackedObject::Write(const char* data, int length) {
    if (!properties.writable) {
        return IO_ERROR;
    }

    // Since all file operations are synchronous this object can not be dirty.
    lseek(fd, readptr, SEEK_SET);
    int res = write(fd, data, length);
    if (res > 0) {
        readptr += res;
    }

    if (size >= 0 && readptr > size) {
        size = readptr;
    }

    return res;
}

AFF4Status FileBackedObject::Truncate() {
    if (ftruncate(fd, 0) != 0) {
        return IO_ERROR;
    }

    Seek(0, SEEK_SET);
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

void aff4_file_init() {}
