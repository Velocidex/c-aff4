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

#ifndef SRC_AFF4_MAP_H_
#define SRC_AFF4_MAP_H_

#include "config.h"

#include "aff4_io.h"
#include <map>

class AFF4Map;


// This is the type written to the map stream in this exact binary layout.
struct BinaryRange {
    uint64_t map_offset = 0;
    uint64_t length = 0;
    uint64_t target_offset = 0;
    uint32_t target_id = 0;
} __attribute__((packed));


class Range: public BinaryRange {
  public:
    explicit Range(BinaryRange range): BinaryRange(range) {}
    Range(): BinaryRange() {}

    aff4_off_t map_end() {
        return map_offset + length;
    }

    aff4_off_t target_end() {
        return target_offset + length;
    }

    std::string SerializeToString();
};


class AFF4Map: public AFF4Stream {
  protected:
    // The URN that will be used as the target of the next Write() operation.
    URN last_target;

  public:
    // The target list.
    std::vector<URN> targets;
    std::map<std::string, int> target_idx_map;
    std::map<aff4_off_t, Range> map;

    explicit AFF4Map(DataStore* resolver): AFF4Stream(resolver) {}

    static AFF4ScopedPtr<AFF4Map> NewAFF4Map(
        DataStore* resolver, const URN& object_urn, const URN& volume_urn);

    virtual AFF4Status LoadFromURN();

    virtual std::string Read(size_t length);
    virtual int Write(const char* data, int length);

    virtual AFF4Status WriteStream(
        AFF4Stream* source,
        ProgressContext* progress = nullptr);

    virtual AFF4Status WriteStream(
        AFF4Map* source,
        ProgressContext* progress = nullptr);

    AFF4Status Flush();

    AFF4Status AddRange(aff4_off_t map_offset, aff4_off_t target_offset,
                        aff4_off_t length, URN target);

    void Dump();

    std::vector<Range> GetRanges();

    // Creates or retrieves the underlying map data stream.
    AFF4Status GetBackingStream(URN& target);

    void Clear();

    virtual aff4_off_t Size();

    using AFF4Stream::Write;
};

extern void aff4_map_init();

#endif  // SRC_AFF4_MAP_H_
