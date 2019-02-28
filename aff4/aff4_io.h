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

#include "aff4/config.h"

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <fstream>
#include <cstdio>
#include <chrono>
#include "aff4/aff4_base.h"
#include "aff4/data_store.h"
#include "aff4/aff4_utils.h"
#include "aff4/rdf.h"

// A constant for various buffers used by the AFF4 library.
#define AFF4_BUFF_SIZE (32 * 1024)

namespace aff4 {


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
    aff4_off_t last_offset = 0;

    // The following are set in advance by users in order to get accurate progress
    // reports.

    // Start offset of this current range.
    aff4_off_t start = 0;

    // Total length for this operation.
    aff4_off_t length = 0;

    // Total length read so far
    aff4_off_t total_read = 0;

    // This will be called periodically to report the progress. Note that readptr
    // is specified relative to the start of the range operation (WriteStream and
    // CopyToStream)
    virtual bool Report(aff4_off_t readptr) {
        UNUSED(readptr);
        return true;
    }

    explicit ProgressContext(DataStore *resolver)
        : resolver(resolver) {}

    virtual ~ProgressContext() {}

 protected:
    DataStore *resolver;
};

// The empty progress renderer is always available.
extern ProgressContext empty_progress;


// Some default progress functions that come out of the box.
class DefaultProgress: public ProgressContext {
 public:
    explicit DefaultProgress(DataStore *resolver): ProgressContext(resolver) {
        last_time = std::chrono::steady_clock::now();
    }
    virtual bool Report(aff4_off_t readptr);

 protected:
    // Managed internally by Report
    std::chrono::steady_clock::time_point last_time;
};

class AFF4Stream: public AFF4Object {
  public:
    aff4_off_t readptr;
    aff4_off_t size;             // How many bytes are used in the stream?

    AFF4StreamProperties properties;

    // Compression method supported by this stream. Note that not all compression
    // methods are supported by all streams.
    int compression_method = AFF4_IMAGE_COMPRESSION_ENUM_STORED;

    AFF4Stream(DataStore* resolver, URN urn):
        AFF4Object(resolver, urn), readptr(0), size(0) {}

    explicit AFF4Stream(DataStore* result):
        AFF4Object(result), readptr(0), size(0) {}

    // Convenience methods.
    AFF4Status Write(const std::unique_ptr<std::string>& data);
    AFF4Status Write(const std::string& data);
    AFF4Status Write(const char data[]);
    int sprintf(std::string fmt, ...);

    // Read a null terminated string.
    std::string ReadCString(size_t length);
    int ReadIntoBuffer(void* buffer, size_t length);

    // Copies length bytes from this stream to the output stream.
    virtual AFF4Status CopyToStream(
        AFF4Stream& output, aff4_off_t length,
        ProgressContext* progress = nullptr,
        size_t buffer_size = 10*1024*1024);

    // Copies the entire source stream into this stream. This is the
    // opposite of CopyToStream. By default we copy from the start to
    // the end of the stream.  Error handling depends on the stream's
    // implementation. If a stream in not capable of noting error
    // (e.g. FileBackedObject) we propagate the source's errors. If
    // the stream can note errors (e.g. Map) then it will contain its
    // own errors and will not propagate them.
    virtual AFF4Status WriteStream(
        AFF4Stream* source,
        ProgressContext* progress = nullptr);

    // The following should be overriden by derived classes.
    virtual AFF4Status Seek(aff4_off_t offset, int whence);
    virtual std::string Read(size_t length);

    virtual AFF4Status ReadBuffer(char* data, size_t* length);

    virtual AFF4Status Write(const char* data, size_t length);
    virtual aff4_off_t Tell();
    virtual aff4_off_t Size() const;

    // Callers may reserve space in the stream for efficiency. This
    // gives the implementation a hint as to how large this stream is
    // likely to be.
    virtual void reserve(size_t size);


    // Requests that this stream change its backing volume if
    // possible. CanSwitchVolume() returns true if it is possible to
    // change the backing volume in this object's
    // lifecycle. Subsequent calls to SwitchVolume() should work - it
    // is a fatal error if they do not.
    virtual bool CanSwitchVolume();
    virtual AFF4Status SwitchVolume(AFF4Volume *volume);

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
        return std::unique_ptr<StringIO>(new StringIO());
    }

    std::string Read(size_t length) override;
    AFF4Status ReadBuffer(char* data, size_t* length) override;
    AFF4Status Write(const char* data, size_t length) override;

    AFF4Status Truncate() override;
    aff4_off_t Size() const override;
    void reserve(size_t size) override;

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

    virtual AFF4Status CreateMemberStream(
        URN segment_urn,
        AFF4Flusher<AFF4Stream> &result) = 0;

    virtual AFF4Status OpenMemberStream(
        URN segment_urn,
        AFF4Flusher<AFF4Stream> &result) = 0;

    // This is used to notify the volume of a stream which is
    // contained within it. The container will ensure the dependent
    // stream is flushed *before* the volume is closed. For example,
    // a map stream is stored in the volume.
    void AddDependency(URN urn) {
        children.insert(urn.SerializeToString());
    }

    // Estimate how large this volume is.
    virtual aff4_off_t Size() const;
};


// Flip this to True to abort the current imager. Note: This is a global setting
// so it is only suitable for setting in a signal handler when a single imager
// is running. To abort a specific imager, call its Abort() method.
extern bool aff4_abort_signaled;

} // namespace aff4

#endif  // SRC_AFF4_IO_H_
