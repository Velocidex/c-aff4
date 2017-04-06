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

#ifndef     SRC_AFF4_IO_H_
#define     SRC_AFF4_IO_H_

#include "config.h"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <fstream>
#include <cstdio>
#include "aff4_base.h"
#include "data_store.h"
#include "aff4_utils.h"
#include "rdf.h"

//using std::string;
//using std::unique_ptr;
//using std::unordered_map;
//using std::ofstream;
//using std::ifstream;

// A constant for various buffers used by the AFF4 library.
#define AFF4_BUFF_SIZE (32 * 1024)


struct AFF4StreamProperties {
    // Set if the stream is non seekable (e.g. a pipe).
    bool seekable = true;

    // Do we know the size of this file?
    bool sizeable = true;

    // Can we write to this file.
    bool writable = false;
};


struct AFF4VolumeProperties {
    // Supports compression?
    bool supports_compression = true;

    // Can we write to this volume?
    bool writable = false;

    // Can file and directory names co-exist? (e.g. can we have a/b and a/b/c).
    bool files_are_directories = true;
};


class ProgressContext {
  public:
    // Maintained by the callback.
    uint64_t last_time = 0;
    aff4_off_t last_offset = 0;

    // The following are set in advance by users in order to get accurate progress
    // reports.

    // Start offset of this current range.
    aff4_off_t start = 0;

    // Total length for this operation.
    aff4_off_t length = 0;

    // This will be called periodically to report the progress. Note that readptr
    // is specified relative to the start of the range operation (WriteStream and
    // CopyToStream)
    virtual bool Report(aff4_off_t readptr) {
        UNUSED(readptr);
        return true;
    }
    virtual ~ProgressContext() {}
};

// The empty progress renderer is always available.
extern ProgressContext empty_progress;


// Some default progress functions that come out of the box.
class DefaultProgress: public ProgressContext {
    virtual bool Report(aff4_off_t readptr);
};

class AFF4Stream: public AFF4Object {
  protected:
    aff4_off_t readptr;
    aff4_off_t size;             // How many bytes are used in the stream?

  public:
    AFF4StreamProperties properties;

    // Compression method supported by this stream. Note that not all compression
    // methods are supported by all streams.
    int compression_method = AFF4_IMAGE_COMPRESSION_ENUM_STORED;

    AFF4Stream(DataStore* resolver, URN urn):
        AFF4Object(resolver, urn), readptr(0), size(0) {}

    explicit AFF4Stream(DataStore* result):
        AFF4Object(result), readptr(0), size(0) {}

    // Convenience methods.
    int Write(const std::unique_ptr<std::string>& data);
    int Write(const std::string& data);
    int Write(const char data[]);
    int sprintf(std::string fmt, ...);

    // Read a null terminated string.
    std::string ReadCString(size_t length);
    int ReadIntoBuffer(void* buffer, size_t length);

    // Copies length bytes from this stream to the output stream.
    virtual AFF4Status CopyToStream(
        AFF4Stream& output, aff4_off_t length,
        ProgressContext* progress = nullptr,
        size_t buffer_size = 10*1024*1024);

    // Copies the entire source stream into this stream. This is the opposite of
    // CopyToStream. By default we copy from the start to the end of the stream.
    virtual AFF4Status WriteStream(
        AFF4Stream* source,
        ProgressContext* progress = nullptr);

    // The following should be overriden by derived classes.
    virtual AFF4Status Seek(aff4_off_t offset, int whence);
    virtual std::string Read(size_t length);
    virtual int Write(const char* data, int length);
    virtual aff4_off_t Tell();
    virtual aff4_off_t Size();

    /**
     * Streams are always reset to their begining when returned from the cache.
     *
     *
     * @return
     */
    virtual AFF4Status Prepare() {
        Seek(0, SEEK_SET);
        return STATUS_OK;
    }

    /**
     * Streams can be truncated. This means the older stream data will be removed
     * and the object is returned to its initial state.
     *
     *
     * @return STATUS_OK if we were able to truncate the stream successfully.
     */
    virtual AFF4Status Truncate() {
        return NOT_IMPLEMENTED;
    }
};

class StringIO: public AFF4Stream {
  public:
    std::string buffer;
    explicit StringIO(DataStore* resolver): AFF4Stream(resolver) {}
    StringIO(): AFF4Stream(nullptr) {}
    explicit StringIO(std::string data): AFF4Stream(nullptr), buffer(data) {}

    // Convenience constructors.
    static std::unique_ptr<StringIO> NewStringIO() {
        std::unique_ptr<StringIO> result(new StringIO());

        return result;
    }

    virtual std::string Read(size_t length);
    virtual int Write(const char* data, int length);

    virtual AFF4Status Truncate();
    virtual aff4_off_t Size();

    using AFF4Stream::Write;
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
    AFF4VolumeProperties properties;

    // This is a list of URNs contained in this volume.
    std::unordered_set<std::string> children;
    AFF4Volume(DataStore* resolver, URN urn): AFF4Object(resolver, urn) {}
    explicit AFF4Volume(DataStore* resolver): AFF4Object(resolver) {}

    // Create a new contained member. Note that if the member already exists you
    // should be able to just open it with the factory - so this is only for
    // creating new members.
    virtual AFF4ScopedPtr<AFF4Stream> CreateMember(URN child) = 0;
};


// Flip this to True to abort the current imager. Note: This is a global setting
// so it is only suitable for setting in a signal handler when a single imager
// is running. To abort a specific imager, call its Abort() method.
extern bool aff4_abort_signaled;

#endif  // SRC_AFF4_IO_H_
