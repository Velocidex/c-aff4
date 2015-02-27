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
#include <unordered_set>
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
  ssize_t size;             // How many bytes are used in the stream?

 public:
  AFF4Stream(DataStore *result): AFF4Object(result), readptr(0), size(0) {};

  // Convenience methods.
  int Write(const unique_ptr<string> &data);
  int Write(const string &data);
  int Write(const char data[]);
  int sprintf(string fmt, ...);

  // Read a null terminated string.
  string ReadCString(size_t length);
  int ReadIntoBuffer(void *buffer, size_t length);

  // Copies length bytes from this stream to the output stream.
  AFF4Status CopyToStream(AFF4Stream &output, size_t length,
                          size_t buffer_size=10*1024*1024);

  // The following should be overriden by derived classes.
  virtual void Seek(size_t offset, int whence);
  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Tell();
  virtual size_t Size();

  /**
   * Streams are always reset to their begining when returned from the cache.
   *
   *
   * @return
   */
  virtual AFF4Status Prepare() {
    Seek(0, SEEK_SET);
    return STATUS_OK;
  };

  /**
   * Streams can be truncated. This means the older stream data will be removed
   * and the object is returned to its initial state.
   *
   *
   * @return STATUS_OK if we were able to truncate the stream successfully.
   */
  virtual AFF4Status Truncate() {
    return NOT_IMPLEMENTED;
  };
};

class StringIO: public AFF4Stream {
 public:
  string buffer;
  StringIO(DataStore *resolver): AFF4Stream(resolver) {};
  StringIO(): AFF4Stream(NULL) {};
  StringIO(string data): AFF4Stream(NULL), buffer(data) {};

  // Convenience constructors.
  static unique_ptr<StringIO> NewStringIO() {
    unique_ptr<StringIO> result(new StringIO());

    return result;
  };

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Size();

  virtual AFF4Status Truncate();
  using AFF4Stream::Write;
};


class FileBackedObject: public AFF4Stream {
 protected:
  int fd;

 public:
  FileBackedObject(DataStore *resolver): AFF4Stream(resolver) {};

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Size();

  /**
   * Load the file from a file:/ URN.
   *
   *
   * @return STATUS_OK if we were able to open it successfully.
   */
  virtual AFF4Status LoadFromURN();

  virtual AFF4Status Truncate();
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
  // This is a list of URNs contained in this volume.
  std::unordered_set<string> children;

  AFF4Volume(DataStore *resolver): AFF4Object(resolver) {};
  virtual AFF4ScopedPtr<AFF4Stream> CreateMember(URN child) = 0;
};


#endif      /* !AFF4_IO_H_ */
