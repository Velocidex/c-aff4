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
    string filename, AFF4Volume &volume) {
  unique_ptr<AFF4Image> result(new AFF4Image());

  result->bevy_index = volume.CreateMember(filename + "/index");
  result->bevy = volume.CreateMember(filename);
  result->volume_urn = volume.urn;

  return result;
};

int AFF4Image::FlushChunk(const char *data, int length) {
  uint32_t offset = bevy->Tell();
  bevy_index->Write((char *)&offset, sizeof(offset));

  uLongf c_length = compressBound(length) + 1;
  Bytef c_buffer[c_length];

  // TODO: What can be done if we failed to compress?
  compress(c_buffer, &c_length, (Bytef *)data, length);

  bevy->Write((char *)c_buffer, c_length);

  return length;
};

int AFF4Image::Write(const char *data, int length) {
  _dirty = true;

  buffer.append(data, length);
  // Consume the chunk.
  if (buffer.length() > chunksize) {
    string chunk = buffer.substr(0, chunksize);
    buffer.erase(0, chunksize);

    FlushChunk(chunk.c_str(), chunk.length());
  };

  readptr += length;
  if (readptr > size) {
    size = readptr;
  };

  return length;
};

AFF4Image::~AFF4Image() {
  if(_dirty) {
    // Flush the last chunk.
    FlushChunk(buffer.c_str(), buffer.length());

    oracle.Set(urn, AFF4_TYPE, new URN(AFF4_IMAGE_TYPE));
    oracle.Set(urn, AFF4_STORED, new URN(volume_urn));
    oracle.Set(urn, AFF4_IMAGE_CHUNK_SIZE, new XSDInteger(chunksize));
    oracle.Set(urn, AFF4_STREAM_SIZE, new XSDInteger(size));
    oracle.Set(
        urn, AFF4_IMAGE_COMPRESSION, new URN(AFF4_IMAGE_COMPRESSION_DEFLATE));
  };
};
