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

AFF4Image *AFF4Image::NewAFF4Image(
    DataStore *resolver, const string &filename, const URN &volume_urn) {
  AFF4Volume *volume = AFF4FactoryOpen<AFF4Volume>(resolver, volume_urn);
  if (!volume)
    return NULL;                        /** Volume not known? */

  unique_ptr<AFF4Image> result(new AFF4Image(resolver));

  // Inform the volume that we have a new image stream contained within it.
  volume->children.insert(result->urn.value);

  resolver->Set(result->urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));
  resolver->Set(result->urn, AFF4_STORED, new URN(volume_urn));

  return AFF4FactoryOpen<AFF4Image>(resolver, result->urn);
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
  if (bevy.Size() == 0)
    return STATUS_OK;

  URN bevy_urn(urn.Append(aff4_sprintf("%08d", bevy_number++)));
  URN bevy_index_urn(bevy_urn.Append("index"));

  // Open the volume.
  AFF4Volume *volume = AFF4FactoryOpen<AFF4Volume>(resolver, volume_urn);
  if (!volume) {
    return NOT_FOUND;
  };

  // Create the new segments in this zip file.
  AFF4Stream *bevy_index_stream = volume->CreateMember(bevy_index_urn.value);
  AFF4Stream *bevy_stream = volume->CreateMember(bevy_urn.value);

  if(!bevy_index_stream || ! bevy_stream) {
    return IO_ERROR;
  };

  bevy_index_stream->Write(bevy_index.buffer);
  bevy_stream->Write(bevy.buffer);

  bevy_index.Truncate();
  bevy.Truncate();

  chunk_count_in_bevy = 0;

  return STATUS_OK;
};


AFF4Status AFF4Image::FlushChunk(const char *data, int length) {
  uint32_t bevy_offset = bevy.Tell();
  uLongf c_length = compressBound(length) + 1;
  Bytef c_buffer[c_length];

  if(compress(c_buffer, &c_length, (Bytef *)data, length) != Z_OK)
    return MEMORY_ERROR;

  bevy_index.Write((char *)&bevy_offset, sizeof(bevy_offset));
  bevy.Write((char *)c_buffer, c_length);

  chunk_count_in_bevy++;

  if(chunk_count_in_bevy >= chunks_per_segment) {
    return _FlushBevy();
  };

  return STATUS_OK;
};

int AFF4Image::Write(const char *data, int length) {
  _dirty = true;

  buffer.append(data, length);
  // Consume the chunk.
  if (buffer.length() > chunk_size) {
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


int AFF4Image::_ReadPartialBevy(size_t length, string &result,
                                AFF4Stream *bevy,
                                uint32_t bevy_index[],
                                uint32_t index_size) {
  int chunk = readptr / chunk_size;
  unsigned int chunk_id_in_bevy = chunk % chunks_per_segment;
  unsigned int compressed_chunk_size;

  if (index_size == 0) {
    return -1;
  };

  // The segment is not completely full.
  if (chunk_id_in_bevy >= index_size) {
    return -1;

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
    readptr += buffer.size();

    return buffer.size();
  };

  return -1;
};

int AFF4Image::_ReadPartial(size_t length, string &result) {
  int bevy_size = chunk_size * chunks_per_segment;

  while (length > 0) {
    int bevy_id = readptr / bevy_size;
    URN bevy_urn = urn.Append(aff4_sprintf("%08d", bevy_id));
    URN bevy_index_urn = bevy_urn.Append("index");

    AFF4Stream *bevy_index = AFF4FactoryOpen<AFF4Stream>(
        resolver, bevy_index_urn);

    AFF4Stream *bevy = AFF4FactoryOpen<AFF4Stream>(resolver, bevy_urn);
    if(!bevy_index || !bevy) {
      return 0;
    }

    uint32_t index_size = bevy_index->Size() / sizeof(uint32_t);
    string bevy_index_data = bevy_index->Read(bevy_index->Size());

    uint32_t *bevy_index_array = (uint32_t *)bevy_index_data.data();

    while (length > 0) {
      int length_read = _ReadPartialBevy(
          length, result, bevy, bevy_index_array, index_size);

      if(length_read < 0) {
        return -1;
      };

      if (length_read == 0) {
        break;
      };

      length -= length_read;

      // The current bevy is more than the bevy we have opened. Return 0 here so
      // our caller can call us again.
      if (readptr / bevy_size > bevy_id) {
        break;
      };
    };
  };

  return 0;
};

string AFF4Image::Read(size_t length) {
  string result;
  int initial_chunk_offset = readptr % chunk_size;

  length = std::min(length, Size() - readptr);

  // Make sure we have enough room for output.
  result.reserve(length + chunk_size);

  while(length > 0) {
    int length_read = _ReadPartial(length, result);
    // Error occured.
    if (length_read < 0) {
      return "";
    } else if (length_read == 0) {
      break;
    };

    length -= length_read;
  };

  if (initial_chunk_offset) {
    result.erase(0, initial_chunk_offset);
  };

  return result;
};


AFF4Status AFF4Image::Flush() {
  if(_dirty) {
    // Flush the last chunk.
    FlushChunk(buffer.c_str(), buffer.length());
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
};

static AFF4Registrar<AFF4Image> r1(AFF4_IMAGE_TYPE);
