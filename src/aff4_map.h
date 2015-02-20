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

#ifndef _AFF4_MAP_H_
#define _AFF4_MAP_H_

#include "aff4_io.h"
#include <map>

struct Range {
  uint64_t map_offset;
  uint64_t target_offset;
  uint64_t length;
  uint64_t target_id;
}__attribute__((packed));


class AFF4Map: public AFF4Stream {
 protected:
  // The target list.
  vector<URN> targets;
  std::map<string, int> target_idx_map;
  std::map<size_t, Range> map;

 public:
  AFF4Map(DataStore *resolver): AFF4Stream(resolver) {};

  static AFF4ScopedPtr<AFF4Map> NewAFF4Map(
      DataStore *resolver, const URN &object_urn, const URN &volume_urn);

  virtual AFF4Status LoadFromURN();

  virtual string Read(size_t length);

  AFF4Status Flush();

  AFF4Status AddRange(size_t map_offset, size_t target_offset,
                      size_t length, URN target);

};


#endif // _AFF4_MAP_H_
