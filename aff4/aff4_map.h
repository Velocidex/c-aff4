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

#include "aff4/config.h"

#include "aff4/aff4_io.h"
#include "aff4/volume_group.h"

#include <map>


namespace aff4 {

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

    uint64_t map_end() const {
        return map_offset + length;
    }

    uint64_t target_end() const {
        return target_offset + length;
    }

    std::string SerializeToString();
};


class AFF4Map: public AFF4Stream {
  protected:
    // The target of the next Write() operation.
    AFF4Stream* last_target = nullptr;

    aff4_off_t size = 0; // Logical size of the map stream

  public:
    // The target list. Non-owning references.
    std::vector<AFF4Stream*> targets;
    std::map<AFF4Stream*, int> target_idx_map;

    // A list of target streams we actually own. We hold on to these
    // until we get destroyed.
    std::vector<AFF4Flusher<AFF4Stream>> our_targets;

    std::map<aff4_off_t, Range> map;

    // We write our data to this volume.
    AFF4Volume *current_volume = nullptr;

    // Use this volume group to locate target streams.
    VolumeGroup *volumes = nullptr;

    // Specifies the fallback read-size to use when Read encounters
    // an unreadable region.
    size_t max_reread_size = 4096;

    bool CanSwitchVolume() override;
    AFF4Status SwitchVolume(AFF4Volume *volume) override;

    explicit AFF4Map(DataStore* resolver): AFF4Stream(resolver) {}

    // When creating a new map we must give the map the first data
    // stream. Subsequent writes will be appended on this stream. It
    // is up to the caller to create the write stream by themselves
    // and they may specify any URN for it. By default the aff4_imager
    // just appends the string "data" to the map urn. We must also
    // provide a reference to the volume on which to write the map
    // index file (contains the targets) and the map file (contains
    // the ranges). You should probably not write to the data stream
    // and the map at the same time.
    static AFF4Status NewAFF4Map(
        DataStore* resolver, const URN& map_urn,
        AFF4Volume *volume, AFF4Stream* data_stream,
        AFF4Flusher<AFF4Map> &result);


    static AFF4Status OpenAFF4Map(
        DataStore* resolver, const URN& stream_urn,
        VolumeGroup *volume, AFF4Flusher<AFF4Map> &result);


    static AFF4Status CopyStreamFromMap(
        AFF4Map* source,
        AFF4Map* dest,
        ProgressContext* progress);

    // Accepts ownership of this target. We will hold it until we get
    // destroyed.
    void GiveTarget(AFF4Flusher<AFF4Stream> &&target);

    AFF4Status ReadBuffer(char* data, size_t* length) override;
    AFF4Status Write(const char* data, size_t length) override;

    AFF4Status WriteStream(
        AFF4Stream* source,
        ProgressContext* progress = nullptr) override;

    AFF4Status Flush() override;

    AFF4Status AddRange(aff4_off_t map_offset, aff4_off_t target_offset,
                        aff4_off_t length,
                        AFF4Stream* target /* Not owned */);

    void Dump();

    std::vector<Range> GetRanges() const;

    void Clear();

    aff4_off_t Size() const override;
    void SetSize(aff4_off_t size);

    using AFF4Stream::Write;
};






extern void aff4_map_init();

} // namespace aff4

#endif  // SRC_AFF4_MAP_H_
