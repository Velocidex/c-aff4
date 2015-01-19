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

unique_ptr<AFF4Image> AFF4Image::NewAFF4Image(
    const string &filename, shared_ptr<AFF4Volume> volume) {
  unique_ptr<AFF4Image> result(new AFF4Image());

  result->urn.Set(volume->urn.value + "/" + filename);
  result->volume = volume;

  return result;
};


/**
 * Initializes this AFF4 object from the information stored in the oracle.
 *
 *
 * @return STATUS_OK if the object was successfully initialized.
 */
AFF4Status AFF4Image::LoadFromURN(const string &mode) {
  URN volume_urn;

  if (oracle.Get(urn, AFF4_STORED, volume_urn) != STATUS_OK) {
    return NOT_FOUND;
  };

  // Open the volume.
  volume = AFF4FactoryOpen<AFF4Volume>(volume_urn);
  if (!volume) {
    return NOT_FOUND;
  };

  // Configure the stream parameters.
  XSDInteger value;

  if(oracle.Get(urn, AFF4_IMAGE_CHUNK_SIZE, value) == STATUS_OK) {
    chunk_size = value.value;
  };

  if(oracle.Get(urn, AFF4_IMAGE_CHUNKS_PER_SEGMENT, value) == STATUS_OK) {
    chunks_per_segment = value.value;
  };

  if(oracle.Get(urn, AFF4_STREAM_SIZE, value) == STATUS_OK) {
    size = value.value;
  };

  return STATUS_OK;
};


// Check that the bevy
AFF4Status AFF4Image::_CreateBevy() {
  // We need to flush the old bevy because it is too full.
  if(bevy && chunk_count_in_bevy >= chunks_per_segment) {
    bevy.reset();
    bevy_index.reset();

    // Make a new bevy.
    bevy_number++;
  };

  // Bevy does not exist yet - we need to make one.
  if(!bevy) {
    string filename = urn.SerializeToString();
    string bevy_urn = aff4_sprintf("%s/%08d", filename.c_str(), bevy_number);

    bevy_index = volume->CreateMember(bevy_urn + "/index");
    bevy = volume->CreateMember(bevy_urn);
    chunk_count_in_bevy = 0;
  };

  return STATUS_OK;
};


AFF4Status AFF4Image::FlushChunk(const char *data, int length) {
  _CreateBevy();  // Ensure the bevy is active.

  uint32_t offset = bevy->Tell();
  bevy_index->Write((char *)&offset, sizeof(offset));

  uLongf c_length = compressBound(length) + 1;
  Bytef c_buffer[c_length];

  // TODO: What can be done if we failed to compress?
  compress(c_buffer, &c_length, (Bytef *)data, length);

  bevy->Write((char *)c_buffer, c_length);

  chunk_count_in_bevy++;

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


string AFF4Image::_ReadPartial(size_t length) {
  int chunk = readptr / chunk_size;
  int chunk_offset = readptr % chunk_size;
  int bevy_id = chunk / chunks_per_segment;
  int chunk_id_in_bevy = chunk % chunks_per_segment;
  URN bevy_urn = urn.Append(aff4_sprintf("%08d", bevy_id));

  unique_ptr<AFF4Stream> bevy = AFF4FactoryOpen<AFF4Stream>(bevy_urn);


};

string AFF4Image::Read(size_t length) {
  string result;

  result.reserve(length);

  while(length > 0) {
    string data = _ReadPartial(length);
    if (data.size() == 0) {
      break;
    };

    length -= data.size();
    result += data;
  };

  return result;
};


AFF4Image::~AFF4Image() {
  if(_dirty) {
    // Flush the last chunk.
    FlushChunk(buffer.c_str(), buffer.length());

    oracle.Set(urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));

    oracle.Set(urn, AFF4_STORED, new URN(volume->urn));

    oracle.Set(urn, AFF4_IMAGE_CHUNK_SIZE,
               new XSDInteger(chunk_size));

    oracle.Set(urn, AFF4_IMAGE_CHUNKS_PER_SEGMENT,
               new XSDInteger(chunks_per_segment));

    oracle.Set(urn, AFF4_STREAM_SIZE, new XSDInteger(size));
    oracle.Set(urn, AFF4_IMAGE_COMPRESSION,
               new URN(AFF4_IMAGE_COMPRESSION_DEFLATE));
  };
};

static AFF4Registrar<AFF4Image> r1(AFF4_IMAGE_TYPE);
