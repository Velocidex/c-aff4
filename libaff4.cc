#include "aff4.h"
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>


#define O_BINARY 0


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

  if(readptr < 0) {
    readptr = 0;
  };
};

bstring AFF4Stream::Read(size_t length) {
  return bstring();
};

string AFF4Stream::ReadCString(size_t length) {
  bstring result = Read(length);

  // Make a little bit more room for the null char.
  result.resize(result.size() + 1);
  result[result.size()] = 0;

  return string(result.data(),
                std::min(result.size(), strlen(result.data())));
};


int AFF4Stream::Write(const bstring &data) {
  return Write(data.data(), data.size());
};

int AFF4Stream::Write(const string &data) {
  return Write(data.c_str(), data.size());
};

int AFF4Stream::Write(const unique_ptr<bstring> &data) {
  return Write(data->data(), data->size());
};

int AFF4Stream::Write(const char data[]) {
  return Write(data, strlen(data));
};

int AFF4Stream::Write(const char *data, int length) {
  return 0;
}

int AFF4Stream::ReadIntoBuffer(void *buffer, size_t length) {
  bstring result = Read(length);

  memcpy(buffer, result.data(), result.size());

  return result.size();
};

size_t AFF4Stream::Tell() {
  return readptr;
}

size_t AFF4Stream::Size() {
  return 0;
}

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

  // Ensure there is enough room in the vector.
  if (buffer.size() < Tell() + length) {
    buffer.resize(Tell() + length);
  };

  memcpy(&buffer[Tell()], data, length);
  readptr += length;

  return length;
};

bstring StringIO::Read(size_t length) {
  int to_read = std::min(length, buffer.size() - readptr);

  bstring result(to_read);

  memcpy(result.data(), &buffer.at(readptr), to_read);

  readptr += to_read;

  return result;
};

size_t StringIO::Size() {
  return buffer.size();
}

unique_ptr<FileBackedObject> FileBackedObject::NewFileBackedObject(
    string filename, string mode) {

  FileBackedObject *self = new FileBackedObject();
  unique_ptr<FileBackedObject> result(self);
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

bstring FileBackedObject::Read(size_t length) {
  char data[length];
  int res;

  lseek(fd, readptr, SEEK_SET);
  res = read(fd, data, length);
  if (res < 0) {
    return bstring();
  };

  readptr += res;

  bstring result(res);
  memcpy(result.data(), data, res);

  return result;
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
