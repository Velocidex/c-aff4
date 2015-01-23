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

#include "aff4_errors.h"
#include "libaff4.h"
#include <uuid/uuid.h>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>


#define O_BINARY 0

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


AFF4Status AFF4Object::Flush() {
  return STATUS_OK;
};


void AFF4Stream::Seek(int offset, int whence) {
  if (whence == 0) {
    readptr = offset;
  } else if(whence == 1) {
    readptr += offset;
  } else if(whence == 2) {
    readptr = offset + Size();
  }

  if(readptr < 0) {
    readptr = 0;
  };
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

size_t AFF4Stream::Tell() {
  return readptr;
}

size_t AFF4Stream::Size() {
  return size;
}


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
  _dirty = true;

  buffer.replace(readptr, length, data, length);
  readptr += length;

  return length;
};

string StringIO::Read(size_t length) {
  string result = buffer.substr(readptr, length);
  readptr += result.size();

  return result;
};

size_t StringIO::Size() {
  return buffer.size();
}

AFF4Status StringIO::Truncate() {
  buffer = "";
  readptr = 0;
  return STATUS_OK;
};


AFF4Status FileBackedObject::LoadFromURN() {
  int flags = O_RDONLY | O_BINARY;
  uri_components components = urn.Parse();
  string mode = "rw";

  // Only file:// URNs are supported.
  if (components.scheme != "file") {
    return INVALID_INPUT;
  };

  if(mode == "w") {
    flags |= O_CREAT | O_TRUNC | O_RDWR;

  } else if (mode == "rw") {
    flags |= O_CREAT | O_RDWR;
  };

  fd = open(components.path.c_str(), flags,
            S_IRWXU | S_IRWXG | S_IRWXO);

  if(fd < 0){
    return IO_ERROR;
  };

  return STATUS_OK;
};


string FileBackedObject::Read(size_t length) {
  char data[length];
  int res;

  lseek(fd, readptr, SEEK_SET);
  res = read(fd, data, length);
  if (res < 0) {
    return "";
  };

  readptr += res;

  return string(data, res);
};

int FileBackedObject::Write(const char *data, int length) {
  // Since all file operations are synchronous this object can not be dirty.
  _dirty = false;

  lseek(fd, readptr, SEEK_SET);
  int res = write(fd, data, length);
  if (res > 0) {
    readptr += res;
  };

  return res;
};

size_t FileBackedObject::Size() {
  off_t result = lseek(fd, 0, SEEK_END);

  return result;
};


AFF4Status FileBackedObject::Truncate() {
  if(ftruncate(fd, 0) != 0)
    return IO_ERROR;

  return STATUS_OK;
};

/**
 * A helper function to create an AFF4 image stream inside a new or existing
 * AFF4 Zip volume.
 *
 * @param output_file
 * @param stream_name
 * @param chunks_per_segment
 * @param max_volume_size
 * @param input_stream
 *
 * @return AFF4Status.
 */
AFF4Status aff4_image(char *output_file, char *stream_name,
                      unsigned int chunks_per_segment,
                      uint64_t max_volume_size,
                      AFF4Stream &input_stream) {

  return NOT_IMPLEMENTED;
};


ClassFactory<AFF4Object> *GetAFF4ClassFactory() {
  static auto* factory = new ClassFactory<AFF4Object>();
  return factory;
}

// The FileBackedObject will be invoked for file:// style urns.
static AFF4Registrar<FileBackedObject> r1("file");
