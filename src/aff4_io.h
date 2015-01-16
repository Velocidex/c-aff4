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

#ifndef     AFF4_IO_H_
#define     AFF4_IO_H_

#include <unordered_map>
#include <string>
#include <memory>
#include <fstream>
#include "aff4_base.h"
#include "data_store.h"
#include "aff4_utils.h"
#include "rdf.h"

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::ofstream;
using std::ifstream;



class AFF4Stream: public AFF4Object {
 protected:
  ssize_t readptr;
  int size;             // How many bytes are used in the stream?
  bool _dirty = false;  // Is this stream modified?

 public:
  string name = "AFF4Stream";

  AFF4Stream(): readptr(0), size(0) {};

  // Convenience methods.
  int Write(const unique_ptr<string> &data);
  int Write(const string &data);
  int Write(const char data[]);
  int sprintf(string fmt, ...);

  // Read a null terminated string.
  string ReadCString(size_t length);
  int ReadIntoBuffer(void *buffer, size_t length);

  // The following should be overriden by derived classes.
  virtual void Seek(int offset, int whence);
  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Tell();
  virtual size_t Size();
};

class StringIO: public AFF4Stream {
 public:
  string buffer;

  StringIO() {};
  StringIO(string data): buffer(data) {};

  // Convenience constructors.
  static unique_ptr<StringIO> NewStringIO() {
    unique_ptr<StringIO> result(new StringIO());

    return result;
  };

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Size();

  using AFF4Stream::Write;
};


class FileBackedObject: public AFF4Stream {
 protected:
  int fd;

 public:
  string name = "FileBackedObject";

  FileBackedObject() {};

  static unique_ptr<FileBackedObject> NewFileBackedObject(
      string filename, string mode);

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Size();

};

/**
   Volumes allow for other objects to be stored within them.

   This means that to create an object within a volume, one must create the
   volume first, then call CreateMember() to add the object to the volume. The
   new object will be flushed to the volume when it is destroyed.

   To open an existing object one needs to:

   1) Open the volume - this will populate the resolver with the information
      within the volume.

   2) Directly open the required URN. The AFF4 resolver will know which volume
      (or volumes) the required object lives in by itself.

   Note that when using a persistent resolver step 1 might be skipped if the
   location of the volume has not changed.
*/
class AFF4Volume: public AFF4Object {
 public:
  virtual unique_ptr<AFF4Stream> CreateMember(string filename) = 0;
};


#define DEBUG_OBJECT(fmt, ...) printf(fmt "\n", ## __VA_ARGS__)

#define CHECK(cond, fmt, ...) if(cond) {                \
    printf(fmt "\n", ## __VA_ARGS__);                   \
    exit(-1);                                           \
  }


#endif      /* !AFF4_IO_H_ */
