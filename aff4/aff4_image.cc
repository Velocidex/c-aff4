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
#include "aff4/aff4_image.h"
#include "aff4/libaff4.h"
#include <zlib.h>
#include <snappy.h>
#include <lz4.h>
#include "aff4/aff4_utils.h"

namespace aff4 {


AFF4Status CompressZlib_(const std::string &input, std::string* output) {
    uLongf c_length = compressBound(input.size()) + 1;
    output->resize(c_length);

    if (compress2(reinterpret_cast<Bytef*>(const_cast<char*>(output->data())),
                  &c_length,
                  reinterpret_cast<Bytef*>(const_cast<char*>(input.data())),
                  input.size(), 1) != Z_OK) {
        return MEMORY_ERROR;
    }

    output->resize(c_length);

    return STATUS_OK;
}

AFF4Status DeCompressZlib_(const std::string &input, std::string* output) {
    uLongf buffer_size = output->size();

    if (uncompress(reinterpret_cast<Bytef*>(const_cast<char*>(output->data())),
                   &buffer_size,
                   (const Bytef*)input.data(), input.size()) == Z_OK) {
        output->resize(buffer_size);
        return STATUS_OK;
    }

    return IO_ERROR;
}


AFF4Status CompressSnappy_(const std::string &input, std::string* output) {
    snappy::Compress(input.data(), input.size(), output);

    return STATUS_OK;
}


AFF4Status DeCompressSnappy_(const std::string &input, std::string* output) {
    if (!snappy::Uncompress(input.data(), input.size(), output)) {
        return GENERIC_ERROR;
    }

    return STATUS_OK;
}

AFF4Status CompressLZ4_(const std::string &input, std::string* output) {
    output->resize(LZ4_compressBound(input.size()));

    int size = LZ4_compress_default(input.data(), const_cast<char *>(output->data()),
                                    input.size(), output->size());
    if (size == 0) {
        return GENERIC_ERROR;
    }

    output->resize(size);

    return STATUS_OK;
}


AFF4Status DeCompressLZ4_(const std::string &input, std::string* output) {
    int size = LZ4_decompress_safe(input.data(), const_cast<char *>(output->data()),
                                   input.size(), output->size());
    if (size == 0) {
        return GENERIC_ERROR;
    }

    output->resize(size);

    return STATUS_OK;
}


class _BevyWriter {
public:
    _BevyWriter(DataStore *resolver,
                AFF4_IMAGE_COMPRESSION_ENUM compression,
                size_t chunk_size, int chunks_per_segment)
        : resolver(resolver),
          compression(compression), chunk_size(chunk_size),
          bevy_index_data(chunks_per_segment + 1),
          chunks_per_segment(chunks_per_segment) {
        bevy.buffer.reserve(chunk_size * chunks_per_segment);
    }

    AFF4Stream &bevy_stream() {
        return bevy;
    }

    // Generate the index stream.
    std::string index_stream() {
        std::string result;
        result.reserve(chunks_per_segment * sizeof(BevyIndex));

        for(const BevyIndex& index_entry: bevy_index_data) {
            // The first empty chunk means the end of the chunks. This
            // happens if the bevy is not full.
            if (index_entry.length == 0) break;

            result += std::string(
                reinterpret_cast<const char *>(&index_entry),
                sizeof(index_entry));
        }

        return result;
    }

    // Receives a single chunk's data. The chunk is compressed on the
    // thread pool and concatenated to the bevy. NOTE: Since this is
    // done asyncronously, the chunks are not stored in the bevy
    // contiguously. Instead, the index is sorted by chunk id and
    // refer to chunks in the bevy in any order.
    void EnqueueCompressChunk(int chunk_id, const std::string& data) {
        results.push_back(
            resolver->pool->enqueue(
                [this, chunk_id, data]() {
                    return _CompressChunk(chunk_id, data);
                }));
    }

    int chunks_written() {
        std::unique_lock<std::mutex> lock(mutex);
        return chunks_written_;
    }

    AFF4Status Finalize() {
        for (auto& result: results) {
            RETURN_IF_ERROR(result.get());
        }
        results.clear();
        return STATUS_OK;
    }

private:
    std::mutex mutex;
    StringIO bevy;
    DataStore *resolver;
    AFF4_IMAGE_COMPRESSION_ENUM compression;
    size_t chunk_size;
    std::vector<BevyIndex> bevy_index_data;
    size_t chunks_per_segment;

    // A counter of how many chunks were written.
    int chunks_written_ = 0;

    std::vector<std::future<AFF4Status>> results;

    AFF4Status _CompressChunk(int chunk_id, const std::string data) {
        std::string c_data;

        switch (compression) {
        case AFF4_IMAGE_COMPRESSION_ENUM_ZLIB: {
            RETURN_IF_ERROR(CompressZlib_(data, &c_data));
        }
            break;

        case AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY: {
            RETURN_IF_ERROR(CompressSnappy_(data, &c_data));
        }
            break;

        case AFF4_IMAGE_COMPRESSION_ENUM_LZ4: {
            RETURN_IF_ERROR(CompressLZ4_(data, &c_data));
        }
            break;

        case AFF4_IMAGE_COMPRESSION_ENUM_STORED: {
            c_data = data;
        }
            break;

        // Should never happen because the object should never accept this
        // compression URN.
        default:
            resolver->logger->critical("Unexpected compression type set {}",
                                       compression);
            return NOT_IMPLEMENTED;
        }

        RETURN_IF_ERROR(WriteChunk(data, c_data, chunk_id));
        return STATUS_OK;
    }


    AFF4Status WriteChunk(const std::string &data,
                          const std::string &c_data,
                          uint32_t chunk_id) {
        if (chunk_id > chunks_per_segment) {
            return IO_ERROR;
        }

        std::unique_lock<std::mutex> lock(mutex);

        BevyIndex &index = bevy_index_data[chunk_id];
        index.offset = bevy.Tell();

        // If by attempting to compress the chunk, we actually made it
        // bigger, we just store the chunk uncompressed. The
        // decompressor can figure that this is uncompressed by
        // comparing the chunk size to the compressed chunk size.

        // 3.2 Compression Storage of uncompressed chunks is supported
        //    by thise simple principle that if len(chunk) ==
        //    aff4:chunk_size then it is a stored chunk. Compression
        //    is not applied to stored chunks.

        // Chunk is compressible - store it compressed.
        if (c_data.size() < chunk_size - 16) {
            index.length = c_data.size();
            RETURN_IF_ERROR(bevy.Write(c_data));

            // Chunk is not compressible enough, store it uncompressed.
        } else {
            index.length = data.size();
            RETURN_IF_ERROR(bevy.Write(data));
        }
        chunks_written_++;

        return STATUS_OK;
    }
};



// A private class which manages image stream.
class _CompressorStream: public AFF4Stream {
  private:
    AFF4Stream* source;

    aff4_off_t initial_offset;
    size_t chunk_size;
    int chunks_per_segment;

  public:
    _BevyWriter bevy_writer;

    _CompressorStream(DataStore* resolver,
                      AFF4_IMAGE_COMPRESSION_ENUM compression,
                      size_t chunk_size,
                      int chunks_per_segment, AFF4Stream* source):
        AFF4Stream(resolver), source(source), initial_offset(source->Tell()),
        chunk_size(chunk_size), chunks_per_segment(chunks_per_segment),
        bevy_writer(resolver, compression, chunk_size,
                    chunks_per_segment) {}

    // Populate the entire bevy into the writer at once.
    AFF4Status PrepareBevy() {
        for (int chunk_id = 0 ; chunk_id < chunks_per_segment; chunk_id++) {
            const std::string data = source->Read(chunk_size);

            // Ran out of source data - we are done early.
            if (data.size() == 0) {
                break;
            }
            size += data.size();
            bevy_writer.EnqueueCompressChunk(chunk_id, data);
        }
        RETURN_IF_ERROR(bevy_writer.Finalize());
        return bevy_writer.bevy_stream().Seek(0, SEEK_SET);
    }

    // The progress reporting calls us to figure out how much work we
    // actually did. Since we compress asynchronously, a better
    // measure of how far along we are is to report how many chunks we
    // finished compressing right now.
    aff4_off_t Tell() override {
        return initial_offset + bevy_writer.chunks_written() * chunk_size;
    };


    // This stream returns an entire compressed bevy from any read
    // request.  We just read a full bevu worth of chunks from our
    // source stream, compress it and return the compressed data. On
    // the next read, we return an empty string which indicates the
    // bevy is complete. This allows us to seamlessly hook this class
    // into WriteStream() calls into the Bevy segment.
    std::string Read(size_t length) override {
        auto& bevy_stream = bevy_writer.bevy_stream();
        return bevy_stream.Read(length);
    };

    virtual ~_CompressorStream() {}
};



AFF4ScopedPtr<AFF4Image> AFF4Image::NewAFF4Image(
    DataStore* resolver, const URN& image_urn, const URN& volume_urn) {
    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
        volume_urn);

    if (!volume) {
        return AFF4ScopedPtr<AFF4Image>();    /** Volume not known? */
    }

    // Inform the volume that we have a new image stream contained within it.
    volume->children.insert(image_urn.SerializeToString());

    resolver->Set(image_urn, AFF4_TYPE, new URN(AFF4_IMAGESTREAM_TYPE),
                  /* replace = */ false);
    resolver->Set(image_urn, AFF4_STORED, new URN(volume_urn));
    if(!resolver->HasURNWithAttribute(image_urn, AFF4_STREAM_SIZE)) {
        resolver->Set(image_urn, AFF4_STREAM_SIZE, new XSDInteger((uint64_t)0));
    }

    // We need to use the resolver here instead of just making a new object, in
    // case the object already exists. If it is already known we will get the
    // existing object from the cache through the factory. If the object does not
    // already exist, then the factory will make a new one.
    return resolver->AFF4FactoryOpen<AFF4Image>(image_urn);
}


/**
 * Initializes this AFF4 object from the information stored in the resolver.
 *
 *
 * @return STATUS_OK if the object was successfully initialized.
 */
AFF4Status AFF4Image::LoadFromURN() {
    URN volume_urn;

    if (resolver->Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
        RETURN_IF_ERROR(resolver->Get(urn, AFF4_LEGACY_STORED, volume_urn));
    }

    // Determine if this is an AFF4:ImageStream (AFF4 Standard) or
    // a aff4:stream (AFF4 Legacy)
    URN rdfType (AFF4_LEGACY_IMAGESTREAM_TYPE);
    isAFF4Legacy = resolver->HasURNWithAttributeAndValue(
        urn, AFF4_TYPE, rdfType);

    // Configure the stream parameters.
    XSDInteger value;

    if(!isAFF4Legacy){
        // AFF4 Standard
        if (resolver->Get(urn, AFF4_STREAM_CHUNK_SIZE, value) == STATUS_OK) {
            chunk_size = value.value;
        }

        if (resolver->Get(urn, AFF4_STREAM_CHUNKS_PER_SEGMENT, value) == STATUS_OK) {
            chunks_per_segment = value.value;
        }

        if (resolver->Get(urn, AFF4_STREAM_SIZE, value) == STATUS_OK) {
            size = value.value;

        } else {
            resolver->logger->error(
                "ImageStream {} does not specify a size. "
                "Is this part of a split image set?", urn);
        }
    } else {
        // AFF4 Legacy
        if (resolver->Get(urn, AFF4_LEGACY_STREAM_CHUNK_SIZE, value) == STATUS_OK) {
            chunk_size = value.value;
        }

        if (resolver->Get(urn, AFF4_LEGACY_STREAM_CHUNKS_PER_SEGMENT, value) == STATUS_OK) {
            chunks_per_segment = value.value;
        }

        if (resolver->Get(urn, AFF4_LEGACY_STREAM_SIZE, value) == STATUS_OK) {
            size = value.value;
        }
    }

    // Load the compression scheme. If it is not set we just default to ZLIB.
    URN compression_urn;
    if (STATUS_OK == resolver->Get(urn, AFF4_IMAGE_COMPRESSION, compression_urn)) {
        compression = CompressionMethodFromURN(compression_urn);
        if (compression == AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN) {
            resolver->logger->error(
                "Compression method {} is not supported by this implementation.",
                compression_urn);
            return NOT_IMPLEMENTED;
        }
    } else if (STATUS_OK == resolver->Get(urn, AFF4_LEGACY_IMAGE_COMPRESSION, compression_urn)) {
        compression = CompressionMethodFromURN(compression_urn);
        if (compression == AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN) {
            resolver->logger->error(
                "Compression method {} is not supported by this implementation.",
                compression_urn);
            return NOT_IMPLEMENTED;
        }
    }

    return STATUS_OK;
}


// Bevy is full - flush it to the image and start the next one. Takes
// and disposes of the bevy_writer. Next Write() will make a new
// writer.
AFF4Status AFF4Image::FlushBevy() {
    RETURN_IF_ERROR(bevy_writer->Finalize());

    AFF4Stream &bevy_stream = bevy_writer->bevy_stream();
    RETURN_IF_ERROR(bevy_stream.Seek(0, SEEK_SET));

    // If the bevy is empty nothing else to do.
    if (bevy_stream.Size() == 0) {
        return STATUS_OK;
    }

    URN bevy_urn(urn.Append(aff4_sprintf("%08d", bevy_number)));
    URN bevy_index_urn(bevy_urn.value +(".index"));

    // Open the volume.
    URN volume_urn;
    RETURN_IF_ERROR(resolver->Get(urn, AFF4_STORED, volume_urn));

    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
        volume_urn);

    if (!volume) {
        return NOT_FOUND;
    }

    // Create the new segments in this zip file.
    AFF4ScopedPtr<AFF4Stream> bevy_index_member = volume->CreateMember(
                bevy_index_urn);

    AFF4ScopedPtr<AFF4Stream> bevy_member = volume->CreateMember(bevy_urn);

    if (!bevy_index_member || !bevy_member) {
        resolver->logger->error("Unable to create bevy URN {}", bevy_urn);
        return IO_ERROR;
    }

    std::string index_stream;
    bevy_index_member->reserve(chunks_per_segment * sizeof(BevyIndex));
    RETURN_IF_ERROR(bevy_index_member->Write(
                        bevy_writer->index_stream()));

    ProgressContext empty_progress(resolver);
    bevy_index_member->reserve(chunks_per_segment * chunk_size);
    RETURN_IF_ERROR(bevy_member->WriteStream(&bevy_stream, &empty_progress));

    // These calls flush the bevies and removes them from the resolver cache.
    RETURN_IF_ERROR(resolver->Close(bevy_index_member));
    RETURN_IF_ERROR(resolver->Close(bevy_member));

    // Done with this bevy - make a new writer.
    bevy_writer.reset(new _BevyWriter(
                          resolver, compression, chunk_size,
                          chunks_per_segment));
    bevy_number++;
    chunk_count_in_bevy = 0;

    return STATUS_OK;
}


AFF4Status AFF4Image::Write(const char* data, size_t length) {
    if (length <= 0) return STATUS_OK;

    // Prepare a bevy writer to collect the first bevy.
    if (bevy_writer == nullptr) {
        bevy_writer.reset(new _BevyWriter(resolver, compression, chunk_size,
                                          chunks_per_segment));
    }

    // This object is now dirty.
    MarkDirty();

    // Append small reads to the buffer.
    buffer.append(data, length);
    size_t offset = 0;

    // Consume full chunks from the buffer and send them to the bevy
    // writer.
    while (buffer.size() - offset >= chunk_size) {
        bevy_writer->EnqueueCompressChunk(
            chunk_count_in_bevy,
            buffer.substr(offset, chunk_size));

        chunk_count_in_bevy++;

        if (chunk_count_in_bevy >= chunks_per_segment) {
            RETURN_IF_ERROR(FlushBevy());
        }

        offset += chunk_size;
    }

    // Keep the last part of the buffer which is smaller than a chunk size.
    buffer.erase(0, offset);

    readptr += length;
    if (readptr > size) {
        size = readptr;
    }

    return STATUS_OK;
}


AFF4Image::AFF4Image(DataStore* resolver, URN urn):
    AFF4Stream(resolver, urn) {}

AFF4Image::AFF4Image(DataStore* resolver):
    AFF4Stream(resolver) {}


AFF4Status AFF4Image::WriteStream(AFF4Stream* source,
                                  ProgressContext* progress) {
    DefaultProgress default_progress(resolver);
    if (!progress) {
        progress = &default_progress;
    }

    // Write a bevy at a time.
    while (1) {
        // This looks like a stream but can only read a bevy at a time.
        _CompressorStream stream(resolver, compression, chunk_size,
                                 chunks_per_segment, source);

        // Read and compress the bevy into memory.
        RETURN_IF_ERROR(stream.PrepareBevy());

        URN volume_urn;

        if (resolver->Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
            resolver->logger->error("Unable to find storage for urn {}", urn);
            return NOT_FOUND;
        }

        // Open the volume.
        AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
            volume_urn);

        if (!volume) {
            return NOT_FOUND;
        }

        URN bevy_urn(urn.Append(aff4_sprintf("%08d", bevy_number)));
        URN bevy_index_urn(bevy_urn.value + (".index"));

        // First write the bevy.
        {
            AFF4ScopedPtr<AFF4Stream> bevy = volume->CreateMember(bevy_urn);
            if (!bevy) {
                resolver->logger->error("Unable to create bevy {}", bevy_urn);
                return IO_ERROR;
            }

            bevy->reserve(chunks_per_segment * chunk_size);
            RETURN_IF_ERROR(bevy->WriteStream(&stream, progress));
            resolver->Close(bevy);
        }

        // Now write the index.
        {
            AFF4ScopedPtr<AFF4Stream> bevy_index = volume->CreateMember(
                    bevy_index_urn);

            if (!bevy_index) {
                resolver->logger->error("Unable to create bevy_index {}",
                                       bevy_index_urn);
                return IO_ERROR;
            }

            RETURN_IF_ERROR(bevy_index->Write(
                                stream.bevy_writer.index_stream()));
            resolver->Close(bevy_index);
        }

        // Report the data read from the source.
        if (!progress->Report(source->Tell())) {
            return ABORTED;
        }

        bevy_number++;
        size += stream.Size();

        // The bevy is not full - this means we reached the end of the
        // input.
        if (stream.Size() < chunks_per_segment * chunk_size) {
            break;
        }
    }
    _write_metadata();

    return STATUS_OK;
}



/**
 * Read a single chunk from the bevy and append it to result.
 *
 * @param result: A string which will receive the chunk data.
 * @param bevy: The bevy to read from.
 * @param bevy_index: A bevy index array - the is the offset of each chunk in
 *        the bevy.
 * @param index_size: The length of the bevy index array.
 *
 * @return number of bytes read, or AFF4Status for error.
 */
AFF4Status AFF4Image::ReadChunkFromBevy(
    std::string& result, unsigned int chunk_id, AFF4ScopedPtr<AFF4Stream>& bevy,
    BevyIndex bevy_index[], uint32_t index_size) {
    unsigned int chunk_id_in_bevy = chunk_id % chunks_per_segment;
    BevyIndex entry;

    if (index_size == 0) {
        resolver->logger->error("Index empty in {} : chunk {}",
                               urn, chunk_id);
        return IO_ERROR;
    }

    // The segment is not completely full.
    if (chunk_id_in_bevy >= index_size) {
        resolver->logger->error("Bevy index too short in {} : {}",
                               urn, chunk_id);
        return IO_ERROR;

    } else {
        entry = (bevy_index[chunk_id_in_bevy]);
    }


    bevy->Seek(entry.offset, SEEK_SET);
    std::string cbuffer = bevy->Read(entry.length);

    std::string buffer;
    // We expect the decompressed buffer to be maximum chunk_size. If
    // it ends up decompressing to longer we error out.
    buffer.resize(chunk_size);

    AFF4Status res;

    if(entry.length == chunk_size) {
        // Chunk not compressed.
        buffer = cbuffer;
        res = STATUS_OK;
    } else {
        switch (compression) {
        case AFF4_IMAGE_COMPRESSION_ENUM_ZLIB:
            res = DeCompressZlib_(cbuffer, &buffer);
            break;

        case AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY:
            res = DeCompressSnappy_(cbuffer, &buffer);
            break;

        case AFF4_IMAGE_COMPRESSION_ENUM_LZ4:
            res = DeCompressLZ4_(cbuffer, &buffer);
            break;

        case AFF4_IMAGE_COMPRESSION_ENUM_STORED:
            buffer = cbuffer;
            res = STATUS_OK;
            break;

            // Should never happen because the object should never accept this
            // compression URN.
        default:
            resolver->logger->critical("Unexpected compression type set");
            res = NOT_IMPLEMENTED;
        }
    }

    if (res != STATUS_OK) {
        resolver->logger->error(" {} : Unable to uncompress chunk {}",
                                urn, chunk_id);
        return res;
    }

    result += buffer;
    return STATUS_OK;
}

int AFF4Image::_ReadPartial(unsigned int chunk_id, int chunks_to_read,
                            std::string& result) {
    int chunks_read = 0;

    while (chunks_to_read > 0) {
        unsigned int bevy_id = chunk_id / chunks_per_segment;
        URN bevy_urn = urn.Append(aff4_sprintf("%08d", bevy_id));
        URN bevy_index_urn;

        if  (isAFF4Legacy) {
            bevy_index_urn = bevy_urn.value + ("/index");
        } else {
            bevy_index_urn = bevy_urn.value + (".index");
        }

        AFF4ScopedPtr<AFF4Stream> bevy_index = resolver->AFF4FactoryOpen
            <AFF4Stream>(bevy_index_urn);

        AFF4ScopedPtr<AFF4Stream> bevy = resolver->AFF4FactoryOpen<AFF4Stream>(
            bevy_urn);

        if (!bevy_index || !bevy) {
            resolver->logger->error("Unable to open bevy {}", bevy_urn);
            return -1;
        }

        uint32_t index_size = bevy_index->Size() / sizeof(BevyIndex);
        std::string bevy_index_data = bevy_index->Read(bevy_index->Size());

        if (isAFF4Legacy) {
            // Massage the bevvy data format from the old into the new.
            bevy_index_data = _FixupBevyData(&bevy_index_data);
            index_size = bevy_index->Size() / sizeof(uint32_t);
        }

        BevyIndex* bevy_index_array = reinterpret_cast<BevyIndex*>(
            const_cast<char*>(bevy_index_data.data()));

        while (chunks_to_read > 0) {
            // Read a full chunk from the bevy.
            if (ReadChunkFromBevy(
                    result, chunk_id, bevy, bevy_index_array, index_size) < 0) {
                return IO_ERROR;
            }

            chunks_to_read--;
            chunk_id++;
            chunks_read++;

            // This bevy is exhausted, get the next one.
            if (bevy_id < chunk_id / chunks_per_segment) {
                break;
            }
        }
    }

    return chunks_read;
}

std::string AFF4Image::Read(size_t length) {
    if (length > AFF4_MAX_READ_LEN) {
        return "";
    }

    length = std::min((aff4_off_t)length,
                      std::max((aff4_off_t)0, (aff4_off_t)Size() - readptr));

    int initial_chunk_offset = readptr % chunk_size;
    unsigned int initial_chunk_id = readptr / chunk_size;
    unsigned int final_chunk_id = (readptr + length - 1) / chunk_size;

    // We read this many full chunks at once.
    int chunks_to_read = final_chunk_id - initial_chunk_id + 1;
    unsigned int chunk_id = initial_chunk_id;
    std::string result;

    // Make sure we have enough room for output.
    result.reserve(chunks_to_read * chunk_size);

    while (chunks_to_read > 0) {
        int chunks_read = _ReadPartial(chunk_id, chunks_to_read, result);
        // Error occured.
        if (chunks_read < 0) {
            return "";
        } else if (chunks_read == 0) {
            break;
        }

        chunks_to_read -= chunks_read;
    }

    if (initial_chunk_offset) {
        result.erase(0, initial_chunk_offset);
    }

    result.resize(length);
    readptr = std::min((aff4_off_t)(readptr + length), Size());

    return result;
}

AFF4Status AFF4Image::_write_metadata() {
    resolver->Set(urn, AFF4_TYPE, new URN(AFF4_IMAGESTREAM_TYPE),
                  /* replace = */ false);

    resolver->Set(urn, AFF4_STREAM_CHUNK_SIZE, new XSDInteger(chunk_size));

    resolver->Set(urn, AFF4_STREAM_CHUNKS_PER_SEGMENT,
                  new XSDInteger(chunks_per_segment));

    resolver->Set(urn, AFF4_STREAM_SIZE, new XSDInteger(size));

    resolver->Set(urn, AFF4_IMAGE_COMPRESSION, new URN(
                      CompressionMethodToURN(compression)));

    return STATUS_OK;
}


AFF4Status AFF4Image::Flush() {
    if (IsDirty()) {
        // Flush the last chunk.
        bevy_writer->EnqueueCompressChunk(chunk_count_in_bevy, buffer);
        RETURN_IF_ERROR(FlushBevy());

        _write_metadata();
        buffer.resize(0);
    }

    // Always call the baseclass to ensure the object is marked non dirty.
    return AFF4Stream::Flush();
}

static AFF4Registrar<AFF4Image> r1(AFF4_IMAGESTREAM_TYPE);
static AFF4Registrar<AFF4Image> r2(AFF4_LEGACY_IMAGESTREAM_TYPE);

void aff4_image_init() {}

std::string AFF4Image::_FixupBevyData(std::string* data){
    uint32_t index_size = data->length() / sizeof(uint32_t);
    BevyIndex* bevy_index_array = new BevyIndex[index_size];
    uint32_t* bevy_index_data = reinterpret_cast<uint32_t*>(const_cast<char*>(data->data()));

    uint64_t cOffset = 0;
    uint32_t cLength = 0;

    for(off_t offset = 0; offset < index_size; offset++){
        cLength = bevy_index_data[offset];
        bevy_index_array[offset].offset = cOffset;
        bevy_index_array[offset].length = cLength - cOffset;
        cOffset += bevy_index_array[offset].length;
    }

    std::string result = std::string(
        reinterpret_cast<char*>(bevy_index_array),
        index_size * sizeof(BevyIndex));
    delete[] bevy_index_array;
    return result;
}



AFF4ScopedPtr<AFF4StdImage> AFF4StdImage::NewAFF4StdImage(
    DataStore* resolver, const URN& image_urn, const URN& volume_urn) {
    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
        volume_urn);

    if (!volume) {
        return AFF4ScopedPtr<AFF4StdImage>();    /** Volume not known? */
    }

    // Inform the volume that we have a new image stream contained within it.
    volume->children.insert(image_urn.SerializeToString());

    resolver->Set(image_urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE),
                  /* replace = */ false);
    resolver->Set(image_urn, AFF4_STORED, new URN(volume_urn));
    if(!resolver->HasURNWithAttribute(image_urn, AFF4_STREAM_SIZE)) {
        resolver->Set(image_urn, AFF4_STREAM_SIZE, new XSDInteger((uint64_t)0));
    }

    return resolver->AFF4FactoryOpen<AFF4StdImage>(image_urn);
}

AFF4Status AFF4StdImage::LoadFromURN() {
    // Find the delegate.
    if (resolver->Get(urn, AFF4_DATASTREAM, delegate) != STATUS_OK) {
        RETURN_IF_ERROR(resolver->Get(urn, AFF4_LEGACY_DATASTREAM, delegate));
    }

    // Try to open the delegate.
    auto delegate_stream = resolver->AFF4FactoryOpen<AFF4Stream>(
        delegate);
    if (!delegate_stream) {
        resolver->logger->error("Unable to open aff4:dataStream {} for Image {}",
                                delegate, urn);
        return NOT_FOUND;
    }

    // Get the delegate's size.
    size = delegate_stream->Size();

    return STATUS_OK;
}

std::string AFF4StdImage::Read(size_t length) {
    auto delegate_stream = resolver->AFF4FactoryOpen<AFF4Stream>(
        delegate);
    if (!delegate_stream) {
        resolver->logger->error("Unable to open aff4:dataStream {} for Image {}",
                                delegate, urn);
        return "";
    }

    delegate_stream->Seek(readptr, SEEK_SET);
    return delegate_stream->Read(length);
}

// AFF4 Standard

// In the AFF4 Standard, AFF4_IMAGE_TYPE is an abstract concept which
// delegates the data stream implementation to some other AFF4 stream
// (image or map) via the DataStore attribute.
static AFF4Registrar<AFF4StdImage> image1(AFF4_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image2(AFF4_DISK_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image3(AFF4_VOLUME_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image4(AFF4_MEMORY_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image5(AFF4_CONTIGUOUS_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image6(AFF4_DISCONTIGUOUS_IMAGE_TYPE);

// Legacy
static AFF4Registrar<AFF4StdImage> image7(AFF4_LEGACY_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image8(AFF4_LEGACY_DISK_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image9(AFF4_LEGACY_VOLUME_IMAGE_TYPE);
static AFF4Registrar<AFF4StdImage> image10(AFF4_LEGACY_CONTIGUOUS_IMAGE_TYPE);


} // namespace aff4
