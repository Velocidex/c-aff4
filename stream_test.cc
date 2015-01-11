#include "libaff4.h"

#include <iostream>

using std::cout;

void test_MemoryDataStore() {
  MemoryDataStore store;

  store.Set(URN("hello"), URN("World"), RDFBytes("foo"));

  RDFBytes result(store.Get(URN("hello"), URN("World")));

  cout << result.value << "\n";
}

void test_AFF4Stream(AFF4Stream *string) {
  cout << "Testing\n";
  cout << "*******\n";

  string->Write("hello world");
  cout << string->Tell() << "\n";

  string->Seek(0, 0);
  cout << string->Tell() << "\n";
  cout << "Data:" << string->Read(1000) << "\n";
  cout << string->Tell() << "\n";

  string->Seek(-5, 2);
  cout << string->Tell() << "\n";
  cout << "Data:" << string->Read(1000) << "\n";

  string->Seek(-5, 2);
  cout << string->Tell() << "\n";
  string->Write("Cruel world");
  string->Seek(0, 0);
  cout << string->Tell() << "\n";
  cout << "Data:" << string->Read(1000) << "\n";
  cout << string->Tell() << "\n";

  string->Seek(0, 0);
  cout << string->Tell() << "\n";
  cout << "Data:" << string->Read(2) << "\n";

  string->sprintf("I have %d arms and %#x legs.", 2, 1025);
  cout << string->Tell() << "\n";

  string->Seek(0, 0);
  cout << "Data:" << string->Read(1000) << "\n";

};

void test_ZipFile() {
  unique_ptr<FileBackedObject> file = FileBackedObject::NewFileBackedObject(
      "test.zip", "rw");

  // The backing file is given to the zip.
  unique_ptr<ZipFile> zip = ZipFile::NewZipFile(std::move(file));

  unique_ptr<ZipFileSegment> segment = zip->CreateMember("Foobar.txt");
  segment->Write("I am a segment!");
};


void runTests() {
  test_ZipFile();

  test_MemoryDataStore();
  test_AFF4Stream(StringIO::NewStringIO().get());

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
