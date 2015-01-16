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


void test_AFF4Image() {
  unique_ptr<AFF4Stream> file = FileBackedObject::NewFileBackedObject(
      "test.zip", "rw");

  // The backing file is given to the zip.
  unique_ptr<AFF4Volume> zip = ZipFile::NewZipFile(std::move(file));

  unique_ptr<AFF4Stream> image = AFF4Image::NewAFF4Image(
      "image.dd", *zip);

  image->Write("Hello wolrd!");
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
  unique_ptr<AFF4Stream> file = FileBackedObject::NewFileBackedObject(
      "test.zip", "w");

  // The backing file is given to the zip.
  unique_ptr<AFF4Volume> zip = ZipFile::NewZipFile(std::move(file));

  cout << "Volume URN:" << zip->urn.SerializeToString().data() << "\n";

  // Files are added in the order of destruction, which in C++ is in reverse
  // order of creation. Therefore the zipfile directory will only contain "I am
  // a segment".
  unique_ptr<AFF4Stream> segment = zip->CreateMember("Foobar.txt");
  segment->Write("I am a segment!");

  unique_ptr<AFF4Stream> segment2 = zip->CreateMember("Foobar.txt");
  segment2->Write("I am another segment!");
};


void test_ZipFileRead() {
  unique_ptr<AFF4Stream> file = FileBackedObject::NewFileBackedObject(
      "test.zip", "r");

  // The backing file is given to the zip.
  unique_ptr<ZipFile> zip = ZipFile::OpenZipFile(std::move(file));

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

void runTests() {
  test_ZipFileCreate();
  test_AFF4Image();
  test_ZipFileRead();

  DumpOracle();
  return;

  test_AFF4Stream(StringIO::NewStringIO().get());

  test_MemoryDataStore();

  unique_ptr<AFF4Stream> file = FileBackedObject::NewFileBackedObject(
      "test_filename.bin", "w");

  if (file == NULL) {
    cout << "Failed to create file.\n";
    return;
  };

  test_AFF4Stream(file.get());
};


int main(int argc, char **argv) {
  runTests();

  return 0;
};
