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

#include "aff4/lexicon.h"

#include <unordered_map>


namespace aff4 {

#define LEXICON_DEFINE(x, y)                                            \
    const char  x[] = y

#include "aff4/lexicon.inc"

#undef LEXICON_DEFINE


std::unordered_map<std::string, Schema> Schema::cache;


Schema Schema::GetSchema(std::string object_type) {
    // If we did not add any schema yet, do so now.
    if (cache.size() == 0) {
        // Define all the Schema and attributes.
        Schema AFF4_OBJECT_SCHEMA("object");
        AFF4_OBJECT_SCHEMA.AddAttribute(
            "type", Attribute(
                AFF4_TYPE, URNType, "The type of this object."));

        Schema AFF4_STREAM_SCHEMA("generic_stream");
        AFF4_STREAM_SCHEMA.AddParent(AFF4_OBJECT_SCHEMA);

        AFF4_STREAM_SCHEMA.AddAttribute(
            "size", Attribute(
                AFF4_STREAM_SIZE, XSDIntegerType,
                "How large the object is in bytes."));

        // Volumes contain other objects.
        Schema AFF4_VOLUME_SCHEMA("volume");
        AFF4_VOLUME_SCHEMA.AddParent(AFF4_OBJECT_SCHEMA);

        Schema AFF4_ZIP_VOLUME_SCHEMA("zip_volume");
        AFF4_ZIP_VOLUME_SCHEMA.AddParent(AFF4_VOLUME_SCHEMA);

        cache["object"] = AFF4_OBJECT_SCHEMA;
        cache["stream"] = AFF4_STREAM_SCHEMA;
        cache["volume"] = AFF4_VOLUME_SCHEMA;
        cache["zip_volume"] = AFF4_ZIP_VOLUME_SCHEMA;
    };

    auto it = cache.find(object_type);
    if (it != cache.end()) {
        return it->second;
    };


    return Schema();
}

} // namespace aff4
