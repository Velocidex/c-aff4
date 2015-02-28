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

#ifndef     AFF4_LEXICON_H_
#define     AFF4_LEXICON_H_

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

#define AFF4_VERSION "0.1"

#define AFF4_MAX_READ_LEN 1024*1024*100

//#define AFF4_NAMESPACE "http://aff4.org/Schema#"
#define AFF4_NAMESPACE "http://aff4.org/Schema#"
#define XSD_NAMESPACE "http://www.w3.org/2001/XMLSchema#"
#define RDF_NAMESPACE "http://www.w3.org/1999/02/22-rdf-syntax-ns#"

// Attributes in this namespace will never be written to persistant
// storage. They are simply used as a way for storing metadata about an AFF4
// object internally.
#define AFF4_VOLATILE_NAMESPACE "http://aff4.org/VolatileSchema#"

/// Attribute names for different AFF4 objects.
/// Base AFF4Object
#define AFF4_TYPE (RDF_NAMESPACE "type")
#define AFF4_STORED (AFF4_NAMESPACE "stored")

/// AFF4 ZipFile containers.
#define AFF4_ZIP_TYPE (AFF4_NAMESPACE "zip_volume")

/// AFF4Stream
#define AFF4_STREAM_SIZE (AFF4_NAMESPACE "size")

// Can be "read", "truncate", "append"
#define AFF4_STREAM_WRITE_MODE (AFF4_VOLATILE_NAMESPACE "writable")

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

//AFF4Map - stores a mapping from one stream to another.
#define AFF4_MAP_TYPE (AFF4_NAMESPACE "map")

// Categories describe the general type of an image.
#define AFF4_CATEGORY (AFF4_NAMESPACE "category")

// These represent standard attributes to describe memory forensics images.
#define AFF4_MEMORY_NAMESPACE AFF4_NAMESPACE "memory/"
#define AFF4_DISK_NAMESPACE AFF4_NAMESPACE "disk/"

#define AFF4_MEMORY_PHYSICAL (AFF4_MEMORY_NAMESPACE "physical")
#define AFF4_MEMORY_VIRTUAL (AFF4_MEMORY_NAMESPACE "virtual")

#define AFF4_DISK_RAW (AFF4_DISK_NAMESPACE "raw")
#define AFF4_DISK_PARTITION (AFF4_DISK_NAMESPACE "partition")



// If is more efficient to use an enum for setting the compression type rather
// than compare URNs all the time.
typedef enum AFF4_IMAGE_COMPRESSION_ENUM_t {
  AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN,
  AFF4_IMAGE_COMPRESSION_ENUM_STORED,
  AFF4_IMAGE_COMPRESSION_ENUM_ZLIB,
  AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY
} AFF4_IMAGE_COMPRESSION_ENUM;

AFF4_IMAGE_COMPRESSION_ENUM CompressionMethodFromURN(URN method);
URN CompressionMethodToURN(AFF4_IMAGE_COMPRESSION_ENUM method);

#endif
