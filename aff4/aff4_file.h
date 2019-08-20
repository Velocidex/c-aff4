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

#ifndef  SRC_AFF4_FILE_H_
#define  SRC_AFF4_FILE_H_

#include "aff4/config.h"

#include "aff4/aff4_io.h"
#include "aff4/data_store.h"
#include "aff4/aff4_utils.h"
#include "aff4/rdf.h"

#include <cstring>
#include <unordered_map>

namespace aff4 {

/*
  Files are AFF4 stream objects backed by a file on the filename.

  In the AFF4 data model, files are handled using their URN (using the file://
  scheme).

  Note that the file:// URN is independent of the actual filename used to store
  the data on the filesystem. Most filesystems have limitations of the filenames
  that are representable on them. For example, Windows filesystems forbid
  certain characters from appearing in filenames.

  An implementation is free to provide a different file name for each file://
  urn using the AFF4_FILE_NAME attribute.
 */
class FileBackedObject: public AFF4Stream {
  public:
    // The filename for this object.
    std::string filename;

    explicit FileBackedObject(DataStore* resolver): AFF4Stream(resolver), fd(0){}
    virtual ~FileBackedObject();



    AFF4Status ReadBuffer(char* data, size_t *length) override;
    AFF4Status Write(const char* data, size_t length) override;

    AFF4Status Truncate() override;

    // We provide access to the underlying file handle so callers can do other
    // things with the stream (i.e. ioctl on raw devices).
#if defined(_WIN32)
    HANDLE fd;
#else
    int fd;
#endif

    size_t cache_block_size;
    size_t cache_block_limit;

  private:
    // Read buffer, bypassing cache
    AFF4Status _ReadBuffer(char* data, size_t *length);

    std::unordered_map<size_t, std::string> read_cache{};
};


AFF4Status NewFileBackedObject(
     DataStore *resolver,
     std::string filename,
     std::string mode,
     AFF4Flusher<FileBackedObject> &result
);

// A generic interface.
AFF4Status NewFileBackedObject(
     DataStore *resolver,
     std::string filename,
     std::string mode,
     AFF4Flusher<AFF4Stream> &result
);


/*
  A stream which just returns the same char over and over.
 */
class AFF4ConstantStream: public AFF4Stream {
    char constant = 0;
  public:
    explicit AFF4ConstantStream(DataStore* resolver): AFF4Stream(resolver) {}

    aff4_off_t Size() const override {
        return -1;
    }

    std::string Read(size_t length) override {
        return std::string(length, constant);
    }

    AFF4Status ReadBuffer(char* data, size_t* length) override {
        std::memset(data, constant, *length);
        return STATUS_OK;
    }
};

class AFF4Stdout : public AFF4Stream {
 public:
    static AFF4Status NewAFF4Stdout(
        DataStore *resolver,
        AFF4Flusher<AFF4Stream> &result);

    explicit AFF4Stdout(DataStore *resolver): AFF4Stream(resolver) {}
    AFF4Status Write(const char* data, size_t length) override;
    AFF4Status Seek(aff4_off_t offset, int whence) override;
    AFF4Status Truncate() override;

 private:
    int fd;
};

AFF4Status _CreateIntermediateDirectories(DataStore *resolver,
                                          std::string dir_name);

AFF4Status CreateIntermediateDirectories(DataStore *resolver,
                                         std::vector<std::string> components);

} // namespace aff4

#endif  // SRC_AFF4_FILE_H_
