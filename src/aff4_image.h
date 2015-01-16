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
