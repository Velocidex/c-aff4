#include "aff4_image.h"
#include <zlib.h>

unique_ptr<AFF4Image> AFF4Image::NewAFF4Image(
    string filename, AFF4Volume &volume) {
  unique_ptr<AFF4Image> result(new AFF4Image());

  result->bevy_index = volume.CreateMember(filename + "/index");
  result->bevy = volume.CreateMember(filename);

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
  buffer.append(data, length);
  // Consume the chunk.
  if (buffer.length() > chunksize) {
    string chunk = buffer.substr(0, chunksize);
    buffer.erase(0, chunksize);

    FlushChunk(chunk.c_str(), chunk.length());
  };

  return length;
};

AFF4Image::~AFF4Image() {
  FlushChunk(buffer.c_str(), buffer.length());
};
