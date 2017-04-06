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

#include "config.h"

#include "aff4_io.h"
#include "data_store.h"
#include "aff4_utils.h"
#include "rdf.h"

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

    virtual std::string Read(size_t length);
    virtual int Write(const char* data, int length);

    /**
     * Load the file from a file:/ URN.
     *
     *
     * @return STATUS_OK if we were able to open it successfully.
     */
    virtual AFF4Status LoadFromURN();

    virtual AFF4Status Truncate();

    // We provide access to the underlying file handle so callers can do other
    // things with the stream (i.e. ioctl on raw devices).
#if defined(_WIN32)
    HANDLE fd;
#else
    int fd;
#endif
};



/*
  A stream which just returns the same char over.
 */
class AFF4ConstantStream: public AFF4Stream {
    char constant = 0;
  public:
    explicit AFF4ConstantStream(DataStore* resolver): AFF4Stream(resolver) {}

    virtual aff4_off_t Size() {
        return -1;
    }
    virtual AFF4Status LoadFromURN() {
        size = Size();
        return STATUS_OK;
    }
    virtual std::string Read(size_t length) {
        return std::string(length, constant);
    }
};

extern void aff4_file_init();

#endif  // SRC_AFF4_FILE_H_
