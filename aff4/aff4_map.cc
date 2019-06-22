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
#include "aff4/lexicon.h"
#include "aff4/aff4_map.h"
#include "aff4/aff4_image.h"
#include "aff4/libaff4.h"
#include "aff4/volume_group.h"

#include <memory>

#define BUFF_SIZE 1024*1024

namespace aff4 {


std::string Range::SerializeToString() {
    BinaryRange result;
    result.map_offset = map_offset;
    result.target_offset = target_offset;
    result.length = length;
    result.target_id = target_id;

    return  std::string(reinterpret_cast<char*>(&result), sizeof(result));
}

AFF4Status AFF4Map::NewAFF4Map(
    DataStore* resolver, const URN& object_urn,
    AFF4Volume *volume,
    AFF4Stream *data_stream,
    AFF4Flusher<AFF4Map> &result) {

    if (volume == nullptr) {
        return INVALID_INPUT;
    }

    auto new_object = make_flusher<AFF4Map>(resolver);
    new_object->urn = object_urn;

    // If callers did not provide a data stream we create a default
    // one.
    if (data_stream == nullptr) {
        AFF4Flusher<AFF4Stream> default_data_stream;
        RETURN_IF_ERROR(
            AFF4Image::NewAFF4Image(
                resolver, object_urn.Append("data"), volume,
                default_data_stream));

        data_stream = default_data_stream.get();
        new_object->our_targets.push_back(std::move(default_data_stream));
    }

    new_object->last_target = data_stream;
    new_object->targets.push_back(data_stream);

    resolver->Set(object_urn, AFF4_TYPE, new URN(AFF4_MAP_TYPE),
                  /* replace= */ false);
    resolver->Set(object_urn, AFF4_STORED, new URN(volume->urn));
    new_object->current_volume = volume;

    result = std::move(new_object);

    return STATUS_OK;
}

AFF4Status AFF4Map::OpenAFF4Map(
    DataStore* resolver, const URN& object_urn,
    VolumeGroup *volumes, AFF4Flusher<AFF4Map> &map_obj) {

    map_obj = make_flusher<AFF4Map>(resolver);
    map_obj->urn = object_urn;
    map_obj->volumes = volumes;

    URN map_stream_urn = map_obj->urn.Append("map");
    AFF4Flusher<AFF4Stream> map_stream;
    RETURN_IF_ERROR(volumes->GetStream(map_stream_urn, map_stream));

    URN map_idx_urn = map_obj->urn.Append("idx");
    AFF4Flusher<AFF4Stream> map_idx;
    RETURN_IF_ERROR(volumes->GetStream(map_idx_urn, map_idx));

    // We assume the idx stream is not too large so it can fit in memory.
    std::istringstream f(map_idx->Read(map_idx->Size()));
    std::string line;
    while (std::getline(f, line)) {
        // deal with CRLF EOL. (hack).
        if(*line.rbegin() == '\r'){
            line.erase(line.length()-1, 1);
        }
        AFF4Flusher<AFF4Stream> target;
        RETURN_IF_ERROR(volumes->GetStream(URN(line), target));

        resolver->logger->debug("MAP: Opened {} {} for target {}",
                                target->urn, line,  map_obj->targets.size());

        map_obj->target_idx_map[target.get()] = map_obj->targets.size();
        map_obj->targets.push_back(target.get());
        map_obj->GiveTarget(std::move(target));
    }

    // Calculate number of ranges
    const auto n = map_stream->Size() / sizeof(BinaryRange);

    // Ensure the Range type hasn't added any extra data members
    static_assert(sizeof(BinaryRange) == sizeof(Range), 
                  "Range has been extended and must be converted here");
    auto buffer = std::unique_ptr<Range[]>{new Range[n]};

    map_stream->ReadIntoBuffer(buffer.get(), n * sizeof(BinaryRange));

    for (size_t i = 0; i < n; i++) {
        auto & range = buffer[i];
        map_obj->map[range.map_end()] = range;
    }

    // If the map has a STREAM_SIZE property we set the size based on that,
    // otherwise we fall back to the last range in the map.
    XSDInteger value;
    if (resolver->Get(map_obj->urn, AFF4_STREAM_SIZE, value) == STATUS_OK) {
        map_obj->size = value.value;
    } else {
        if (!map_obj->map.empty()) {
            map_obj->size = (--map_obj->map.end())->second.map_end();
        }
    }

    return STATUS_OK;
}

void AFF4Map::GiveTarget(AFF4Flusher<AFF4Stream> &&target) {
    our_targets.push_back(std::move(target));
}

AFF4Status AFF4Map::ReadBuffer(char* data, size_t* length) {
    if (*length > AFF4_MAX_READ_LEN) {
        *length = 0;
        return STATUS_OK; // FIXME?
    }

    size_t remaining = std::min((aff4_off_t)*length, Size() - readptr);
    *length = 0;

    std::string tdata;

    while (remaining > 0) {
        auto map_it = map.upper_bound(readptr);

        // No range contains the current readptr - just pad it.
        if (map_it == map.end()) {
            *length = remaining;
            readptr += remaining;
            return STATUS_OK;
        }

        Range range = map_it->second;
        aff4_off_t length_to_start_of_range = std::min(
            (aff4_off_t)remaining, (aff4_off_t)(range.map_offset - readptr));
        if (length_to_start_of_range > 0) {
            // Null pad it.
            *length += length_to_start_of_range;
            remaining = std::max(
                (aff4_off_t)0,
                (aff4_off_t)remaining - length_to_start_of_range
            );
            readptr = std::min((aff4_off_t)Size(),
                               readptr + length_to_start_of_range);
            continue;
        }

        // Borrow a reference to the stream.
        AFF4Stream *target_stream = targets[range.target_id];
        size_t length_to_read_in_target = remaining;

        if (range.map_end() - readptr <= SIZE_MAX)
            length_to_read_in_target = std::min(
                remaining, (size_t)(range.map_end() - readptr));

        aff4_off_t offset_in_target = range.target_offset + (
                                          readptr - range.map_offset);

        target_stream->Seek(offset_in_target, SEEK_SET);

        tdata.resize(length_to_read_in_target);
        size_t rlen = length_to_read_in_target;

        resolver->logger->debug("MAP: Reading {} @ {}",
                                target_stream->urn, offset_in_target);

        target_stream->ReadBuffer(&tdata[0], &rlen);
        tdata.resize(rlen);

        if (tdata.size() < length_to_read_in_target) {
            // Failed to read some portion of memory. On Windows platforms, this is usually
            // due to Virtual Secure Mode (VSM) memory. Re-read memory in smaller units,
            // while leaving the unreadable regions null-padded.
            resolver->logger->info(
                "Map target {} cannot produced required {} bytes at offset 0x{:x}. Got {} bytes. Will re-read one page at a time.",
                target_stream->urn.SerializeToString(),
                length_to_read_in_target, offset_in_target, tdata.size());

            // Reset target_strem back to original position, and then re-read one page at a time.
            target_stream->Seek(offset_in_target, SEEK_SET);

            // Grow the data buffer to the expected size, filled to the end with null bytes.
            tdata.resize(length_to_read_in_target, 0);
            const char* buffer = tdata.data();

            size_t reread_total = 0;
            while (reread_total < length_to_read_in_target) {
                size_t reread_want = std::min((size_t)(length_to_read_in_target - reread_total), max_reread_size);
                int reread_actual = target_stream->ReadIntoBuffer((void *)buffer, reread_want);
                if (reread_actual < reread_want) {
                    resolver->logger->info(
                        "Map target {}: Read error starting at offset 0x{:x} of {} bytes. Expected {} bytes. Null padding.",
                        target_stream->urn.SerializeToString(),
                        offset_in_target+reread_total,
                        reread_actual, reread_want);
                }
                reread_total += reread_want;
                // Advance the pointer in the buffer.
                buffer += reread_want;

                // ensure that we seek to the correct position in target_stream
                target_stream->Seek(offset_in_target+reread_total, SEEK_SET);
            }
        }

        std::memcpy(data+*length, tdata.data(), tdata.size());
        *length += tdata.size();

        readptr = std::min(Size(), (aff4_off_t)(readptr + length_to_read_in_target));
        remaining = std::max((size_t)0, remaining - length_to_read_in_target);
    }

    return STATUS_OK;
}

aff4_off_t AFF4Map::Size() const {
    return size;
}

void AFF4Map::SetSize(aff4_off_t size) {
    this->size = size;
    MarkDirty();
}

static std::vector<Range> _MergeRanges(std::vector<Range>& ranges) {
    std::vector<Range> result;
    Range last_range;
    last_range.target_id = -1;

    bool last_range_set = false;

    for (Range range : ranges) {
        // This range should be merged with the last one.
        if (last_range.target_id == range.target_id &&
                last_range.map_end() == range.map_offset &&
                last_range.target_end() == range.target_offset) {
            // Just extend the last range to include this range.
            last_range.length += range.length;
            continue;
        }

        // Flush the last range and start counting again.
        if (last_range_set) {
            result.push_back(last_range);
        }

        last_range_set = true;
        last_range = range;
    }

    // Flush the last range and start counting again.
    if (last_range_set) {
        result.push_back(last_range);
    }

    return result;
}

/**
 * Add the new range into the map. If the new range can be merged with existing
 * ranges, the map is adjusted so that the new range overrides the existing
 * ranges - i.e. existing ranges are shrunk as needed to not overlap with the
 * new range. Ultimately no ranges may overlap in the map because that will
 * create an ambiguous situation.
 *

The algorithm is in two stages:

Phase 1: A splitting phase - in this step the new range is split into subranges
such that a subrange is either not contained by an existing range or contained
in the existing range.

For subranges which are not already covered by an existing range, they can be
immediately added to the map.

If a subrange is already covered by an existing range, the existing range is
split into three: The subrange of the old range before the new subrange (pre old
range), the new subrange itself, and the subrange of the old range after the new
subrange's end (post old range). The old complete range is removed, and the new
subranges are added. Note that either or both of the old subranges may have a
length of 0 in which case they are not added.

Diagram:
         |<----------------->|       Old range
   |-------------------|   New range.

After splitting:
   |--1--|------2------|  Subranges

Subrange 1 can be immediately added to the map since it does not conflict with
exiting ranges. Subrange 2 goes through the merge procedure:

      |<--------------------->|    Old range
              |-----|              Subrange

After split we have 3 subranges (2 is the new subrange):
      |<--1-->|--2--|<---3--->|

Note that either subrange 1 or 3 can have a length == 0 which means they do not
get added to the map.

Phase 2: the merge phase. In this phase we iterate over the map and merge
neighbouring ranges if possible (i.e. they effectively point at the same
mapping).

 * @param map_offset
 * @param target_offset
 * @param length
 * @param target
 *
 * @return
 */
AFF4Status AFF4Map::AddRange(aff4_off_t map_offset, aff4_off_t target_offset,
                             aff4_off_t length, AFF4Stream *target) {
    auto it = target_idx_map.find(target);
    Range subrange;

    last_target = target;

    // Since it is difficult to modify the map while iterating over it, we collect
    // modifications in this vector and then apply them directly.
    std::vector<Range> to_remove;
    std::vector<Range> to_add;

    if (it == target_idx_map.end()) {
        target_idx_map[target] = targets.size();
        subrange.target_id = targets.size();

        targets.push_back(target);
    } else {
        subrange.target_id = it->second;
    }

    // We want to merge with the previous range. Therefore we add it to both the
    // remove and add lists. If merging is possible it will be modified in the
    // to_add list, otherwise it will simply be removed and re-added.
    {
        auto map_it = map.upper_bound(map_offset);
        if (map_it != map.begin()) {
            map_it--;
            to_remove.push_back(map_it->second);
            to_add.push_back(map_it->second);
        }
    }

    // Phase 1: Split new range into subranges.
    while (length > 0) {
        auto map_it = map.upper_bound(map_offset);

        subrange.map_offset = map_offset;
        subrange.target_offset = target_offset;

        // Range not found in map - there are no more old ranges after the start of
        // this range. We can just add the entire range and return.
        if (map_it == map.end()) {
            subrange.length = length;

            to_add.push_back(subrange);

            length = 0;
            continue;
        }

        Range old_range = map_it->second;

        // Old range starts after the begining of this range. This means this
        // subrange is not covered by the old range.
        if (old_range.map_offset > (size_t)map_offset) {
            subrange.length = std::min(
                (aff4_off_t)length,
                (aff4_off_t)old_range.map_offset - map_offset);

            // Subrange is not covered by old range, just add it to the map.
            to_add.push_back(subrange);

            // Consume the subrange and continue splitting the next subrange.
            map_offset += subrange.length;
            target_offset += subrange.length;
            length -= subrange.length;
            continue;
        }

        // If we get here, the next subrange overlaps with the old range. First
        // split the subrange to consume as much as the old range as possible but
        // not exceed it. Then we split the old range into three subranges.
        subrange.length = std::min(
                              (aff4_off_t)length,
                              (aff4_off_t)(old_range.map_end() - subrange.map_offset));

        map_offset += subrange.length;
        target_offset += subrange.length;
        length -= subrange.length;

        Range pre_old_range = old_range;
        Range post_old_range = old_range;

        pre_old_range.length = subrange.map_offset - old_range.map_offset;

        post_old_range.length = std::min(
                                    post_old_range.length,
                                    old_range.map_end() - subrange.map_end());

        post_old_range.map_offset = old_range.map_end() - post_old_range.length;
        post_old_range.target_offset = (old_range.target_end() -
                                        post_old_range.length);

        // Remove the old range and insert the three new ones.
        to_remove.push_back(map_it->second);

        if (pre_old_range.length > 0) {
            to_add.push_back(pre_old_range);
        }

        to_add.push_back(subrange);

        if (post_old_range.length > 0) {
            to_add.push_back(post_old_range);
        }
    }

    // We want to merge with the next range after the new subranges. We add and
    // remove the next existing range in the map found after the last sub range to
    // be added.
    {
        Range last_range = to_add.back();

        auto map_it = map.upper_bound(last_range.map_end());
        if (map_it != map.end()) {
            to_remove.push_back(map_it->second);
            to_add.push_back(map_it->second);
        }
    }

    // Phase 2: Merge subranges together. All the newly added ranges will be
    // merged into the smallest number of ranges possible. We then update the map
    // atomically.
    {
        to_add = _MergeRanges(to_add);
        for (Range it : to_remove) {
            map.erase(it.map_end());
        }

        for (Range it : to_add) {
            map[it.map_end()] = it;
        }
    }

    const auto last_byte = (--map.end())->second.map_end();
    if (size < last_byte) {
        size = last_byte;
    }

    MarkDirty();

    return STATUS_OK;
}

AFF4Status AFF4Map::Flush() {
    if (IsDirty() && current_volume) {
        {
            // Get the volume we are stored on.
            AFF4Flusher<AFF4Stream> map_stream;
            RETURN_IF_ERROR(current_volume->CreateMemberStream(
                                urn.Append("map"), map_stream));

            for (auto it : map) {
                Range* range_it = &it.second;
                RETURN_IF_ERROR(map_stream->Write(range_it->SerializeToString()));
            }
        }

        {
            AFF4Flusher<AFF4Stream> idx_stream;
            RETURN_IF_ERROR(
                current_volume->CreateMemberStream(urn.Append("idx"), idx_stream));

            for (auto &it : targets) {
                idx_stream->sprintf("%s\n", it->urn.SerializeToString().c_str());
            }

        }

        // Add the stream size property to the map
        resolver->Set(urn, AFF4_STREAM_SIZE, new XSDInteger(size));
    }

    return AFF4Stream::Flush();
}


void AFF4Map::Dump() {
    for (auto it : map) {
        resolver->logger->info("Key: {}  map_offset={:x} target_offset={:x} length={:x} target_id={} ",
                               it.first, it.second.map_offset, it.second.target_offset,
                               it.second.length, it.second.target_id);
    }
}


std::vector<Range> AFF4Map::GetRanges() const {
    std::vector<Range> result;
    for (auto it : map) {
        result.push_back(it.second);
    }

    return result;
}


void AFF4Map::Clear() {
    map.clear();
    target_idx_map.clear();
    targets.clear();
}

AFF4Status AFF4Map::Write(const char* data, size_t length) {
    AddRange(readptr, last_target->Size(), length, last_target);

    // Append the data on the end of the stream.
    last_target->Seek(0, SEEK_END);
    RETURN_IF_ERROR(last_target->Write(data, length));

    readptr += length;

    MarkDirty();

    return STATUS_OK;
}

// This is the default WriteStream() which operates on linear streams. We just
// copy the source into our data stream and then add a single range to the map
// to encapsulate it.
AFF4Status AFF4Map::WriteStream(AFF4Stream* source, ProgressContext* progress) {
    RETURN_IF_ERROR(last_target->WriteStream(source, progress));

    // Add a single range to cover the bulk of the image.
    AddRange(0, 0, last_target->Size(), last_target);

    return STATUS_OK;
}

// This specialized version of WriteStream() operates on another AFF4Map. All
// ranges in the source are copied in order into this map's data stream and this
// map is adjusted to reflect the source's ranges. The result is a direct and
// efficient copy of the source - preserving the sparseness.
AFF4Status AFF4Map::CopyStreamFromMap(
    AFF4Map* source, AFF4Map* dest, ProgressContext* progress) {
    source->resolver->logger->debug("Copy Map Stream {} -> {} ", source->urn, dest->urn);

    // For each range in the source map we copy the data into the
    // destination's data stream and add a range.
    for (auto &range: source->GetRanges()) {
        AFF4Stream *target_stream = source->targets[range.target_id];

        // Append the range's data on the end of the data stream.
        dest->last_target->Seek(0, SEEK_END);
        aff4_off_t data_stream_offset = dest->last_target->Tell();

        target_stream->Seek(range.target_offset, SEEK_SET);

        RETURN_IF_ERROR(target_stream->CopyToStream(
                            *dest->last_target, range.length, progress));

        dest->AddRange(range.map_offset, data_stream_offset,
                       range.length, dest->last_target);
    }

    return STATUS_OK;
}

bool AFF4Map::CanSwitchVolume() {
    for (auto x: targets) {
        if (!x->CanSwitchVolume()) {
            return false;
        }
    }

    return true;
}

AFF4Status AFF4Map::SwitchVolume(AFF4Volume *volume) {
    current_volume = volume;

    for (auto x: targets) {
        RETURN_IF_ERROR(x->SwitchVolume(volume));
    }

    return STATUS_OK;
}



} // namespace aff4
