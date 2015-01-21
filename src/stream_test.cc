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

#include "libaff4.h"

#include <iostream>

using std::cout;


URN test_AFF4Image() {
  AFF4Stream *file = AFF4FactoryOpen<AFF4Stream>("test.zip", "rw");

  // The backing file is given to the zip.
  AFF4Volume *zip = ZipFile::NewZipFile(file->urn);

  AFF4Image *image = AFF4Image::NewAFF4Image("image.dd", zip->urn);

  // For testing - rediculously small chunks.
  image->chunk_size = 10;
  image->chunks_per_segment = 3;

  for(int i=0; i<100; i++) {
    image->Write("Hello wolrd!");
  };

  return image->urn;
};

void test_MemoryDataStore() {
  unique_ptr<MemoryDataStore> store(new MemoryDataStore());
  unique_ptr<AFF4Stream> output = StringIO::NewStringIO();

  store->Set(URN("hello"), URN("World"), new XSDString("foo"));
  store->Set(URN("hello"), URN("World"), new XSDString("bar"));

  RDFBytes result;

  store->Get(URN("hello"), URN("World"), result);
  cout << result.SerializeToString().data() << "\n";

  store->DumpToYaml(*output);
  store->DumpToTurtle(*output);

  cout << output->Tell() << "\n";
  output->Seek(0, 0);
  cout << output->Read(1000).c_str() << "\n";
}

void test_AFF4Stream(AFF4Stream *stream) {
  cout << "Testing\n";
  cout << "*******\n";

  stream->Write("hello world");
  cout << stream->Tell() << "\n";

  stream->Seek(0, 0);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->Read(1000).c_str() << "\n";
  cout << stream->Tell() << "\n";

  stream->Seek(-5, 2);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->Read(1000).c_str() << "\n";

  stream->Seek(-5, 2);
  cout << stream->Tell() << "\n";
  stream->Write("Cruel world");
  stream->Seek(0, 0);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->Read(1000).c_str() << "\n";
  cout << stream->Tell() << "\n";

  stream->Seek(0, 0);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->Read(2).c_str() << "\n";

  stream->sprintf("I have %d arms and %#x legs.", 2, 1025);
  cout << stream->Tell() << "\n";

  stream->Seek(0, 0);
  cout << "Data:" << stream->Read(1000).c_str() << "\n";

};

void test_ZipFileCreate() {
  AFF4Stream* file = AFF4FactoryOpen<AFF4Stream>("test.zip", "rw");

  // The backing file is given to the zip.
  AFF4Volume* zip = ZipFile::NewZipFile(file->urn);

  cout << "Volume URN:" << zip->urn.SerializeToString().data() << "\n";

  AFF4Stream *segment = zip->CreateMember("Foobar.txt");
  segment->Write("I am a segment!");

  // This is actually the same stream as above, we will simply get the same
  // pointer and so the new message will be appended to the old message.
  AFF4Stream *segment2 = zip->CreateMember("Foobar.txt");
  segment2->Write("I am another segment!");
};


void test_ZipFileRead() {
  AFF4Stream *file = AFF4FactoryOpen<AFF4Stream>("test.zip", "r");
  URN zip_urn;

  // The backing file is given to the zip.
  ZipFile *zip = ZipFile::NewZipFile(file->urn);

  if (!zip) {
    cout << "Cant open zip file.\n";
    return;
  };

  cout << "Found " << zip->members.size() << " files:\n";
  for(auto it=zip->members.begin(); it != zip->members.end(); it++) {
    cout << it->first << "\n";
  };

  unique_ptr<AFF4Stream> member = zip->OpenMember("Foobar.txt");
  cout << member->Read(100).c_str() << "\n";
};

void DumpOracle() {
  StringIO output;

  oracle.DumpToTurtle(output);

  cout << output.buffer;
};

/**
 * This test will automatically open the image, and its containing volume from
 * information stored in the AFF4 oracle.
 *
 * @param image_urn: The URN of the AFF4Image object to open.
 */
void test_OpenImageByURN(URN image_urn) {
  AFF4Image *image = AFF4FactoryOpen<AFF4Image>(image_urn);

  if (image) {
    cout << image->urn.value << "\n";
  };

  cout << "data:\n" << image->Read(100).c_str() << "\n";
};


void runTests() {
  URN image_urn = test_AFF4Image();

  oracle.Flush();

  DumpOracle();

  test_OpenImageByURN(image_urn);
  return;


  test_MemoryDataStore();

  unique_ptr<AFF4Stream> stream = StringIO::NewStringIO();

  test_AFF4Stream(stream.get());

  DumpOracle();

  AFF4Stream *file = AFF4FactoryOpen<AFF4Stream>("test_filename.bin", "w");

  if (file == NULL) {
    cout << "Failed to create file.\n";
    return;
  };

  test_AFF4Stream(file);

  test_ZipFileCreate();

  oracle.Flush();

  DumpOracle();

  oracle.Clear();

  test_ZipFileRead();

  return;


  return;

  test_AFF4Stream(StringIO::NewStringIO().get());

};


int main(int argc, char **argv) {
  runTests();
  oracle.Clear();

  return 0;
};
