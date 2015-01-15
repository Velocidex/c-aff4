#ifndef _AFF4_IMAGE_H_
#define _AFF4_IMAGE_H_

#include "aff4_io.h"

class AFF4Image: public AFF4Stream {
 private:
  int FlushChunk(const char *data, int length);

 protected:
  string buffer;
  unique_ptr<AFF4Stream> bevy_index;
  unique_ptr<AFF4Stream> bevy;
  URN volume_urn;

 public:
  virtual ~AFF4Image();

  unsigned int chunksize = 32*1024;

  static unique_ptr<AFF4Image> NewAFF4Image(string filename, AFF4Volume &volume);

  virtual int Write(const char *data, int length);

  using AFF4Stream::Write;
};



#endif // _AFF4_IMAGE_H_
