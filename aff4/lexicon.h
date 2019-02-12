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

#ifndef     SRC_LEXICON_H_
#define     SRC_LEXICON_H_

#include <string>

#include "aff4/config.h"

/**
 * @file
 * @author scudette <scudette@google.com>
 * @date   Sun Jan 18 17:08:13 2015
 *
 * @brief This file defines attribute URNs of AFF4 object predicates. It
 *        standardizes on these attributes which must be interoperable to all
 *        AFF4 implementations.
 */

#include "aff4/rdf.h"

namespace aff4 {

#define LEXICON_DEFINE(x, y)                    \
    extern const char x[]

#include "lexicon.inc"

#undef LEXICON_DEFINE

const unsigned long AFF4_MAX_READ_LEN = 1024*1024*100;


// If is more efficient to use an enum for setting the compression type rather
// than compare URNs all the time.
typedef enum AFF4_IMAGE_COMPRESSION_ENUM_t {
    AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN = -1,
    AFF4_IMAGE_COMPRESSION_ENUM_STORED = 0,   // Not compressed.
    AFF4_IMAGE_COMPRESSION_ENUM_ZLIB = 1,     // Uses zlib.compress()
    AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY = 2,   // snappy.compress()
    AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE = 8,   // zlib.deflate()
    AFF4_IMAGE_COMPRESSION_ENUM_LZ4 = 16   // lz4
} AFF4_IMAGE_COMPRESSION_ENUM;

AFF4_IMAGE_COMPRESSION_ENUM CompressionMethodFromURN(URN method);
URN CompressionMethodToURN(AFF4_IMAGE_COMPRESSION_ENUM method);

/*
 * The below is a structured way of specifying the allowed AFF4 schemas for
 * different objects.
 */

/**
 * An attribute describes an allowed RDF name and type/
 *
 * @param name
 * @param type
 */
class Attribute {
  protected:
    std::string name;
    std::string type;
    std::string description;

    /// If this attribute may only take on certain values, this vector will
    /// contain the list of allowed values.
    std::unordered_map<std::string, std::string> allowed_values;

  public:
    Attribute() {}

    Attribute(std::string name, std::string type, std::string description):
        name(name), type(type), description(description) {}

    void AllowedValue(std::string alias, std::string value) {
        allowed_values[alias] = value;
    }
};


/**
 * A Schema describes allowed attributes for an AFF4 object type.
 *
 * @param object_type
 */
class Schema {
  protected:
    std::unordered_map<std::string, Attribute> attributes;
    std::string object_type;

    /// This schema inherits from these parents.
    std::vector<Schema> parents;

    static std::unordered_map<std::string, Schema> cache;

  public:
    Schema() {}

    Schema(std::string object_type): object_type(object_type) {}

    void AddAttribute(std::string alias, Attribute attribute) {
        attributes[alias] = attribute;
    }

    void AddParent(Schema parent) {
        parents.push_back(parent);
    }

    static Schema GetSchema(std::string object_type);
};

void aff4_lexicon_init();

} // namespace aff4

#endif  // SRC_LEXICON_H_
