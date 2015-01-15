#ifndef     AFF4_IO_H_
#define     AFF4_IO_H_

#include <unordered_map>
#include <string>
#include <memory>
#include <fstream>
#include "data_store.h"
#include "aff4_utils.h"
#include "rdf.h"

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::ofstream;
using std::ifstream;


template<typename T>
RDFValue *fCreate() {
  return new T();
};

struct AFF4Schema {
  string classname;
  RDFValue* (*constructor)();
};


/**
    All AFF4 objects extend this basic object.

    AFF4Objects present an external API for users. There are two main ways to
    instantiate an AFF4Object:

    1) To create a new object, one uses the static Factory function defined in
       the AFF4 public interface. For example:

       static unique_ptr<ZipFile> NewZipFile(unique_ptr<AFF4Stream> stream);

       This will return a new instance of the AFF4Object. When the object is
       deleted, it will be flushed to the AFF4 resolver.

    2) Similarly, to open an existing object, the static factory function can be
       used. e.g.:

       static unique_ptr<ZipFile> OpenZipFile(URN urn);

       Typically only the URN is required as a parameter. Note that if the
       object stored at the specified URN is not of the required type, this
       method will fail and return NULL.

    Internally all AFF4 objects must be able to be recreated exactly from the
    AFF4 resolver.  Therefore the following common pattern is followed:

       static unique_ptr<XXXX> NewXXXX(arg1, arg2, arg3) {
           this->Set(predicate1, arg1);
           this->Set(predicate2, arg2);
           this->Set(predicate3, arg2);

           // instantiate the object.
           XXXX(arg1, arg2, arg3);

           // Now, when the object is destroyed the predicates set above will
           // be flushed to storage.
       };

       static unique_ptr<XXXX> OpenXXXX(URN urn) {
          InitAFF4Attributes();  // Load all attributes from the resolver.

          arg1 = this->Get(predicate1);
          arg2 = this->Get(predicate2);
          arg3 = this->Get(predicate3);

          return unique_ptr<XXXX>(new XXX(arg1, arg2, arg3));
       };

 **/
class AFF4Object {

 protected:
  // AFF4 objects store attributes.
  AFF4_Attributes synced_attributes;

  AFF4Schema schema[1] = {
    {"URN", &fCreate<URN>}
  };

  string name = "AFF4Object";

 public:
  URN urn;

  // AFF4 objects are created using the following pattern:
  // obj = AFF4_FACTORY.Create(type)
  // obj.Set(attribute1, value1)
  // obj.Set(attribute2, value2)
  // if (!obj.finish()) {
  //     Failed to create object.
  // }
  AFF4Object(); // Used by the factory for generic
                                    // instantiation.

  // By defining a virtual destructor this allows the destructor of derived
  // objects to be called when deleting a pointer to a base object.
  virtual ~AFF4Object() {};

  AFF4Status Flush();
};


class AFF4Stream: public AFF4Object {
 protected:
  ssize_t readptr;
  int size;             // How many bytes are used in the stream?
  bool _dirty = false;  // Is this stream modified?

 public:
  string name = "AFF4Stream";

  AFF4Stream(): readptr(0), size(0) {};

  // Convenience methods.
  int Write(const unique_ptr<string> &data);
  int Write(const string &data);
  int Write(const char data[]);
  int sprintf(string fmt, ...);

  // Read a null terminated string.
  string ReadCString(size_t length);
  int ReadIntoBuffer(void *buffer, size_t length);

  // The following should be overriden by derived classes.
  virtual void Seek(int offset, int whence);
  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Tell();
  virtual size_t Size();
};

class StringIO: public AFF4Stream {
 public:
  string buffer;

  StringIO() {};
  StringIO(string data): buffer(data) {};

  // Convenience constructors.
  static unique_ptr<StringIO> NewStringIO() {
    unique_ptr<StringIO> result(new StringIO());

    return result;
  };

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Size();

  using AFF4Stream::Write;
};


class FileBackedObject: public AFF4Stream {
 protected:
  int fd;

 public:
  string name = "FileBackedObject";

  FileBackedObject() {};

  static unique_ptr<FileBackedObject> NewFileBackedObject(
      string filename, string mode);

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Size();

};

/**
   Volumes allow for other objects to be stored within them.

   This means that to create an object within a volume, one must create the
   volume first, then call CreateMember() to add the object to the volume. The
   new object will be flushed to the volume when it is destroyed.

   To open an existing object one needs to:

   1) Open the volume - this will populate the resolver with the information
      within the volume.

   2) Directly open the required URN. The AFF4 resolver will know which volume
      (or volumes) the required object lives in by itself.

   Note that when using a persistent resolver step 1 might be skipped if the
   location of the volume has not changed.
*/
class AFF4Volume: public AFF4Object {
 public:
  virtual unique_ptr<AFF4Stream> CreateMember(string filename) = 0;
};


#define DEBUG_OBJECT(fmt, ...) printf(fmt "\n", ## __VA_ARGS__)

#define CHECK(cond, fmt, ...) if(cond) {                \
    printf(fmt "\n", ## __VA_ARGS__);                   \
    exit(-1);                                           \
  }


#endif      /* !AFF4_IO_H_ */
