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

AFF4ScopedPtr<AFF4Image> AFF4Image::NewAFF4Image(
    DataStore *resolver, const URN &image_urn, const URN &volume_urn) {
  AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
      volume_urn);

  if (!volume)
    return AFF4ScopedPtr<AFF4Image>();        /** Volume not known? */

  // Inform the volume that we have a new image stream contained within it.
  volume->children.insert(image_urn.value);

  resolver->Set(image_urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));
  resolver->Set(image_urn, AFF4_STORED, new URN(volume_urn));

  return resolver->AFF4FactoryOpen<AFF4Image>(image_urn);
};


/**
 * Initializes this AFF4 object from the information stored in the resolver.
 *
 *
 * @return STATUS_OK if the object was successfully initialized.
 */
AFF4Status AFF4Image::LoadFromURN() {
  if (resolver->Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
    return NOT_FOUND;
  };

  // Configure the stream parameters.
  XSDInteger value;

  if(resolver->Get(urn, AFF4_IMAGE_CHUNK_SIZE, value) == STATUS_OK) {
    chunk_size = value.value;
  };

  if(resolver->Get(urn, AFF4_IMAGE_CHUNKS_PER_SEGMENT, value) == STATUS_OK) {
    chunks_per_segment = value.value;
  };

  if(resolver->Get(urn, AFF4_STREAM_SIZE, value) == STATUS_OK) {
    size = value.value;
  };

  return STATUS_OK;
};


// Check that the bevy
AFF4Status AFF4Image::_FlushBevy() {
  // If the bevy is empty nothing else to do.
  if (bevy.Size() == 0) {
    LOG(INFO) << urn.value.c_str() << "Bevy is empty.";
    return STATUS_OK;
  };

  URN bevy_urn(urn.Append(aff4_sprintf("%08d", bevy_number++)));
  URN bevy_index_urn(bevy_urn.Append("index"));

  // Open the volume.
  AFF4ScopedPtr<AFF4Volume> volume = resolver->AFF4FactoryOpen<AFF4Volume>(
      volume_urn);

  if (!volume) {
    return NOT_FOUND;
  };

  // Create the new segments in this zip file.
  AFF4ScopedPtr<AFF4Stream> bevy_index_stream = volume->CreateMember(bevy_index_urn);
  AFF4ScopedPtr<AFF4Stream> bevy_stream = volume->CreateMember(bevy_urn);

  if(!bevy_index_stream || ! bevy_stream) {
    LOG(ERROR) << "Unable to create bevy URN";
    return IO_ERROR;
  };

  bevy_index_stream->Write(bevy_index.buffer);
  bevy_stream->Write(bevy.buffer);

  // These calls flush the bevies and removes them from the resolver cache.
  resolver->Close(bevy_index_stream);
  resolver->Close(bevy_stream);

  bevy_index.Truncate();
  bevy.Truncate();

  chunk_count_in_bevy = 0;

  return STATUS_OK;
};


/**
 * Flush the current chunk into the current bevy.
 *
 * @param data: Chunk data. This should be a full chunk unless it is the last
 *        chunk in the stream which may be short.
 * @param length: Length of data.
 *
 * @return Status.
 */
AFF4Status AFF4Image::FlushChunk(const char *data, int length) {
  uint32_t bevy_offset = bevy.Tell();
  uLongf c_length = compressBound(length) + 1;
  Bytef c_buffer[c_length];

  if(compress(c_buffer, &c_length, (Bytef *)data, length) != Z_OK) {
    LOG(ERROR) << "Unable to compress chunk " << urn.value.c_str();
    return MEMORY_ERROR;
  };

  bevy_index.Write((char *)&bevy_offset, sizeof(bevy_offset));
  bevy.Write((char *)c_buffer, c_length);

  chunk_count_in_bevy++;

  if(chunk_count_in_bevy >= chunks_per_segment) {
    return _FlushBevy();
  };

  return STATUS_OK;
};

int AFF4Image::Write(const char *data, int length) {
  // This object is now dirty.
  MarkDirty();

  buffer.append(data, length);

  // Consume full chunks.
  while (buffer.length() > chunk_size) {
    string chunk = buffer.substr(0, chunk_size);
    buffer.erase(0, chunk_size);

    FlushChunk(chunk.c_str(), chunk.length());
  };

  readptr += length;
  if (readptr > size) {
    size = readptr;
  };

  return length;
};


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
    string &result, unsigned int chunk_id, AFF4ScopedPtr<AFF4Stream> &bevy,
    uint32_t bevy_index[], uint32_t index_size) {

  unsigned int chunk_id_in_bevy = chunk_id % chunks_per_segment;
  unsigned int compressed_chunk_size;

  if (index_size == 0) {
    LOG(ERROR) << "Index empty in " << urn.value.c_str() << ":" << chunk_id;
    return IO_ERROR;
  };

  // The segment is not completely full.
  if (chunk_id_in_bevy >= index_size) {
    LOG(ERROR) << "Bevy index too short in " << urn.value.c_str() << ":"
               << chunk_id;
    return IO_ERROR;

    // For the last chunk in the bevy, consume to the end of the bevy segment.
  } else if (chunk_id_in_bevy == index_size - 1) {
    compressed_chunk_size = bevy->Size() - bevy->Tell();

  } else {
    compressed_chunk_size = (bevy_index[chunk_id_in_bevy + 1] -
                             bevy_index[chunk_id_in_bevy]);
  };


  bevy->Seek(bevy_index[chunk_id_in_bevy], SEEK_SET);
  string cbuffer = bevy->Read(compressed_chunk_size);

  string buffer;
  buffer.resize(chunk_size);

  uLongf buffer_size = chunk_size;

  if(uncompress((Bytef *)buffer.data(), &buffer_size,
                (const Bytef *)cbuffer.data(), cbuffer.size()) == Z_OK) {

    result += buffer;
    return STATUS_OK;
  };

  LOG(ERROR) << urn.value.c_str() << ": Unable to uncompress chunk "
             << chunk_id;
  return IO_ERROR;
};

int AFF4Image::_ReadPartial(unsigned int chunk_id, int chunks_to_read,
                            string &result) {
  int chunks_read = 0;

  while (chunks_to_read > 0) {
    unsigned int bevy_id = chunk_id / chunks_per_segment;
    URN bevy_urn = urn.Append(aff4_sprintf("%08d", bevy_id));
    URN bevy_index_urn = bevy_urn.Append("index");

    AFF4ScopedPtr<AFF4Stream> bevy_index = resolver->AFF4FactoryOpen<AFF4Stream>(
        bevy_index_urn);

    AFF4ScopedPtr<AFF4Stream> bevy = resolver->AFF4FactoryOpen<AFF4Stream>(
        bevy_urn);

    if(!bevy_index || !bevy) {
      LOG(ERROR) << "Unable to open bevy " << bevy_urn.value.c_str();
      return -1;
    }

    uint32_t index_size = bevy_index->Size() / sizeof(uint32_t);
    string bevy_index_data = bevy_index->Read(bevy_index->Size());

    uint32_t *bevy_index_array = (uint32_t *)bevy_index_data.data();

    while (chunks_to_read > 0) {
      // Read a full chunk from the bevy.
      AFF4Status res = _ReadChunkFromBevy(
          result, chunk_id, bevy, bevy_index_array, index_size);

      if(res != STATUS_OK) {
        return res;
      };

      chunks_to_read--;
      chunk_id++;
      chunks_read++;

      // This bevy is exhausted, get the next one.
      if(bevy_id < chunk_id / chunks_per_segment) {
        break;
      }
    };
  };

  return chunks_read;
};

string AFF4Image::Read(size_t length) {
  length = std::min(length, Size() - readptr);

  int initial_chunk_offset = readptr % chunk_size;
  // We read this many full chunks at once.
  int chunks_to_read = length / chunk_size + 1;
  unsigned int chunk_id = readptr / chunk_size;
  string result;

  // Make sure we have enough room for output.
  result.reserve(chunks_to_read * chunk_size);

  while(chunks_to_read > 0) {
    int chunks_read = _ReadPartial(chunk_id, chunks_to_read, result);
    // Error occured.
    if (chunks_read < 0) {
      return "";
    } else if (chunks_read == 0) {
      break;
    };

    chunks_to_read -= chunks_read;
  };

  if (initial_chunk_offset) {
    result.erase(0, initial_chunk_offset);
  };

  result.resize(length);
  readptr += length;

  return result;
};


AFF4Status AFF4Image::Flush() {
  if(IsDirty()) {
    // Flush the last chunk.
    FlushChunk(buffer.c_str(), buffer.length());
    buffer.resize(0);
    _FlushBevy();

    resolver->Set(urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));

    resolver->Set(urn, AFF4_STORED, new URN(volume_urn));

    resolver->Set(urn, AFF4_IMAGE_CHUNK_SIZE,
                  new XSDInteger(chunk_size));

    resolver->Set(urn, AFF4_IMAGE_CHUNKS_PER_SEGMENT,
                  new XSDInteger(chunks_per_segment));

    resolver->Set(urn, AFF4_STREAM_SIZE, new XSDInteger(size));
    resolver->Set(urn, AFF4_IMAGE_COMPRESSION,
                  new URN(AFF4_IMAGE_COMPRESSION_DEFLATE));

  };

  // Always call the baseclass to ensure the object is marked non dirty.
  return AFF4Stream::Flush();
};

static AFF4Registrar<AFF4Image> r1(AFF4_IMAGE_TYPE);
