// This file defines the AFF4 Lexicon.

/* NOTE: The LEXICON_DEFINE macro expands to extern const std::string
in headers */

LEXICON_DEFINE(AFF4_VERSION, "1.0");
LEXICON_DEFINE(AFF4_VERSION_MAJOR, "1");
LEXICON_DEFINE(AFF4_VERSION_MINOR, "0");
LEXICON_DEFINE(AFF4_TOOL, "libaff4");

LEXICON_DEFINE(AFF4_NAMESPACE, "http://aff4.org/Schema#");
LEXICON_DEFINE(XSD_NAMESPACE, "http://www.w3.org/2001/XMLSchema#");
LEXICON_DEFINE(RDF_NAMESPACE, "http://www.w3.org/1999/02/22-rdf-syntax-ns#");
LEXICON_DEFINE(AFF4_DISK_NAMESPACE, "http://aff4.org/Schema#disk/");

LEXICON_DEFINE(AFF4_LEGACY_NAMESPACE, "http://afflib.org/2009/aff4#");

/**
 * The default AFF4 prefix for AFF4 objects.
 */
LEXICON_DEFINE(AFF4_PREFIX, "aff4://");

// Attributes in this namespace will never be written to persistant
// storage. They are simply used as a way for storing metadata about an AFF4
// object internally.
LEXICON_DEFINE(AFF4_VOLATILE_NAMESPACE, "http://aff4.org/VolatileSchema#");

// Commonly used RDF types.
LEXICON_DEFINE(URNType, "URN");
LEXICON_DEFINE(XSDStringType, XSD_NAMESPACE + "string");
LEXICON_DEFINE(RDFBytesType, XSD_NAMESPACE + "hexBinary");
LEXICON_DEFINE(XSDIntegerType, XSD_NAMESPACE + "integer");
LEXICON_DEFINE(XSDIntegerTypeInt, XSD_NAMESPACE + "int");
LEXICON_DEFINE(XSDIntegerTypeLong, XSD_NAMESPACE + "long");
LEXICON_DEFINE(XSDBooleanType, XSD_NAMESPACE + "boolean");

/// Attribute names for different AFF4 objects.

/// Base AFF4Object
LEXICON_DEFINE(AFF4_TYPE, RDF_NAMESPACE + "type");
/**
 * Defines the URN where this object is stored.
 */
LEXICON_DEFINE(AFF4_STORED, AFF4_NAMESPACE + "stored");
LEXICON_DEFINE(AFF4_LEGACY_STORED, AFF4_LEGACY_NAMESPACE + "stored");
/**
 * Defines the URNs of any child objects.
 */
LEXICON_DEFINE(AFF4_CONTAINS, AFF4_NAMESPACE + "contains");
/**
 * Defines the URN that this object is a child of.
 */
LEXICON_DEFINE(AFF4_TARGET, AFF4_NAMESPACE + "target");

/*
 * Each container should have this file which contains the URN of the container.
 */
LEXICON_DEFINE(AFF4_CONTAINER_DESCRIPTION, "container.description");
/**
 * Defines the default name for the information turtle file.
 */
LEXICON_DEFINE(AFF4_CONTAINER_INFO_TURTLE, "information.turtle");
LEXICON_DEFINE(AFF4_CONTAINER_INFO_YAML, "information.yaml");
/**
 * Each AFF4 container should have this file to denote the AFF4 standard which this container is using.
 */
LEXICON_DEFINE(AFF4_CONTAINER_VERSION_TXT, "version.txt");

/// AFF4 ZipFile containers.
LEXICON_DEFINE(AFF4_ZIP_TYPE, AFF4_NAMESPACE + "ZipVolume");



// Can be "read", "truncate", "append"
LEXICON_DEFINE(AFF4_STREAM_WRITE_MODE, AFF4_VOLATILE_NAMESPACE + "writable");

// FileBackedObjects are either marked explicitly or using the file:// scheme.
LEXICON_DEFINE(AFF4_FILE_TYPE, AFF4_NAMESPACE + "file");

// file:// based URNs do not always have a direct mapping to filesystem
// paths. This volatile attribute is used to control the filename mapping.
LEXICON_DEFINE(AFF4_FILE_NAME, AFF4_VOLATILE_NAMESPACE + "filename");

// The original filename the stream had.
LEXICON_DEFINE(AFF4_STREAM_ORIGINAL_FILENAME, AFF4_NAMESPACE + "original_filename");

/// ZipFileSegment
LEXICON_DEFINE(AFF4_ZIP_SEGMENT_TYPE, AFF4_NAMESPACE + "zip_segment");

/**
 * AFF4 Primary Object Types.
 *
 * Note: These types are abstract entities which refer to a logical
 * image. They do not say anything about the actual storage methos
 * used. Instead they refer to the AFF4 stream which stores the data
 * in their dataStream attribute.
 *
 * Since a single AFF4 URN may represent several different types,
 * these types may also apply to a concrete object, in which case no
 * aff4:dataStream is needed.
 */
LEXICON_DEFINE(AFF4_IMAGE_TYPE, AFF4_NAMESPACE + "Image");
LEXICON_DEFINE(AFF4_DISK_IMAGE_TYPE, AFF4_NAMESPACE + "DiskImage");
LEXICON_DEFINE(AFF4_VOLUME_IMAGE_TYPE, AFF4_NAMESPACE + "VolumeImage");
LEXICON_DEFINE(AFF4_MEMORY_IMAGE_TYPE, AFF4_NAMESPACE + "MemoryImage");
LEXICON_DEFINE(AFF4_CONTIGUOUS_IMAGE_TYPE, AFF4_NAMESPACE + "ContiguousImage");
LEXICON_DEFINE(AFF4_DISCONTIGUOUS_IMAGE_TYPE, AFF4_NAMESPACE + "DiscontiguousImage");

/**
 * AFF4 Evimetry Legacy Object Types.
 * The aff4:Image may contain device information, and MUST contain the size and reference to the map, via
 * aff4:dataStream
 */
LEXICON_DEFINE(AFF4_LEGACY_IMAGE_TYPE, AFF4_LEGACY_NAMESPACE + "Image");
LEXICON_DEFINE(AFF4_LEGACY_DISK_IMAGE_TYPE, AFF4_LEGACY_NAMESPACE + "device");
LEXICON_DEFINE(AFF4_LEGACY_VOLUME_IMAGE_TYPE, AFF4_LEGACY_NAMESPACE + "volume");
LEXICON_DEFINE(AFF4_LEGACY_CONTIGUOUS_IMAGE_TYPE, AFF4_LEGACY_NAMESPACE + "contiguousMap");

/**
 * AFF4 Map Stream
 * The aff4:Map stream will contain the hash digests, and dataStream references to the lower streams.
 */
LEXICON_DEFINE(AFF4_MAP_TYPE, AFF4_NAMESPACE + "Map");
LEXICON_DEFINE(AFF4_LEGACY_MAP_TYPE, AFF4_LEGACY_NAMESPACE + "map");
/**
 * The AFF4 Stream to use when filling in gaps for aff4:DiscontiguousImage
 */
LEXICON_DEFINE(AFF4_MAP_GAP_STREAM, AFF4_NAMESPACE + "mapGapDefaultStream");

/**
 * AFF4 Data Stream
 */
LEXICON_DEFINE(AFF4_IMAGESTREAM_TYPE, AFF4_NAMESPACE + "ImageStream");
LEXICON_DEFINE(AFF4_DATASTREAM, AFF4_NAMESPACE + "dataStream");
LEXICON_DEFINE(AFF4_STREAM_SIZE, AFF4_NAMESPACE + "size");
LEXICON_DEFINE(AFF4_STREAM_CHUNK_SIZE, AFF4_NAMESPACE + "chunkSize");
LEXICON_DEFINE(AFF4_STREAM_VERSION, AFF4_NAMESPACE + "version");
LEXICON_DEFINE(AFF4_STREAM_CHUNKS_PER_SEGMENT, AFF4_NAMESPACE + "chunksInSegment");

/**
 * AFF4 Evimetry Legacy Data Stream
 */
LEXICON_DEFINE(AFF4_LEGACY_IMAGESTREAM_TYPE, AFF4_LEGACY_NAMESPACE + "stream");
LEXICON_DEFINE(AFF4_LEGACY_DATASTREAM, AFF4_LEGACY_NAMESPACE + "dataStream");
LEXICON_DEFINE(AFF4_LEGACY_STREAM_SIZE, AFF4_LEGACY_NAMESPACE + "size");
LEXICON_DEFINE(AFF4_LEGACY_STREAM_CHUNK_SIZE, AFF4_LEGACY_NAMESPACE + "chunk_size");
LEXICON_DEFINE(AFF4_LEGACY_STREAM_VERSION, AFF4_LEGACY_NAMESPACE + "version");
LEXICON_DEFINE(AFF4_LEGACY_STREAM_CHUNKS_PER_SEGMENT, AFF4_LEGACY_NAMESPACE + "chunks_in_segment");

/**
 * Compression Methods.
 */
LEXICON_DEFINE(AFF4_IMAGE_COMPRESSION, AFF4_NAMESPACE + "compressionMethod");
LEXICON_DEFINE(AFF4_LEGACY_IMAGE_COMPRESSION, AFF4_LEGACY_NAMESPACE + "CompressionMethod");
LEXICON_DEFINE(AFF4_IMAGE_COMPRESSION_ZLIB, "https://www.ietf.org/rfc/rfc1950.txt");
LEXICON_DEFINE(AFF4_IMAGE_COMPRESSION_DEFLATE, "https://tools.ietf.org/html/rfc1951");
LEXICON_DEFINE(AFF4_IMAGE_COMPRESSION_SNAPPY, "http://code.google.com/p/snappy/");
LEXICON_DEFINE(AFF4_IMAGE_COMPRESSION_SNAPPY2, "https://github.com/google/snappy");
LEXICON_DEFINE(AFF4_IMAGE_COMPRESSION_LZ4, "https://code.google.com/p/lz4/");
LEXICON_DEFINE(AFF4_IMAGE_COMPRESSION_STORED, AFF4_NAMESPACE + "NullCompressor");
LEXICON_DEFINE(AFF4_LEGACY_IMAGE_COMPRESSION_STORED, AFF4_LEGACY_NAMESPACE + "nullCompressor");

/**
 * Default namespace for symbolic streams.
 */
LEXICON_DEFINE(AFF4_IMAGESTREAM_ZERO, AFF4_NAMESPACE + "Zero");
LEXICON_DEFINE(AFF4_IMAGESTREAM_FF, AFF4_NAMESPACE + "FFDevice");
LEXICON_DEFINE(AFF4_IMAGESTREAM_UNKNOWN, AFF4_NAMESPACE + "UnknownData");
LEXICON_DEFINE(AFF4_IMAGESTREAM_UNREADABLE, AFF4_NAMESPACE + "UnreadableData");
LEXICON_DEFINE(AFF4_IMAGESTREAM_SYMBOLIC_PREFIX, AFF4_NAMESPACE + "SymbolicStream");

LEXICON_DEFINE(AFF4_LEGACY_IMAGESTREAM_ZERO, AFF4_LEGACY_NAMESPACE + "Zero");
LEXICON_DEFINE(AFF4_LEGACY_IMAGESTREAM_FF, AFF4_LEGACY_NAMESPACE + "FF");
LEXICON_DEFINE(AFF4_LEGACY_IMAGESTREAM_UNKNOWN, AFF4_LEGACY_NAMESPACE + "UnknownData");
LEXICON_DEFINE(AFF4_LEGACY_IMAGESTREAM_UNREADABLE, AFF4_LEGACY_NAMESPACE + "UnreadableData");
LEXICON_DEFINE(AFF4_LEGACY_IMAGESTREAM_SYMBOLIC_PREFIX, "http://afflib.org/2012/SymbolicStream#");

// AFF4Map - stores a mapping from one stream to another.


// Categories describe the general type of an image.
LEXICON_DEFINE(AFF4_CATEGORY, AFF4_NAMESPACE + "category");
LEXICON_DEFINE(AFF4_MEMORY_NAMESPACE, "http://aff4.org/Schema#memory/");
LEXICON_DEFINE(AFF4_MEMORY_PHYSICAL, AFF4_MEMORY_NAMESPACE + "physical");
LEXICON_DEFINE(AFF4_MEMORY_VIRTUAL, AFF4_MEMORY_NAMESPACE + "virtual");
LEXICON_DEFINE(AFF4_MEMORY_PAGEFILE, AFF4_MEMORY_NAMESPACE + "pagefile");
LEXICON_DEFINE(AFF4_MEMORY_PAGEFILE_NUM, AFF4_MEMORY_NAMESPACE + "pagefile_number");

LEXICON_DEFINE(AFF4_DISK_RAW, AFF4_DISK_NAMESPACE + "raw");
LEXICON_DEFINE(AFF4_DISK_PARTITION, AFF4_DISK_NAMESPACE + "partition");


// The constant stream is a psuedo stream which just returns a constant.
LEXICON_DEFINE(AFF4_CONSTANT_TYPE, AFF4_NAMESPACE + "constant");

// The constant to repeat, default 0).
LEXICON_DEFINE(AFF4_CONSTANT_CHAR, AFF4_NAMESPACE + "constant_char");

LEXICON_DEFINE(AFF4_DIRECTORY_TYPE, AFF4_NAMESPACE + "directory");

// An AFF4 Directory stores all members as files on the filesystem. Some
// filesystems can not represent the URNs properly, hence we need a mapping
// between the URN and the filename. This attribute stores the _relative_ path
// of the filename for the member URN relative to the container's path.
LEXICON_DEFINE(AFF4_DIRECTORY_CHILD_FILENAME, AFF4_NAMESPACE + "directory/filename");
