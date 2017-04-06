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

#include "config.h"

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

#define AFF4_VERSION "1.0"

#define AFF4_VERSION_MAJOR "1"
#define AFF4_VERSION_MINOR "0"
#define AFF4_TOOL "libaff4"

#define AFF4_MAX_READ_LEN 1024*1024*100

#define AFF4_NAMESPACE "http://aff4.org/Schema#"
#define XSD_NAMESPACE "http://www.w3.org/2001/XMLSchema#"
#define RDF_NAMESPACE "http://www.w3.org/1999/02/22-rdf-syntax-ns#"

#define AFF4_LEGACY_NAMESPACE "http://afflib.org/2009/aff4#"

/**
 * The default AFF4 prefix for AFF4 objects.
 */
#define AFF4_PREFIX "aff4://"

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
/**
 * Defines the URN where this object is stored.
 */
#define AFF4_STORED (AFF4_NAMESPACE "stored")
#define AFF4_LEGACY_STORED (AFF4_LEGACY_NAMESPACE "stored")
/**
 * Defines the URNs of any child objects.
 */
#define AFF4_CONTAINS (AFF4_NAMESPACE "contains")
/**
 * Defines the URN that this object is a child of.
 */
#define AFF4_TARGET (AFF4_NAMESPACE "target")

/*
 * Each container should have this file which contains the URN of the container.
 */
#define AFF4_CONTAINER_DESCRIPTION "container.description"
/**
 * Defines the default name for the information turtle file.
 */
#define AFF4_CONTAINER_INFO_TURTLE "information.turtle"
#define AFF4_CONTAINER_INFO_YAML "information.yaml"
/**
 * Each AFF4 container should have this file to denote the AFF4 standard which this container is using.
 */
#define AFF4_CONTAINER_VERSION_TXT "version.txt"

/// AFF4 ZipFile containers.
#define AFF4_ZIP_TYPE (AFF4_NAMESPACE "ZipVolume")



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

/**
 * AFF4 Primary Object Types.
 * The aff4:Image may contain device information, and MUST contain the size and reference to the map (via
 * aff4:dataStream
 */
#define AFF4_IMAGE_TYPE (AFF4_NAMESPACE "Image")
#define AFF4_DISK_IMAGE_TYPE (AFF4_NAMESPACE "DiskImage")
#define AFF4_VOLUME_IMAGE_TYPE (AFF4_NAMESPACE "VolumeImage")
#define AFF4_MEMORY_IMAGE_TYPE (AFF4_NAMESPACE "MemoryImage")
#define AFF4_CONTIGUOUS_IMAGE_TYPE (AFF4_NAMESPACE "ContiguousImage")
#define AFF4_DISCONTIGUOUS_IMAGE_TYPE (AFF4_NAMESPACE "DiscontiguousImage")

/**
 * AFF4 Evimetry Legacy Object Types.
 * The aff4:Image may contain device information, and MUST contain the size and reference to the map (via
 * aff4:dataStream
 */
#define AFF4_LEGACY_IMAGE_TYPE (AFF4_LEGACY_NAMESPACE "Image")
#define AFF4_LEGACY_DISK_IMAGE_TYPE (AFF4_LEGACY_NAMESPACE "device")
#define AFF4_LEGACY_VOLUME_IMAGE_TYPE (AFF4_LEGACY_NAMESPACE "volume")
#define AFF4_LEGACY_CONTIGUOUS_IMAGE_TYPE (AFF4_LEGACY_NAMESPACE "contiguousMap")

/**
 * AFF4 Map Stream
 * The aff4:Map stream will contain the hash digests, and dataStream references to the lower streams.
 */
#define AFF4_MAP_TYPE (AFF4_NAMESPACE "Map")
#define AFF4_LEGACY_MAP_TYPE (AFF4_LEGACY_NAMESPACE "map")
/**
 * The AFF4 Stream to use when filling in gaps for aff4:DiscontiguousImage
 */
#define AFF4_MAP_GAP_STREAM (AFF4_NAMESPACE "mapGapDefaultStream")

/**
 * AFF4 Data Stream
 */
#define AFF4_IMAGESTREAM_TYPE (AFF4_NAMESPACE "ImageStream")
#define AFF4_DATASTREAM (AFF4_NAMESPACE "dataStream")
#define AFF4_STREAM_SIZE (AFF4_NAMESPACE "size")
#define AFF4_STREAM_CHUNK_SIZE (AFF4_NAMESPACE "chunkSize")
#define AFF4_STREAM_VERSION (AFF4_NAMESPACE "version")
#define AFF4_STREAM_CHUNKS_PER_SEGMENT (AFF4_NAMESPACE "chunksInSegment")

/**
 * AFF4 Evimetry Legacy Data Stream
 */
#define AFF4_LEGACY_IMAGESTREAM_TYPE (AFF4_LEGACY_NAMESPACE "stream")
#define AFF4_LEGACY_DATASTREAM (AFF4_LEGACY_NAMESPACE "dataStream")
#define AFF4_LEGACY_STREAM_SIZE (AFF4_LEGACY_NAMESPACE "size")
#define AFF4_LEGACY_STREAM_CHUNK_SIZE (AFF4_LEGACY_NAMESPACE "chunk_size")
#define AFF4_LEGACY_STREAM_VERSION (AFF4_LEGACY_NAMESPACE "version")
#define AFF4_LEGACY_STREAM_CHUNKS_PER_SEGMENT (AFF4_LEGACY_NAMESPACE "chunks_in_segment")

/**
 * Compression Methods.
 */
#define AFF4_IMAGE_COMPRESSION (AFF4_NAMESPACE "compressionMethod")
#define AFF4_LEGACY_IMAGE_COMPRESSION (AFF4_LEGACY_NAMESPACE "CompressionMethod")
#define AFF4_IMAGE_COMPRESSION_ZLIB "https://www.ietf.org/rfc/rfc1950.txt"
#define AFF4_IMAGE_COMPRESSION_DEFLATE "https://tools.ietf.org/html/rfc1951"
#define AFF4_IMAGE_COMPRESSION_SNAPPY "http://code.google.com/p/snappy/"
#define AFF4_IMAGE_COMPRESSION_SNAPPY2 "https://github.com/google/snappy"
#define AFF4_IMAGE_COMPRESSION_LZ4 "https://code.google.com/p/lz4/"
#define AFF4_IMAGE_COMPRESSION_STORED (AFF4_NAMESPACE "NullCompressor")
#define AFF4_LEGACY_IMAGE_COMPRESSION_STORED (AFF4_LEGACY_NAMESPACE "nullCompressor")

/**
 * Default namespace for symbolic streams.
 */
#define AFF4_IMAGESTREAM_ZERO (AFF4_NAMESPACE "Zero")
#define AFF4_IMAGESTREAM_FF (AFF4_NAMESPACE "FFDevice")
#define AFF4_IMAGESTREAM_UNKNOWN (AFF4_NAMESPACE "UnknownData")
#define AFF4_IMAGESTREAM_UNREADABLE (AFF4_NAMESPACE "UnreadableData")
#define AFF4_IMAGESTREAM_SYMBOLIC_PREFIX (AFF4_NAMESPACE "SymbolicStream")

#define AFF4_LEGACY_IMAGESTREAM_ZERO (AFF4_LEGACY_NAMESPACE "Zero")
#define AFF4_LEGACY_IMAGESTREAM_FF (AFF4_LEGACY_NAMESPACE "FF")
#define AFF4_LEGACY_IMAGESTREAM_UNKNOWN (AFF4_LEGACY_NAMESPACE "UnknownData")
#define AFF4_LEGACY_IMAGESTREAM_UNREADABLE (AFF4_LEGACY_NAMESPACE "UnreadableData")
#define AFF4_LEGACY_IMAGESTREAM_SYMBOLIC_PREFIX ("http://afflib.org/2012/SymbolicStream#")

// AFF4Map - stores a mapping from one stream to another.


// Categories describe the general type of an image.
/*#define AFF4_CATEGORY (AFF4_NAMESPACE "category")

#define AFF4_MEMORY_PHYSICAL (AFF4_MEMORY_NAMESPACE "physical")
#define AFF4_MEMORY_VIRTUAL (AFF4_MEMORY_NAMESPACE "virtual")
#define AFF4_MEMORY_PAGEFILE (AFF4_MEMORY_NAMESPACE "pagefile")
#define AFF4_MEMORY_PAGEFILE_NUM (AFF4_MEMORY_NAMESPACE "pagefile_number")

#define AFF4_DISK_RAW (AFF4_DISK_NAMESPACE "raw")
#define AFF4_DISK_PARTITION (AFF4_DISK_NAMESPACE "partition")



// The constant stream is a psuedo stream which just returns a constant.
#define AFF4_CONSTANT_TYPE (AFF4_NAMESPACE "constant")

// The constant to repeat (default 0).
#define AFF4_CONSTANT_CHAR (AFF4_NAMESPACE "constant_char")
*/

#define AFF4_DIRECTORY_TYPE (AFF4_NAMESPACE "directory")

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
    AFF4_IMAGE_COMPRESSION_ENUM_DEFLATE = 8,   // zlib.deflate()
    AFF4_IMAGE_COMPRESSION_ENUM_LZ4 = 16   // lz4 (coming soon).
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


#endif  // SRC_LEXICON_H_
