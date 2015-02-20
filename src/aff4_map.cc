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

AFF4ScopedPtr<AFF4Map> AFF4Map::NewAFF4Map(
    DataStore *resolver, const URN &object_urn, const URN &volume_urn) {
  AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
      volume_urn);

  if (!volume)
    return AFF4ScopedPtr<AFF4Map>();        /** Volume not known? */

  // Inform the volume that we have a new image stream contained within it.
  volume->children.insert(object_urn.value);

  resolver->Set(object_urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));
  resolver->Set(object_urn, AFF4_STORED, new URN(volume_urn));

  return resolver->AFF4FactoryOpen<AFF4Map>(object_urn);
}


AFF4Status AFF4Map::LoadFromURN() {
  URN map_urn = urn.Append("map");
  URN map_idx_urn = urn.Append("idx");

  AFF4ScopedPtr<AFF4Stream> map_idx = resolver->AFF4FactoryOpen<AFF4Stream>(
      map_idx_urn);

  if(!map_idx) {
    return IO_ERROR;
  };




};

string AFF4Map::Read(size_t length) {

}


AFF4Status AFF4Map::AddRange(size_t map_offset, size_t target_offset,
                             size_t length, URN target) {
  string key = target.SerializeToString();
  auto it = target_idx_map.find(key);
  Range range;

  range.map_offset = map_offset;
  range.target_offset = target_offset;
  range.length = length;

  if (it == target_idx_map.end()) {
    target_idx_map[key] = targets.size();
    range.target_id = targets.size();

    targets.push_back(key);
  } else {
    range.target_id = it->second;
  };

  map[map_offset] = range;

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
        volume_urn.Append("map"));
    if(!map_stream)
      return IO_ERROR;

    for(auto it: map) {
      Range *range_it = &it.second;
      map_stream->Write((char *)range_it, sizeof(Range));
    };

    AFF4ScopedPtr<AFF4Stream> idx_stream = volume->CreateMember(
        volume_urn.Append("idx"));
    if(!idx_stream)
      return IO_ERROR;

    for(auto it: targets) {
      map_stream->sprintf("%s\n", it.SerializeToString().c_str());
    };


  };

  return AFF4Stream::Flush();
};


static AFF4Registrar<AFF4Map> r1(AFF4_MAP_TYPE);
