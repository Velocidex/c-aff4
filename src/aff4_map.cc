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
#include "lexicon.h"
#include "aff4_map.h"
#include "aff4_image.h"


AFF4ScopedPtr<AFF4Map> AFF4Map::NewAFF4Map(
    DataStore *resolver, const URN &object_urn, const URN &volume_urn) {
  AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
      volume_urn);

  if (!volume)
    return AFF4ScopedPtr<AFF4Map>();        /** Volume not known? */

  // Inform the volume that we have a new image stream contained within it.
  volume->children.insert(object_urn.value);

  resolver->Set(object_urn, AFF4_TYPE, new URN(AFF4_MAP_TYPE));
  resolver->Set(object_urn, AFF4_STORED, new URN(volume_urn));

  return resolver->AFF4FactoryOpen<AFF4Map>(object_urn);
}


AFF4Status AFF4Map::LoadFromURN() {
  URN map_urn = urn.Append("map");
  URN map_idx_urn = urn.Append("idx");

  AFF4ScopedPtr<AFF4Stream> map_idx = resolver->AFF4FactoryOpen<AFF4Stream>(
      map_idx_urn);

  // Parse the map out of the map stream. If the stream does not exist yet we
  // just start with an empty map.
  if(map_idx.get()) {
    // We assume the idx stream is not too large so it can fit in memory.
    std::istringstream f(map_idx->Read(map_idx->Size()));
    std::string line;
    while (std::getline(f, line)) {
      target_idx_map[line] = targets.size();
      targets.push_back(line);
    }

    AFF4ScopedPtr<AFF4Stream> map_stream = resolver->AFF4FactoryOpen<AFF4Stream>(
        map_urn);
    if(!map_stream) {
      LOG(INFO) << "Unable to open map data.";
      // Clear the map so we start with a fresh map.
      Clear();
    } else {
      Range range;
      while (map_stream->ReadIntoBuffer(
                 &range, sizeof(range)) == sizeof(range)) {
        map[range.map_end()] = range;
      };
    };
  };

  return STATUS_OK;
};

string AFF4Map::Read(size_t length) {
  string result;
  length = std::min(length, Size() - readptr);

  while(length > 0) {
    auto map_it = map.upper_bound(readptr);

    // No range contains the current readptr - just pad it.
    if(map_it == map.end()) {
      result.resize(length);
      return result;
    };

    Range range = map_it->second;
    size_t length_to_start_of_range = range.map_offset - readptr;
    if(length_to_start_of_range > 0) {
      // Null pad it.
      result.resize(result.size() + length_to_start_of_range);
      length -= length_to_start_of_range;
      readptr += length_to_start_of_range;
      continue;
    };

    // The readptr is inside a range.
    URN target = targets[range.target_id];
    size_t length_to_read_in_target = std::min(
        length, range.map_end() - readptr);

    size_t offset_in_target = range.target_offset + (
        readptr - range.map_offset);

    AFF4ScopedPtr<AFF4Stream> target_stream = resolver->AFF4FactoryOpen<
      AFF4Stream>(target);

    if(!target_stream) {
      LOG(INFO) << "Unable to open target stream " << target.value <<
          " For map " << urn.value << " (Offset " << readptr << " )";

      // Null pad
      result.resize(result.size() + length_to_read_in_target);
      length -= length_to_read_in_target;
      readptr += length_to_read_in_target;
      continue;
    }

    target_stream->Seek(offset_in_target, SEEK_SET);
    result += target_stream->Read(length_to_read_in_target);
    readptr += length_to_read_in_target;
    length -= length_to_read_in_target;
  };

  return result;
}

size_t AFF4Map::Size() {
  // The size of the stream is the end of the last range.
  auto it = map.end();
  if(it == map.begin()) {
    return 0;
  };

  it --;
  return it->second.map_end();
}

static std::vector<Range> _MergeRanges(std::vector<Range> &ranges) {
  vector<Range> result;
  Range last_range;
  last_range.target_id = -1;

  bool last_range_set = false;

  for(Range range: ranges) {

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
  };

  // Flush the last range and start counting again.
  if (last_range_set) {
    result.push_back(last_range);
  }

  return result;
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
AFF4Status AFF4Map::AddRange(size_t map_offset, size_t target_offset,
                             size_t length, URN target) {
  string key = target.SerializeToString();
  auto it = target_idx_map.find(key);
  Range subrange;

  last_target = target;

  // Since it is difficult to modify the map while iterating over it, we collect
  // modifications in this vector and then apply them directly.
  vector<Range> to_remove;
  vector<Range> to_add;

  if (it == target_idx_map.end()) {
    target_idx_map[key] = targets.size();
    subrange.target_id = targets.size();

    targets.push_back(key);
  } else {
    subrange.target_id = it->second;
  };

  // We want to merge with the previous range. Therefore we add it to both the
  // remove and add lists. If merging is possible it will be modified in the
  // to_add list, otherwise it will simply be removed and re-added.
  {
    auto map_it = map.upper_bound(map_offset);
    if(map_it != map.begin()) {
      map_it--;
      to_remove.push_back(map_it->second);
      to_add.push_back(map_it->second);
    };
  };

  // Phase 1: Split new range into subranges.
  while (length > 0) {
    auto map_it = map.upper_bound(map_offset);

    subrange.map_offset = map_offset;
    subrange.target_offset = target_offset;

    // Range not found in map - there are no more old ranges after the start of
    // this range. We can just add the entire range and return.
    if(map_it == map.end()) {
      subrange.length = length;

      to_add.push_back(subrange);

      length = 0;
      continue;
    };

    Range old_range = map_it->second;

    // Old range starts after the begining of this range. This means this
    // subrange is not covered by the old range.
    if(old_range.map_offset > map_offset) {
      subrange.length = std::min(subrange.length,
                                 old_range.map_offset - map_offset);

      // Subrange is not covered by old range, just add it to the map.
      to_add.push_back(subrange);

      // Consume the subrange and continue splitting the next subrange.
      map_offset += subrange.length;
      target_offset += subrange.length;
      length -= subrange.length;
      continue;
    };

    // If we get here, the next subrange overlaps with the old range. First
    // split the subrange to consume as much as the old range as possible but
    // not exceed it. Then we split the old range into three subranges.
    subrange.length = std::min(length,
                               old_range.map_end() - subrange.map_offset);

    map_offset += subrange.length;
    target_offset += subrange.length;
    length -= subrange.length;

    Range pre_old_range = old_range;
    Range post_old_range = old_range;

    pre_old_range.length = subrange.map_offset - old_range.map_offset;

    post_old_range.length = std::min(post_old_range.length,
                                     old_range.map_end() - subrange.map_end());

    post_old_range.map_offset = old_range.map_end() - post_old_range.length;
    post_old_range.target_offset = (old_range.target_end() -
                                    post_old_range.length);

    // Remove the old range and insert the three new ones.
    to_remove.push_back(map_it->second);

    if(pre_old_range.length > 0) {
      to_add.push_back(pre_old_range);
    };

    to_add.push_back(subrange);

    if(post_old_range.length > 0) {
      to_add.push_back(post_old_range);
    };
  };

  // We want to merge with the next range after the new subranges. We add and
  // remove the next existing range in the map found after the last sub range to
  // be added.
  {
    Range last_range = to_add.back();

    auto map_it = map.upper_bound(last_range.map_end());
    if(map_it != map.end()) {
      to_remove.push_back(map_it->second);
      to_add.push_back(map_it->second);
    };
  };

  // Phase 2: Merge subranges together. All the newly added ranges will be
  // merged into the smallest number of ranges possible. We then update the map
  // atomically.
  {
    to_add = _MergeRanges(to_add);
    for(Range it: to_remove) {
      map.erase(it.map_end());
    };

    for(Range it: to_add) {
      map[it.map_end()] = it;
    };
  };

  MarkDirty();

  return STATUS_OK;
};

AFF4Status AFF4Map::Flush() {
  if(IsDirty()) {
    // Get the volume we are stored on.
    URN volume_urn;
    AFF4Status res = resolver->Get(urn, AFF4_STORED, volume_urn);
    if(res != STATUS_OK) {
      return res;
    };

    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
        volume_urn);

    if(!volume) {
      return IO_ERROR;
    };

    AFF4ScopedPtr<AFF4Stream> map_stream = volume->CreateMember(
        urn.Append("map"));
    if(!map_stream)
      return IO_ERROR;

    for(auto it: map) {
      Range *range_it = &it.second;
      map_stream->Write((char *)range_it, sizeof(Range));
    };

    AFF4ScopedPtr<AFF4Stream> idx_stream = volume->CreateMember(
        urn.Append("idx"));
    if(!idx_stream)
      return IO_ERROR;

    for(auto it: targets) {
      idx_stream->sprintf("%s\n", it.SerializeToString().c_str());
    };
  };

  return AFF4Stream::Flush();
};


void AFF4Map::Dump() {
  for(auto it: map) {
    LOG(INFO) << "Key:" << it.first << " map_offset=" << it.second.map_offset <<
        " target_offset=" << it.second.target_offset << " length=" << it.second.length <<
        " target_id=" << it.second.target_id;
  };
};


std::vector<Range> AFF4Map::GetRanges() {
  vector<Range> result;
  for(auto it: map) {
    result.push_back(it.second);
  };

  return result;
};


void AFF4Map::Clear() {
  map.clear();
  target_idx_map.clear();
  targets.clear();
};


int AFF4Map::Write(const char *data, int length) {
  URN target;

  // Using the write API we automatically select the last target used.
  if(targets.size() == 0) {
    target = urn.Append("data");
  } else {
    target = last_target;
  };

  AFF4ScopedPtr<AFF4Stream> stream = resolver->AFF4FactoryOpen<AFF4Stream>(
      target);

  // If the backing stream does not already exist, we make one.
  if(!stream) {
    URN volume_urn;
    if(resolver->Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
      LOG(INFO) << "Map not stored in a volume.";
      return IO_ERROR;
    };

    // Create a new stream and assign to the scoped pointer.
    stream.reset(AFF4Image::NewAFF4Image(resolver, target, volume_urn).release());
    if(!stream) {
      LOG(INFO) << "Unable to create backing stream.";
      return IO_ERROR;
    };
  };

  AddRange(readptr, stream->Size(), length, target);

  // Append the data on the end of the stream.
  stream->Seek(0, SEEK_END);
  stream->Write(data, length);

  return length;
};


static AFF4Registrar<AFF4Map> r1(AFF4_MAP_TYPE);
