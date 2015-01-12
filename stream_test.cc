#include "libaff4.h"

#include <iostream>

using std::cout;


void test_AFF4Image() {
  unique_ptr<AFF4Stream> file = FileBackedObject::NewFileBackedObject(
      "test.zip", "rw");

  // The backing file is given to the zip.
  unique_ptr<AFF4Volume> zip = ZipFile::NewZipFile(std::move(file));

  unique_ptr<AFF4Stream> image = AFF4Image::NewAFF4Image(
      "image.dd", *(zip.get()));

  image->Write("Hello wolrd!");
};

void test_MemoryDataStore() {
  unique_ptr<MemoryDataStore> store(new MemoryDataStore());
  unique_ptr<AFF4Stream> output = StringIO::NewStringIO();

  store->Set(URN("hello"), URN("World"), RDFBytes("foo"));

  RDFBytes result;

  store->Get(URN("hello"), URN("World"), result);
  cout << result.value << "\n";

  store->DumpToYaml(*output.get());

  cout << output->Tell() << "\n";
  output->Seek(0, 0);
  cout << output->ReadCString(1000).data() << "\n";
}

void test_AFF4Stream(AFF4Stream *stream) {
  cout << "Testing\n";
  cout << "*******\n";

  stream->Write("hello world");
  cout << stream->Tell() << "\n";

  stream->Seek(0, 0);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->ReadCString(1000).data() << "\n";
  cout << stream->Tell() << "\n";

  stream->Seek(-5, 2);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->ReadCString(1000).data() << "\n";

  stream->Seek(-5, 2);
  cout << stream->Tell() << "\n";
  stream->Write("Cruel world");
  stream->Seek(0, 0);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->ReadCString(1000).data() << "\n";
  cout << stream->Tell() << "\n";

  stream->Seek(0, 0);
  cout << stream->Tell() << "\n";
  cout << "Data:" << stream->ReadCString(2).data() << "\n";

  stream->sprintf("I have %d arms and %#x legs.", 2, 1025);
  cout << stream->Tell() << "\n";

  stream->Seek(0, 0);
  cout << "Data:" << stream->ReadCString(1000).data() << "\n";

};

void test_ZipFile() {
  unique_ptr<AFF4Stream> file = FileBackedObject::NewFileBackedObject(
      "test.zip", "rw");

  // The backing file is given to the zip.
  unique_ptr<AFF4Volume> zip = ZipFile::NewZipFile(std::move(file));

  // Files are added in the order of destruction, which in C++ is in reverse
  // order of creation. Therefore the zipfile directory will only contain "I am
  // a segment".
  unique_ptr<AFF4Stream> segment = zip->CreateMember("Foobar.txt");
  segment->Write("I am a segment!");

  unique_ptr<AFF4Stream> segment2 = zip->CreateMember("Foobar.txt");
  segment2->Write("I am another segment!");
};


void runTests() {
  test_AFF4Stream(StringIO::NewStringIO().get());

  test_AFF4Image();

  test_MemoryDataStore();

  test_ZipFile();
  return;


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
