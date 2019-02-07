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

#include <memory>

namespace aff4 {


std::string Range::SerializeToString() {
    BinaryRange result;
    result.map_offset = map_offset;
    result.target_offset = target_offset;
    result.length = length;
    result.target_id = target_id;

    return  std::string(reinterpret_cast<char*>(&result), sizeof(result));
}

AFF4ScopedPtr<AFF4Map> AFF4Map::NewAFF4Map(
    DataStore* resolver, const URN& object_urn, const URN& volume_urn) {
    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
                                           volume_urn);

    if (!volume) {
        return AFF4ScopedPtr<AFF4Map>();    /** Volume not known? */
    }

    // Inform the volume that we have a new image stream contained within it.
    volume->children.insert(object_urn.value);

    resolver->Set(object_urn, AFF4_TYPE, new URN(AFF4_MAP_TYPE),
                  /* replace= */ false);
    resolver->Set(object_urn, AFF4_STORED, new URN(volume_urn));

    return resolver->AFF4FactoryOpen<AFF4Map>(object_urn);
}


AFF4Status AFF4Map::LoadFromURN() {
    URN map_stream_urn = urn.Append("map");
    URN map_idx_urn = urn.Append("idx");

    AFF4ScopedPtr<AFF4Stream> map_idx = resolver->AFF4FactoryOpen<AFF4Stream>(
        map_idx_urn);

    // Parse the map out of the map stream. If the stream does not exist yet we
    // just start with an empty map.
    if (map_idx.get()) {
        // We assume the idx stream is not too large so it can fit in memory.
        std::istringstream f(map_idx->Read(map_idx->Size()));
        std::string line;
        while (std::getline(f, line)) {
                // deal with CRLF EOL. (hack).
                if(*line.rbegin() == '\r'){
                        line.erase(line.length()-1, 1);
                }
            target_idx_map[line] = targets.size();
            targets.push_back(line);
        }

        AFF4ScopedPtr<AFF4Stream> map_stream = resolver->AFF4FactoryOpen
                                               <AFF4Stream>(map_stream_urn);
        if (!map_stream) {
            resolver->logger->info("Unable to open map data.");
            // Clear the map so we start with a fresh map.
            Clear();
        } else {
            // Calculate number of ranges
            const auto n = map_stream->Size() / sizeof(BinaryRange);
            auto buffer = new BinaryRange[n];

            map_stream->ReadIntoBuffer(buffer, n * sizeof(BinaryRange));

            for (size_t i = 0; i < n; i++) {
                const auto & binary_range = buffer[i];
                Range range{binary_range};
                map[range.map_end()] = range;
            }

            delete [] buffer;
        }
    }

    return STATUS_OK;
}

std::string AFF4Map::Read(size_t length) {
    if (length > AFF4_MAX_READ_LEN) {
        return "";
    }

    std::string result;
    length = std::min((aff4_off_t)length, Size() - readptr);

    while (length > 0) {
        auto map_it = map.upper_bound(readptr);

        // No range contains the current readptr - just pad it.
        if (map_it == map.end()) {
            result.resize(length);
            readptr += length;
            return result;
        }

        Range range = map_it->second;
        aff4_off_t length_to_start_of_range = std::min(
            (aff4_off_t)length, (aff4_off_t)(range.map_offset - readptr));
        if (length_to_start_of_range > 0) {
            // Null pad it.
            result.resize(result.size() + length_to_start_of_range);
            length = std::max((aff4_off_t)0,
                              (aff4_off_t)length - length_to_start_of_range);
            readptr = std::min((aff4_off_t)Size(),
                               readptr + length_to_start_of_range);
            continue;
        }

        // The readptr is inside a range.
        URN target = targets[range.target_id];
        size_t length_to_read_in_target = length;

        if (range.map_end() - readptr <= SIZE_MAX)
            length_to_read_in_target = std::min(
                length, (size_t)(range.map_end() - readptr));

        aff4_off_t offset_in_target = range.target_offset + (
                                          readptr - range.map_offset);

        AFF4ScopedPtr<AFF4Stream> target_stream = resolver->AFF4FactoryOpen<
                AFF4Stream>(target);

        if (!target_stream) {
            resolver->logger->info("Unable to open target stream {} "
                                   " For map {}  (Offset {:x})",
                                   target.value, urn.value, readptr);
            // Null pad
            result.resize(result.size() + length_to_read_in_target);
            length = std::max((size_t)0, length - length_to_read_in_target);
            readptr = std::min(Size(), (aff4_off_t)(readptr + length_to_read_in_target));
            continue;
        }

        target_stream->Seek(offset_in_target, SEEK_SET);
        {
            std::string data = target_stream->Read(length_to_read_in_target);
            if (data.size() < length_to_read_in_target) {
                // Failed to read some portion of memory. On Windows platforms, this is usually
                // due to Virtual Secure Mode (VSM) memory. Re-read memory in smaller units,
                // while leaving the unreadable regions null-padded.
                resolver->logger->info(
                    "Map target {} cannot produced required {} bytes at offset 0x{:x}. Got {} bytes. Will re-read one page at a time.",
                    target_stream->urn.SerializeToString(),
                    length_to_read_in_target, offset_in_target, data.size());

                // Reset target_strem back to original position, and then re-read one page at a time.
                target_stream->Seek(offset_in_target, SEEK_SET);

                // Grow the data buffer to the expected size, filled to the end with null bytes.
                data.resize(length_to_read_in_target, 0);
                const char* buffer = data.data();

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
            };
            result += data;
        }
        readptr = std::min(Size(), (aff4_off_t)(readptr + length_to_read_in_target));
        length = std::max((size_t)0, length - length_to_read_in_target);
    }

    return result;
}

aff4_off_t AFF4Map::Size() {
    // The size of the stream is the end of the last range.
    auto it = map.end();
    if (it == map.begin()) {
        return 0;
    }

    it--;
    return it->second.map_end();
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

class _MapStreamHelper: public AFF4Stream {
    friend AFF4Map;

  protected:
    aff4_off_t range_offset = 0;
    std::map<aff4_off_t, Range>::iterator current_range;

  public:
    AFF4Map* source;   // The map we are reading from.
    AFF4Map* destination;  // The map we are creating.

    explicit _MapStreamHelper(
        DataStore* resolver, AFF4Map* source, AFF4Map* destination):
        AFF4Stream(resolver), source(source), destination(destination) {
        current_range = source->map.begin();
    }

    /* Just read data from the ranges consecutively. */
    virtual std::string Read(size_t length) {
        std::string result;

        // This is the target data stream of the map.
        URN target;
        AFF4Status res = destination->GetBackingStream(target);
        if (res != STATUS_OK) {
            return "";
        }

        // Need more data - read more.
        while (result.size() < length) {
            // We are done! All source ranges read.
            if (current_range == source->map.end()) {
                break;
            }

            // Add a range if we are at the beginning of a range.
            if (range_offset == 0)
                destination->AddRange(current_range->second.map_offset,
                                      // This is the current offset in the data stream.
                                      readptr,
                                      current_range->second.length,
                                      target);

            // Read as much data as possible from this range.
            size_t to_read;
            to_read = std::min(
                          // How much we need.
                          (aff4_off_t)(length - result.size()),
                          // How much is available in this range.
                          (aff4_off_t)current_range->second.length - range_offset);

            // Range is exhausted - get the next range.
            if (to_read == 0) {
                current_range++;
                range_offset = 0;
                continue;
            }

            // Read and copy the data.
            URN source_urn = source->targets[current_range->second.target_id];
            AFF4ScopedPtr<AFF4Stream> source = resolver->AFF4FactoryOpen<AFF4Stream>(
                                                   source_urn);

            if (!source) {
                resolver->logger->error("Unable to open URN {}",
                                        source_urn.SerializeToString());
                break;
            }

            source->Seek(
                current_range->second.target_offset + range_offset, SEEK_SET);

            std::string data = source->Read(to_read);
            if (data.size() == 0) {
                break;
            }

            result += data;
            range_offset += data.size();

            // Keep track of all the data we have released.
            readptr += data.size();
        }

        return result;
    }

    virtual ~_MapStreamHelper() {}
};


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
                             aff4_off_t length, URN target) {
    std::string key = target.SerializeToString();
    auto it = target_idx_map.find(key);
    Range subrange;

    last_target = target;

    // Since it is difficult to modify the map while iterating over it, we collect
    // modifications in this vector and then apply them directly.
    std::vector<Range> to_remove;
    std::vector<Range> to_add;

    if (it == target_idx_map.end()) {
        target_idx_map[key] = targets.size();
        subrange.target_id = targets.size();

        targets.push_back(key);
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

    MarkDirty();

    return STATUS_OK;
}

AFF4Status AFF4Map::Flush() {
    if (IsDirty()) {
        // Get the volume we are stored on.
        URN volume_urn;
        AFF4Status res = resolver->Get(urn, AFF4_STORED, volume_urn);
        if (res != STATUS_OK) {
            return res;
        }

        AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
                                               volume_urn);

        if (!volume) {
            return IO_ERROR;
        }

        AFF4ScopedPtr<AFF4Stream> map_stream = volume->CreateMember(
                urn.Append("map"));
        if (!map_stream) {
            return IO_ERROR;
        }

        for (auto it : map) {
            Range* range_it = &it.second;
            RETURN_IF_ERROR(map_stream->Write(range_it->SerializeToString()));
        }

        resolver->Close(map_stream);

        AFF4ScopedPtr<AFF4Stream> idx_stream = volume->CreateMember(
                urn.Append("idx"));
        if (!idx_stream) {
            return IO_ERROR;
        }

        for (auto it : targets) {
            idx_stream->sprintf("%s\n", it.SerializeToString().c_str());
        }

        resolver->Close(idx_stream);
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


std::vector<Range> AFF4Map::GetRanges() {
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

AFF4Status AFF4Map::GetBackingStream(URN& target) {
    // Using the write API we automatically select the last target used.
    if (targets.size() == 0) {
        target = urn.Append("data");
    } else {
        // If we had this target before, we assume it exists.
        target = last_target;
        return STATUS_OK;
    }

    // Try to ensure the target exists.
    AFF4ScopedPtr<AFF4Stream> stream = resolver->AFF4FactoryOpen<AFF4Stream>(
                                           target);

    // Backing stream is fine - just use it.
    if (stream.get()) {
        return STATUS_OK;
    }

    // If the backing stream does not already exist, we make one.

    // Get the containing volume.
    URN volume_urn;
    if (resolver->Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
        resolver->logger->info("Map not stored in a volume.");
        return IO_ERROR;
    }

    URN compression_urn;
    resolver->Get(target, AFF4_IMAGE_COMPRESSION, compression_urn);
    AFF4_IMAGE_COMPRESSION_ENUM compression = CompressionMethodFromURN(
                compression_urn);

    resolver->logger->info("Stream will be compressed with {}",
                           compression_urn.SerializeToString());

    // If the stream should not be compressed, it is more efficient to use a
    // native volume member (e.g. ZipFileSegment or FileBackedObjects) than
    // the more complex bevy based images.
    if (compression == AFF4_IMAGE_COMPRESSION_ENUM_STORED) {
        AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
                                               volume_urn);

        if (volume.get()) {
            volume->CreateMember(target);
            return STATUS_OK;
        }
    } else {
        AFF4ScopedPtr<AFF4Image> stream = AFF4Image::NewAFF4Image(
                                              resolver, target, volume_urn);
        if (stream.get()) {
            return STATUS_OK;
        }
    }
    return IO_ERROR;
}


AFF4Status AFF4Map::Write(const char* data, size_t length) {
    URN target;
    RETURN_IF_ERROR(GetBackingStream(target));

    AFF4ScopedPtr<AFF4Stream> stream = resolver->AFF4FactoryOpen<AFF4Stream>(
                                           target);

    AddRange(readptr, stream->Size(), length, target);

    // Append the data on the end of the stream.
    stream->Seek(0, SEEK_END);
    RETURN_IF_ERROR(stream->Write(data, length));

    readptr += length;

    MarkDirty();

    return STATUS_OK;
}

// This is the default WriteStream() which operates on linear streams. We just
// copy the source into our data stream and then add a single range to the map
// to encapsulate it.
AFF4Status AFF4Map::WriteStream(AFF4Stream* source, ProgressContext* progress) {
    URN data_stream_urn;
    AFF4Status result = GetBackingStream(data_stream_urn);
    if (result == STATUS_OK) {
        AFF4ScopedPtr<AFF4Stream> data_stream = resolver->AFF4FactoryOpen<
                                                AFF4Stream>(data_stream_urn);

        if (!data_stream) {
            return IO_ERROR;
        }

        result = data_stream->WriteStream(source, progress);

        // Add a single range to cover the bulk of the image.
        AddRange(0, data_stream->Size(), data_stream->Size(), data_stream->urn);
    }

    return result;
}


// This specialized version of WriteStream() operates on another AFF4Map. All
// ranges in the source are copied in order into this map's data stream and this
// map is adjusted to reflect the source's ranges. The result is a direct and
// efficient copy of the source - preserving the sparseness.
AFF4Status AFF4Map::WriteStream(AFF4Map* source, ProgressContext* progress) {
    URN data_stream_urn;
    AFF4Status result = GetBackingStream(data_stream_urn);
    if (result == STATUS_OK) {
        AFF4ScopedPtr<AFF4Stream> data_stream = resolver->AFF4FactoryOpen<
                                                AFF4Stream>(data_stream_urn);

        if (!data_stream) {
            return IO_ERROR;
        }

        _MapStreamHelper helper(resolver, source, this);
        result = data_stream->WriteStream(&helper, progress);
    }

    return result;
}

static AFF4Registrar<AFF4Map> map1(AFF4_MAP_TYPE);
static AFF4Registrar<AFF4Map> map2(AFF4_LEGACY_MAP_TYPE);

void aff4_map_init() {}

} // namespace aff4
