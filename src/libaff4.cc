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

#include "aff4_utils.h"
#include "aff4_errors.h"
#include "aff4_io.h"
#include "libaff4.h"
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <cstring>
#include <iostream>
#include <iomanip>

#ifndef O_BINARY
#define O_BINARY 0
#endif


#if defined(HAVE_LIBUUID)
#include <uuid/uuid.h>

/**
 * By default empty objects receive a unique URN.
 *
 * @param resolver
 */
AFF4Object::AFF4Object(DataStore *resolver): resolver(resolver) {
  uuid_t uuid;
  vector<char> buffer(100);

  uuid_generate(uuid);
  uuid_unparse(uuid, buffer.data());
  urn.Set("aff4://" + string(buffer.data()));
};

// On windows we use native GUID generation.
#elif defined(_WIN32)
#include <objbase.h>

/**
 * By default empty objects receive a unique URN.
 *
 * @param resolver
 */
AFF4Object::AFF4Object(DataStore *resolver): resolver(resolver) {
  GUID newId;
  UuidCreate(&newId);

  unsigned char *buffer;
  UuidToString(&newId, &buffer);

  urn.Set("aff4://" + string((char *)buffer));
};

#endif

AFF4Status AFF4Object::Flush() {
  // Flushing makes the object no longer dirty.
  _dirty = false;

  return STATUS_OK;
};


void AFF4Object::Return() {
  resolver->Return(this);
};

AFF4Status AFF4Stream::Seek(off_t offset, int whence) {
  off_t new_offset = readptr;

  if (whence == 0) {
    new_offset = offset;

  } else if(whence == 1) {
    new_offset += offset;

  } else if(whence == 2) {
    // We can not seek relative to size for streams which are non sizeable.
    if(!properties.sizeable) {
      return IO_ERROR;
    };

    new_offset = offset + Size();
  }

  if(new_offset < 0) {
    new_offset = 0;
  };

  // For non-seekable streams its ok to seek to the current position.
  if(!properties.seekable && new_offset!=offset)
    return IO_ERROR;

  readptr = new_offset;
  return STATUS_OK;
};

string AFF4Stream::Read(size_t length) {
  return "";
};

int AFF4Stream::Write(const string &data) {
  return Write(data.c_str(), data.size());
};

int AFF4Stream::Write(const char data[]) {
  return Write(data, strlen(data));
};

int AFF4Stream::Write(const char *data, int length) {
  return 0;
}

int AFF4Stream::ReadIntoBuffer(void *buffer, size_t length) {
  string result = Read(length);

  memcpy(buffer, result.data(), result.size());

  return result.size();
};

off_t AFF4Stream::Tell() {
  return readptr;
}

off_t AFF4Stream::Size() {
  return size;
}

AFF4Status AFF4Stream::CopyToStream(AFF4Stream &output, size_t length,
                                    size_t buffer_size) {
  uint64_t last_time = 0;
  off_t last_offset = 0;
  off_t start = Tell();
  size_t length_remaining = length;

  while(length_remaining > 0) {
    size_t to_read = std::min(buffer_size, length_remaining);
    string data = Read(to_read);
    if(data.size() == 0) {
      break;
    };

    output.Write(data);
    length_remaining -= data.size();

    uint64_t now = time_from_epoch();

    if (now > last_time + 1000000/4) {
      // Rate in MB/s.
      off_t rate = (readptr - last_offset) / (now - last_time) * 1000000 /
          1024/1024 ;

      std::cout << " Reading 0x" << std::hex << readptr << "  " <<
          std::dec << (readptr - start)/1024/1024 << "MiB / " <<
          length/1024/1024 << "MiB " << rate << "MiB/s\r";
      std::cout.flush();
      last_time = now;
      last_offset = readptr;
    };
  };

  return STATUS_OK;
};


string aff4_sprintf(string fmt, ...) {
  va_list ap;
  int size = fmt.size() * 2 + 50;

  while (1) {
    char buffer[size + 1];

    // Null terminate the buffer (important on MSVC which does not always
    // terminate).
    buffer[size] = 0;

    va_start(ap, fmt);
    int n = vsnprintf(buffer, size, fmt.c_str(), ap);
    va_end(ap);

    if (n > -1 && n < size) {  // Everything worked
      return string(buffer, n);
    };

    if (n > -1)  // Needed size returned
      size = n + 1;   // For null char
    else
      size *= 2;      // Guess at a larger size (OS specific)
  }
};


int AFF4Stream::sprintf(string fmt, ...) {
  va_list ap;
  int size = fmt.size() * 2 + 50;

  while (1) {
    char buffer[size + 1];

    // Null terminate the buffer (important on MSVC which does not always
    // terminate).
    buffer[size] = 0;

    va_start(ap, fmt);
    int n = vsnprintf(buffer, size, fmt.c_str(), ap);
    va_end(ap);

    if (n > -1 && n < size) {  // Everything worked
      Write(buffer, n);
      return n;
    };

    if (n > -1)  // Needed size returned
      size = n + 1;   // For null char
    else
      size *= 2;      // Guess at a larger size (OS specific)
  }
};

int StringIO::Write(const char *data, int length) {
  MarkDirty();

  buffer.replace(readptr, length, data, length);
  readptr += length;

  return length;
};

string StringIO::Read(size_t length) {
  string result = buffer.substr(readptr, length);
  readptr += result.size();

  return result;
};

off_t StringIO::Size() {
  return buffer.size();
}

AFF4Status StringIO::Truncate() {
  buffer = "";
  readptr = 0;
  return STATUS_OK;
};

/***************************************************************
FileBackedObject implementation.
****************************************************************/

// Windows files are read through the CreateFile() API so that devices can be
// read.
#if defined(_WIN32)
AFF4Status FileBackedObject::LoadFromURN() {
  DWORD desired_access = GENERIC_READ;
  DWORD creation_disposition = OPEN_EXISTING;

  XSDString mode("read");

  // Only file:// URNs are supported.
  if (urn.Scheme() != "file") {
    return INVALID_INPUT;
  };

  // Attribute is optional so if it is not there we just go with false.
  resolver->Get(urn, AFF4_STREAM_WRITE_MODE, mode);

  if(mode == "truncate") {
    creation_disposition = CREATE_ALWAYS;
    desired_access |= GENERIC_WRITE;

    // Next call will append.
    resolver->Set(urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    properties.writable = true;

  } else if (mode == "append") {
    creation_disposition = OPEN_ALWAYS;
    desired_access |= GENERIC_WRITE;
    properties.writable = true;
  };

  string filename = urn.ToFilename();
  LOG(INFO) << "Opening file " << filename;

  fd = CreateFile(filename.c_str(),
                  desired_access,
                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                  NULL,
                  creation_disposition,
                  FILE_ATTRIBUTE_NORMAL,
                  NULL);

  if(fd == INVALID_HANDLE_VALUE){
    LOG(ERROR) << "Can not open file " << filename << " :" <<
        GetLastErrorMessage();

    return IO_ERROR;
  };

  LARGE_INTEGER tmp;

  // Now deduce the size of the stream.
  if(GetFileSizeEx(fd, &tmp)) {
    size = tmp.QuadPart;
  } else {
    // The file may be a raw device so we need to issue an ioctl to see how
    // large it is.
    GET_LENGTH_INFORMATION lpOutBuffer;
    DWORD lpBytesReturned;
    if(DeviceIoControl(
           fd,                // handle to device
           IOCTL_DISK_GET_LENGTH_INFO,    // dwIoControlCode
           NULL,                          // lpInBuffer
           0,                             // nInBufferSize
           &lpOutBuffer,                     // output buffer
           sizeof(GET_LENGTH_INFORMATION),// size of output buffer
           (LPDWORD) &lpBytesReturned,    // number of bytes returned
           NULL                           // OVERLAPPED structure
                       )) {
      size = lpOutBuffer.Length.QuadPart;
    } else {
      // We dont know the size - seek relative to the end will fail now.
      size = -1;
      properties.sizeable = false;
    };
  };

  return STATUS_OK;
};

string FileBackedObject::Read(size_t length) {
  DWORD buffer_size = length;
  string result(buffer_size, 0);

  if(properties.seekable) {
    LARGE_INTEGER tmp;
    tmp.QuadPart = readptr;
    if(!SetFilePointerEx(fd, tmp, &tmp, FILE_BEGIN)) {
      LOG(INFO) << "Failed to seek:" << GetLastErrorMessage();
    };
  };

  if(!ReadFile(fd, &result[0], buffer_size, &buffer_size, NULL)) {
    LOG(INFO) << "Reading failed " << readptr << ": " <<
        GetLastErrorMessage();

    return "";
  };

  readptr += buffer_size;
  result.resize(buffer_size);

  return result;
};

int FileBackedObject::Write(const char *data, int length) {
  // Dont even try to write on files we are not allowed to write on.
  if(!properties.writable)
    return IO_ERROR;

  if(properties.seekable) {
    LARGE_INTEGER tmp;
    tmp.QuadPart = readptr;
    SetFilePointerEx(fd, tmp, &tmp, FILE_BEGIN);
  };

  DWORD tmp = length;
  if (!WriteFile(fd, data, tmp, &tmp, NULL)) {
    return IO_ERROR;
  };

  readptr += tmp;
  if(size >= 0 && readptr > size)
    size = readptr;

  return tmp;
};

AFF4Status FileBackedObject::Truncate() {
  if(!properties.seekable)
    return IO_ERROR;

  LARGE_INTEGER tmp;
  tmp.QuadPart = 0;

  SetFilePointerEx(fd, tmp, &tmp, FILE_BEGIN);
  if(SetEndOfFile(fd) == 0)
    return IO_ERROR;

  return STATUS_OK;
};

FileBackedObject::~FileBackedObject() {
  CloseHandle(fd);
};

// On other systems the posix open() API is used.
#else

AFF4Status FileBackedObject::LoadFromURN() {
  int flags = O_RDONLY | O_BINARY;

  XSDString mode("read");

  // Only file:// URNs are supported.
  if (urn.Scheme() != "file") {
    return INVALID_INPUT;
  };

  // Attribute is optional so if it is not there we just go with false.
  resolver->Get(urn, AFF4_STREAM_WRITE_MODE, mode);

  if(mode == "truncate") {
    flags |= O_CREAT | O_TRUNC | O_RDWR;

    // Next call will append.
    resolver->Set(urn, AFF4_STREAM_WRITE_MODE, new XSDString("append"));
    properties.writable = true;

  } else if (mode == "append") {
    flags |= O_CREAT | O_RDWR;
    properties.writable = true;
  };

  string filename = urn.ToFilename();
  LOG(INFO) << "Opening file " << filename;

  fd = open(filename.c_str(), flags,
            S_IRWXU | S_IRWXG | S_IRWXO);

  if(fd < 0){
    LOG(ERROR) << "Can not open file " << filename << " :" <<
        GetLastErrorMessage();
    return IO_ERROR;
  };

  // If this fails we dont know the size - this can happen e.g. with devices. In
  // this case seeks relative to the end will fail.
  size = lseek(fd, 0, SEEK_END);
  if (size < 0) {
    properties.sizeable = false;
  };

  // Detect if the file is seekable (e.g. a pipe).
  if(lseek(fd, 0, SEEK_CUR)<0) {
    properties.seekable = false;
  };

  return STATUS_OK;
};

string FileBackedObject::Read(size_t length) {
  string result(length, 0);
  int res;

  lseek(fd, readptr, SEEK_SET);
  res = read(fd, &result[0], result.size());
  if (res < 0) {
    return "";
  };

  readptr += res;
  result.resize(res);

  return result;
};

int FileBackedObject::Write(const char *data, int length) {
  if(!properties.writable) {
    return IO_ERROR;
  };

  // Since all file operations are synchronous this object can not be dirty.
  lseek(fd, readptr, SEEK_SET);
  int res = write(fd, data, length);
  if (res > 0) {
    readptr += res;
  };

  if(size >= 0 && readptr > size)
    size = readptr;

  return res;
};

AFF4Status FileBackedObject::Truncate() {
  if(ftruncate(fd, 0) != 0)
    return IO_ERROR;

  Seek(0, SEEK_SET);
  size = 0;

  return STATUS_OK;
};

FileBackedObject::~FileBackedObject() {
  if(fd>=0)
    close(fd);
};

#endif


ClassFactory<AFF4Object> *GetAFF4ClassFactory() {
  static auto* factory = new ClassFactory<AFF4Object>();
  return factory;
}

// The FileBackedObject will be invoked for file:// style urns.
static AFF4Registrar<FileBackedObject> r1("file");

#ifdef _WIN32

string GetLastErrorMessage() {
  LPTSTR lpMsgBuf;
  DWORD dw = GetLastError();

  FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER |
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      dw,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPTSTR) &lpMsgBuf,
      0, NULL );

  return lpMsgBuf;
};

#else
string GetLastErrorMessage() {
  return std::strerror(errno);
};

#endif




extern "C" {

char *AFF4_version() {
  static char version[] = "libaff4 version " AFF4_VERSION;

  return version;
};

}

AFF4_IMAGE_COMPRESSION_ENUM CompressionMethodFromURN(URN method) {
  if(method.value == AFF4_IMAGE_COMPRESSION_ZLIB) {
    return AFF4_IMAGE_COMPRESSION_ENUM_ZLIB;
  } else if(method.value == AFF4_IMAGE_COMPRESSION_SNAPPY) {
    return AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY;
  } else if(method.value == AFF4_IMAGE_COMPRESSION_STORED) {
    return AFF4_IMAGE_COMPRESSION_ENUM_STORED;
  } else {
    return AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN;
  };
};

URN CompressionMethodToURN(AFF4_IMAGE_COMPRESSION_ENUM method) {
  switch(method) {
    case AFF4_IMAGE_COMPRESSION_ENUM_ZLIB:
      return AFF4_IMAGE_COMPRESSION_ZLIB;

    case AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY:
      return AFF4_IMAGE_COMPRESSION_SNAPPY;

    case AFF4_IMAGE_COMPRESSION_ENUM_STORED:
      return AFF4_IMAGE_COMPRESSION_STORED;

    default:
      return "";
  };
};
