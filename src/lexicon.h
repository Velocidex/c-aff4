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

/**
 * @file
 * @author scudette <scudette@google.com>
 * @date   Sun Jan 18 17:08:13 2015
 *
 * @brief This file defines attribute URNs of AFF4 object predicates. It
 *        standardizes on these attributes which must be interoperable to all
 *        AFF4 implementations.
 */


#include "rdf.h"

#define AFF4_VERSION "0.2"

#define AFF4_MAX_READ_LEN 1024*1024*100

#define AFF4_NAMESPACE "http://aff4.org/Schema#"
#define XSD_NAMESPACE "http://www.w3.org/2001/XMLSchema#"
#define RDF_NAMESPACE "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
#define AFF4_MEMORY_NAMESPACE "http://aff4.org/Schema#memory/"
#define AFF4_DISK_NAMESPACE "http://aff4.org/Schema#disk/"

// Attributes in this namespace will never be written to persistant
// storage. They are simply used as a way for storing metadata about an AFF4
// object internally.
#define AFF4_VOLATILE_NAMESPACE "http://aff4.org/VolatileSchema#"

// Commonly used RDF types.
#define URNType "URN"
#define XSDStringType (XSD_NAMESPACE "string")
#define RDFBytesType (XSD_NAMESPACE "hexBinary")
#define XSDIntegerType (XSD_NAMESPACE "integer")
#define XSDIntegerTypeInt (XSD_NAMESPACE "int")
#define XSDIntegerTypeLong (XSD_NAMESPACE "long")
#define XSDBooleanType (XSD_NAMESPACE "boolean")

/// Attribute names for different AFF4 objects.

/// Base AFF4Object
#define AFF4_TYPE (RDF_NAMESPACE "type")
#define AFF4_STORED (AFF4_NAMESPACE "stored")
#define AFF4_CONTAINS (AFF4_NAMESPACE "contains")

// Each container should have this file which contains the URN of the container.
#define AFF4_CONTAINER_DESCRIPTION "container.description"
#define AFF4_CONTAINER_INFO_TURTLE "information.turtle"
#define AFF4_CONTAINER_INFO_YAML "information.yaml"

/// AFF4 ZipFile containers.
#define AFF4_ZIP_TYPE (AFF4_NAMESPACE "zip_volume")

/// AFF4Stream
#define AFF4_STREAM_SIZE (AFF4_NAMESPACE "size")

// Can be "read", "truncate", "append"
#define AFF4_STREAM_WRITE_MODE (AFF4_VOLATILE_NAMESPACE "writable")

// FileBackedObjects are either marked explicitly or using the file:// scheme.
#define AFF4_FILE_TYPE (AFF4_NAMESPACE "file")

// file:// based URNs do not always have a direct mapping to filesystem
// paths. This volatile attribute is used to control the filename mapping.
#define AFF4_FILE_NAME (AFF4_VOLATILE_NAMESPACE "filename")

// The original filename the stream had.
#define AFF4_STREAM_ORIGINAL_FILENAME (AFF4_NAMESPACE "original_filename")

/// ZipFileSegment
#define AFF4_ZIP_SEGMENT_TYPE (AFF4_NAMESPACE "zip_segment")

/// AFF4Image - stores a stream using Bevies.
#define AFF4_IMAGE_TYPE (AFF4_NAMESPACE "image")
#define AFF4_IMAGE_CHUNK_SIZE (AFF4_NAMESPACE "chunk_size")
#define AFF4_IMAGE_CHUNKS_PER_SEGMENT (AFF4_NAMESPACE "chunks_per_segment")
#define AFF4_IMAGE_COMPRESSION (AFF4_NAMESPACE "compression")
#define AFF4_IMAGE_COMPRESSION_ZLIB "https://www.ietf.org/rfc/rfc1950.txt"
#define AFF4_IMAGE_COMPRESSION_SNAPPY "https://github.com/google/snappy"
#define AFF4_IMAGE_COMPRESSION_STORED (AFF4_NAMESPACE "compression/stored")

// AFF4Map - stores a mapping from one stream to another.
#define AFF4_MAP_TYPE (AFF4_NAMESPACE "map")

// Categories describe the general type of an image.
#define AFF4_CATEGORY (AFF4_NAMESPACE "category")

#define AFF4_MEMORY_PHYSICAL (AFF4_MEMORY_NAMESPACE "physical")
#define AFF4_MEMORY_VIRTUAL (AFF4_MEMORY_NAMESPACE "virtual")
#define AFF4_MEMORY_PAGEFILE (AFF4_MEMORY_NAMESPACE "pagefile")
#define AFF4_MEMORY_PAGEFILE_NUM (AFF4_MEMORY_NAMESPACE "pagefile_number")

#define AFF4_DISK_RAW (AFF4_DISK_NAMESPACE "raw")
#define AFF4_DISK_PARTITION (AFF4_DISK_NAMESPACE "partition")

#define AFF4_DIRECTORY_TYPE (AFF4_NAMESPACE "directory")

// The constant stream is a psuedo stream which just returns a constant.
#define AFF4_CONSTANT_TYPE (AFF4_NAMESPACE "constant")

// The constant to repeat (default 0).
#define AFF4_CONSTANT_CHAR (AFF4_NAMESPACE "constant_char")


// An AFF4 Directory stores all members as files on the filesystem. Some
// filesystems can not represent the URNs properly, hence we need a mapping
// between the URN and the filename. This attribute stores the _relative_ path
// of the filename for the member URN relative to the container's path.
#define AFF4_DIRECTORY_CHILD_FILENAME (AFF4_NAMESPACE "directory/filename")

// If is more efficient to use an enum for setting the compression type rather
// than compare URNs all the time.
typedef enum AFF4_IMAGE_COMPRESSION_ENUM_t {
  AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN = -1,
  AFF4_IMAGE_COMPRESSION_ENUM_STORED = 0,   // Not compressed.
  AFF4_IMAGE_COMPRESSION_ENUM_ZLIB = 1,     // Uses zlib.compress()
  AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY = 2,   // snappy.compress()
  AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE = 8   // zlib.deflate()
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
  string name;
  string type;
  string description;

  /// If this attribute may only take on certain values, this vector will
  /// contain the list of allowed values.
  std::unordered_map<string, string> allowed_values;

 public:
  Attribute() {}

  Attribute(string name, string type, string description):
      name(name), type(type), description(description) {}

  void AllowedValue(string alias, string value) {
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
  std::unordered_map<string, Attribute> attributes;
  string object_type;

  /// This schema inherits from these parents.
  vector<Schema> parents;

  static std::unordered_map<string, Schema> cache;

 public:
  Schema() {}

  Schema(string object_type): object_type(object_type) {}
  void AddAttribute(string alias, Attribute attribute) {
    attributes[alias] = attribute;
  }

  void AddParent(Schema parent) {
    parents.push_back(parent);
  }

  static Schema GetSchema(string object_type);
};


#endif  // SRC_LEXICON_H_
