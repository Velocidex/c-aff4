#include "aff4.h"
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>


#define O_BINARY 0

void MemoryDataStore::Set(URN urn, URN attribute, const RDFValue &value) {
  store[urn.value][attribute.value] = value.Serialize();
};


DataStoreObject MemoryDataStore::Get(URN urn, URN attribute) {
  return store[urn.value][attribute.value];
};

bool AFF4Object::finish() {
  return true;
};


void AFF4Stream::Seek(int offset, int whence) {
  if (whence == 0) {
    readptr = offset;
  } else if(whence == 1) {
    readptr += offset;
  } else if(whence == 2) {
    readptr = offset + Size();
  }
};

string AFF4Stream::Read(size_t length) {
  return "";
};

int AFF4Stream::Write(const string &data) {
  return Write(data.c_str(), data.size());
};

int AFF4Stream::Write(const unique_ptr<string> &data) {
  return Write(data->c_str(), data->length());
};

int AFF4Stream::Write(const char data[]) {
  return Write(data, strlen(data));
};

int AFF4Stream::Write(const char *data, int length) {
  return 0;
}

size_t AFF4Stream::Tell() {
  return readptr;
}

int AFF4Stream::Size() {
  return 0;
}

int AFF4Stream::sprintf(string fmt, ...) {
  va_list ap;
  int size = fmt.size() * 2 + 50;

  while (1) {
    unique_ptr<char> buffer_(new char[size + 1]);
    char *buffer = buffer_.get();

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

  buffer.replace(readptr, length, data);
  readptr += length;

  return length;
};

string StringIO::Read(size_t length) {
  string result = buffer.substr(readptr, length);
  readptr += result.size();

  return result;
};

int StringIO::Size() {
  return buffer.size();
}

unique_ptr<FileBackedObject> FileBackedObject::NewFileBackedObject(
    string filename, string mode) {

  unique_ptr<FileBackedObject> result(new FileBackedObject());
  FileBackedObject *self = result.get();

  int flags = O_RDONLY | O_BINARY;

  if(mode == "w") {
    flags |= O_CREAT | O_TRUNC | O_RDWR;

  } else if (mode == "rw") {
    flags |= O_CREAT | O_RDWR;
  };

  self->fd = open(filename.c_str(), flags, S_IRWXU | S_IRWXG | S_IRWXO);
  if(self->fd < 0){
    return NULL;
  };

  return result;
};

string FileBackedObject::Read(size_t length) {
  unique_ptr<char[]> data(new char[length]);
  int res;

  if (!data) {
    return NULL;
  };

  lseek(fd, readptr, SEEK_SET);
  res = read(fd, data.get(), length);
  if (res < 0) {
    return "";
  };

  readptr += res;

  return string(data.get(), res);
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

int FileBackedObject::Size() {
  off_t result = lseek(fd, 0, SEEK_END);

  return result;
};
