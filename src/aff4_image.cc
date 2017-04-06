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
#include "aff4_image.h"
#include <zlib.h>
#include <snappy.h>


AFF4ScopedPtr<AFF4Image> AFF4Image::NewAFF4Image(
    DataStore* resolver, const URN& image_urn, const URN& volume_urn) {
    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
                                           volume_urn);

    if (!volume) {
        return AFF4ScopedPtr<AFF4Image>();    /** Volume not known? */
    }

    // Inform the volume that we have a new image stream contained within it.
    volume->children.insert(image_urn.SerializeToString());

    resolver->Set(image_urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));
    resolver->Set(image_urn, AFF4_STORED, new URN(volume_urn));

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
    if (resolver->Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
    	if (resolver->Get(urn, AFF4_LEGACY_STORED, volume_urn) != STATUS_OK) {
    		return NOT_FOUND;
    	}
    }

    // Determine if this is an AFF4:ImageStream (AFF4 Standard) or
    // a aff4:stream (AFF4 Legacy)
    URN rdfType (AFF4_LEGACY_IMAGESTREAM_TYPE);
    isAFF4Legacy = (resolver->Has(urn, AFF4_TYPE, rdfType) == STATUS_OK);


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
            LOG(ERROR) << "Compression method " <<
                       compression_urn.SerializeToString().c_str() <<
                       " is not supported by this implementation.";
            return NOT_IMPLEMENTED;
        }
    } else if (STATUS_OK == resolver->Get(urn, AFF4_LEGACY_IMAGE_COMPRESSION, compression_urn)) {
        compression = CompressionMethodFromURN(compression_urn);
        if (compression == AFF4_IMAGE_COMPRESSION_ENUM_UNKNOWN) {
            LOG(ERROR) << "Compression method " <<
                       compression_urn.SerializeToString().c_str() <<
                       " is not supported by this implementation.";
            return NOT_IMPLEMENTED;
        }
    }

    return STATUS_OK;
}


// Check that the bevy
AFF4Status AFF4Image::_FlushBevy() {
    // If the bevy is empty nothing else to do.
    if (bevy.Size() == 0) {
        LOG(INFO) << urn.SerializeToString() << "Bevy is empty.";
        return STATUS_OK;
    }

    URN bevy_urn(urn.Append(aff4_sprintf("%08d", bevy_number)));
    URN bevy_index_urn(bevy_urn.value +(".index"));

    // Open the volume.
    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
                                           volume_urn);

    if (!volume) {
        return NOT_FOUND;
    }

    // Create the new segments in this zip file.
    AFF4ScopedPtr<AFF4Stream> bevy_index_stream = volume->CreateMember(
                bevy_index_urn);

    AFF4ScopedPtr<AFF4Stream> bevy_stream = volume->CreateMember(bevy_urn);

    if (!bevy_index_stream || !bevy_stream) {
        LOG(ERROR) << "Unable to create bevy URN";
        return IO_ERROR;
    }

    if (bevy_index_stream->Write(bevy_index.buffer) < 0) {
        return IO_ERROR;
    }

    if (bevy_stream->Write(bevy.buffer) < 0) {
        return IO_ERROR;
    }

    // These calls flush the bevies and removes them from the resolver cache.
    AFF4Status res = resolver->Close(bevy_index_stream);
    if (res != STATUS_OK) {
        return res;
    }

    res = resolver->Close(bevy_stream);
    if (res != STATUS_OK) {
        return res;
    }

    bevy_index.Truncate();
    bevy.Truncate();

    chunk_count_in_bevy = 0;
    bevy_number++;

    return STATUS_OK;
}


/**
 * Flush the current chunk into the current bevy.
 *
 * @param data: Chunk data. This should be a full chunk unless it is the last
 *        chunk in the stream which may be short.
 * @param length: Length of data.
 *
 * @return Status.
 */
AFF4Status AFF4Image::FlushChunk(const char* data, size_t length) {
    uint32_t bevy_offset = bevy.Tell();
    std::string output;

    AFF4Status result;

    switch (compression) {
        case AFF4_IMAGE_COMPRESSION_ENUM_ZLIB: {
            result = CompressZlib_(data, length, &output);
        }
        break;

        case AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY: {
            result = CompressSnappy_(data, length, &output);
        }
        break;

        case AFF4_IMAGE_COMPRESSION_ENUM_STORED: {
            output.assign(data, length);
            result = STATUS_OK;
        }
        break;

        default:
            return IO_ERROR;
    }

    if (bevy_index.Write(
                reinterpret_cast<char*>(&bevy_offset), sizeof(bevy_offset)) < 0) {
        return IO_ERROR;
    }

    if (bevy.Write(output) < 0) {
        return IO_ERROR;
    }

    chunk_count_in_bevy++;

    if (chunk_count_in_bevy >= chunks_per_segment) {
        return _FlushBevy();
    }

    return result;
}

AFF4Status CompressZlib_(const char* data, size_t length, std::string* output) {
    uLongf c_length = compressBound(length) + 1;
    output->resize(c_length);

    if (compress2(reinterpret_cast<Bytef*>(const_cast<char*>(output->data())),
                  &c_length,
                  reinterpret_cast<Bytef*>(const_cast<char*>(data)),
                  length, 1) != Z_OK) {
        return MEMORY_ERROR;
    }

    output->resize(c_length);

    return STATUS_OK;
}

AFF4Status DeCompressZlib_(const char* data, size_t length, std::string* buffer) {
    uLongf buffer_size = buffer->size();

    if (uncompress(reinterpret_cast<Bytef*>(const_cast<char*>(buffer->data())),
                   &buffer_size,
                   (const Bytef*)data, length) == Z_OK) {
        buffer->resize(buffer_size);
        return STATUS_OK;
    }

    return IO_ERROR;
}


AFF4Status CompressSnappy_(const char* data, size_t length, std::string* output) {
    snappy::Compress(data, length, output);

    return STATUS_OK;
}


AFF4Status DeCompressSnappy_(const char* data, size_t length, std::string* output) {
    if (!snappy::Uncompress(data, length, output)) {
        return GENERIC_ERROR;
    }

    return STATUS_OK;
}


int AFF4Image::Write(const char* data, int length) {
    // This object is now dirty.
    MarkDirty();

    buffer.append(data, length);
    size_t offset = 0;
    const char* chunk_ptr = buffer.data();

    // Consume full chunks from the buffer.
    while (buffer.length() - offset >= chunk_size) {
        if (FlushChunk(chunk_ptr + offset, chunk_size) != STATUS_OK) {
            return IO_ERROR;
        }

        offset += chunk_size;
    }

    // Keep the last part of the buffer which is smaller than a chunk size.
    buffer.erase(0, offset);

    readptr += length;
    if (readptr > size) {
        size = readptr;
    }

    return length;
}


// A private class which manages image stream.
class _CompressorStream: public AFF4Stream {
  private:
    AFF4Stream* stream;
    AFF4Image* owner;

  public:
    StringIO bevy_index;
    uint32_t chunk_count_in_bevy = 0;
    uint32_t bevy_length = 0;

    _CompressorStream(DataStore* resolver, AFF4Image* owner, AFF4Stream* stream):
        AFF4Stream(resolver), stream(stream), owner(owner) {}

    std::string Read(size_t unused_length) {
        UNUSED(unused_length);
        // Stop copying when the bevy is full.
        if (chunk_count_in_bevy >= owner->chunks_per_segment) {
            return "";
        }

        const std::string data(stream->Read(owner->chunk_size));
        if (data.size() == 0) {
            return "";
        }

        // Our readptr reflects the source stream's readptr in order to report how
        // much we read from it accurately.
        readptr = stream->Tell();

        size_t length = data.size();
        std::string output;

        size += length;
        AFF4Status result;

        switch (owner->compression) {
            case AFF4_IMAGE_COMPRESSION_ENUM_ZLIB: {
                result = CompressZlib_(data.data(), length, &output);
            }
            break;

            case AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY: {
                result = CompressSnappy_(data.data(), length, &output);
            }
            break;

            case AFF4_IMAGE_COMPRESSION_ENUM_STORED: {
                output.assign(data.data(), length);
                result = STATUS_OK;
            }
            break;

            // Should never happen because the object should never accept this
            // compression URN.
            default:
                LOG(FATAL) << "Unexpected compression type set";
                result = NOT_IMPLEMENTED;
        }

        if (result == STATUS_OK) {
            if (bevy_index.Write(
                        reinterpret_cast<char*>(&bevy_length), sizeof(bevy_length)) < 0) {
                return "";
            }

            bevy_length += output.size();
            chunk_count_in_bevy++;
        }

        return output;
    }


    virtual ~_CompressorStream() {}
};


AFF4Status AFF4Image::WriteStream(AFF4Stream* source,
                                  ProgressContext* progress) {
    URN volume_urn;
    DefaultProgress default_progress;
    if (!progress) {
        progress = &default_progress;
    }

    if (resolver->Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
        LOG(ERROR) << "Unable to find storage for urn " <<
                   urn.SerializeToString().c_str();
        return NOT_FOUND;
    }

    // Open the volume.
    AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
                                           volume_urn);

    if (!volume) {
        return NOT_FOUND;
    }

    // Write a bevy at a time.
    while (1) {
        _CompressorStream stream(resolver, this, source);

        // TODO(scudette): This scheme is problematic on filesystems that
        // distinguish between files and directories.
        URN bevy_urn(urn.Append(aff4_sprintf("%08d", bevy_number)));
        URN bevy_index_urn(bevy_urn.value + (".index"));

        // First write the bevy.
        {
            AFF4ScopedPtr<AFF4Stream> bevy = volume->CreateMember(bevy_urn);
            if (!bevy) {
                LOG(ERROR) << "Unable to create bevy " <<
                           bevy_urn.SerializeToString().c_str();
                return IO_ERROR;
            }

            AFF4Status res = bevy->WriteStream(&stream, progress);
            if (res != STATUS_OK) {
                return res;
            }

            // Report the data read from the source.
            if (!progress->Report(source->Tell())) {
                return ABORTED;
            }
        }

        // Now write the index.
        {
            AFF4ScopedPtr<AFF4Stream> bevy_index = volume->CreateMember(
                    bevy_index_urn);

            if (!bevy_index) {
                LOG(ERROR) << "Unable to create bevy_index " <<
                           bevy_index_urn.SerializeToString().c_str();
                return IO_ERROR;
            }

            if (bevy_index->Write(stream.bevy_index.buffer) < 0) {
                return IO_ERROR;
            }
        }

        bevy_number++;
        size += stream.Size();

        if (stream.chunk_count_in_bevy != chunks_per_segment) {
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
AFF4Status AFF4Image::_ReadChunkFromBevy(
    std::string& result, unsigned int chunk_id, AFF4ScopedPtr<AFF4Stream>& bevy,
    BevvyIndex bevy_index[], uint32_t index_size) {

    unsigned int chunk_id_in_bevy = chunk_id % chunks_per_segment;
    BevvyIndex entry;

    if (index_size == 0) {
        LOG(ERROR) << "Index empty in " <<
                   urn.SerializeToString() << ":" << chunk_id;
        return IO_ERROR;
    }

    // The segment is not completely full.
    if (chunk_id_in_bevy >= index_size) {
        LOG(ERROR) << "Bevy index too short in " <<
                   urn.SerializeToString() << ":" << chunk_id;
        return IO_ERROR;

    } else {
        entry = (bevy_index[chunk_id_in_bevy]);
    }


    bevy->Seek(entry.offset, SEEK_SET);
    std::string cbuffer = bevy->Read(entry.length);

    std::string buffer;
    buffer.resize(chunk_size);

    AFF4Status res;

    if(entry.length == chunk_size) {
        // Chunk not compressed.
        buffer = cbuffer;
        res = STATUS_OK;
    } else {
        switch (compression) {
            case AFF4_IMAGE_COMPRESSION_ENUM_ZLIB:
                res = DeCompressZlib_(cbuffer.data(), cbuffer.length(), &buffer);
                break;

            case AFF4_IMAGE_COMPRESSION_ENUM_SNAPPY:
                res = DeCompressSnappy_(cbuffer.data(), cbuffer.length(), &buffer);
                break;

            case AFF4_IMAGE_COMPRESSION_ENUM_STORED:
                buffer = cbuffer;
                res = STATUS_OK;
                break;

            // Should never happen because the object should never accept this
            // compression URN.
            default:
                LOG(FATAL) << "Unexpected compression type set";
                res = NOT_IMPLEMENTED;
        }
    }

    if (res != STATUS_OK) {
        LOG(ERROR) << urn.SerializeToString() <<
                   ": Unable to uncompress chunk " << chunk_id;
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
        URN bevy_index_urn = (!isAFF4Legacy) ? bevy_urn.value + (".index") : bevy_urn.value + ("/index");

        AFF4ScopedPtr<AFF4Stream> bevy_index = resolver->AFF4FactoryOpen
                                               <AFF4Stream>(bevy_index_urn);

        AFF4ScopedPtr<AFF4Stream> bevy = resolver->AFF4FactoryOpen<AFF4Stream>(
                                             bevy_urn);

        if (!bevy_index || !bevy) {
            LOG(ERROR) << "Unable to open bevy " <<
                       bevy_urn.SerializeToString();
            return -1;
        }

        uint32_t index_size = bevy_index->Size() / sizeof(BevvyIndex);
        std::string bevy_index_data = bevy_index->Read(bevy_index->Size());

		if (isAFF4Legacy) {
			// Massage the bevvy data format from the old into the new.
			bevy_index_data = _FixupBevvyData(&bevy_index_data);
			index_size = bevy_index->Size() / sizeof(uint32_t);
		}

        BevvyIndex* bevy_index_array = reinterpret_cast<BevvyIndex*>(
                                           const_cast<char*>(bevy_index_data.data()));

        while (chunks_to_read > 0) {
            // Read a full chunk from the bevy.
            AFF4Status res = _ReadChunkFromBevy(
                                 result, chunk_id, bevy, bevy_index_array, index_size);

            if (res != STATUS_OK) {
                return res;
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

    length = std::min((aff4_off_t)length, Size() - readptr);

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
    readptr += length;

    return result;
}

AFF4Status AFF4Image::_write_metadata() {
    resolver->Set(urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));

    resolver->Set(urn, AFF4_STORED, new URN(volume_urn));

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
        AFF4Status res = FlushChunk(buffer.c_str(), buffer.length());
        if (res != STATUS_OK) {
            return res;
        }

        buffer.resize(0);
        res = _FlushBevy();
        if (res != STATUS_OK) {
            return res;
        }

        _write_metadata();
    }

    // Always call the baseclass to ensure the object is marked non dirty.
    return AFF4Stream::Flush();
}

static AFF4Registrar<AFF4Image> r1(AFF4_IMAGESTREAM_TYPE);
static AFF4Registrar<AFF4Image> r2(AFF4_LEGACY_IMAGESTREAM_TYPE);

void aff4_image_init() {}

std::string AFF4Image::_FixupBevvyData(std::string* data){
	uint32_t index_size = data->length() / sizeof(uint32_t);
	BevvyIndex* bevy_index_array = new BevvyIndex[index_size];
	uint32_t* bevy_index_data = reinterpret_cast<uint32_t*>(const_cast<char*>(data->data()));

	uint64_t cOffset = 0;
	uint32_t cLength = 0;

	for(off_t offset = 0; offset < index_size; offset++){
		cLength = bevy_index_data[offset];
		bevy_index_array[offset].offset = cOffset;
		bevy_index_array[offset].length = cLength - cOffset;
		cOffset += bevy_index_array[offset].length;
	}

	std::string result = std::string(reinterpret_cast<char*>(bevy_index_array), index_size * sizeof(BevvyIndex));
	delete[] bevy_index_array;
	return result;
}
