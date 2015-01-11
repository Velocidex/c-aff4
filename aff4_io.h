#ifndef     AFF4_IO_H_
#define     AFF4_IO_H_

#include <unordered_map>
#include <string>
#include <memory>
#include <fstream>

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::ofstream;
using std::ifstream;

enum DataStoreObjectWireType {
  STRING,
  INTEGER
};

/* These are the objects which are stored in the data store */
class DataStoreObject {
 public:
  // The data store can only store the following primitive types.
  int int_data = 0;
  string string_data;
  DataStoreObjectWireType wire_type = STRING;

  DataStoreObject(string data): string_data(data), wire_type(STRING) {};
  DataStoreObject(int data): int_data(data), wire_type(INTEGER) {};
  DataStoreObject(){};

};


// Base class for all RDF Values.
class RDFValue {
 protected:
  virtual ~RDFValue(){};

 public:
  // RDFValues must provide methods for serializing and unserializing.
  virtual DataStoreObject Serialize() const = 0;
  virtual int UnSerialize(DataStoreObject data) = 0;
};


/* These are the objects which are stored in the data store */
class RDFBytes: public RDFValue {
 public:
  string value;

  RDFBytes(string data): value(data) {};
  RDFBytes(const char * data): value(data) {};
  RDFBytes(const DataStoreObject& data): value(data.string_data) {};
  RDFBytes(){};

  DataStoreObject Serialize() const {
    return DataStoreObject(value);
  };

  int UnSerialize(DataStoreObject data) {
    value = data.string_data;

    return true;
  };
};

class URN: public RDFBytes {
 public:
  URN(string data): RDFBytes(data) {};
  URN(const char * data): RDFBytes(data) {};
  URN(const DataStoreObject &data): RDFBytes(data.string_data) {};
  URN(){};

};

// AFF4_Attributes are a collection of data store objects, keyed by attributes.
typedef unordered_map<string, DataStoreObject> AFF4_Attributes;


/** The abstract data store. */
class DataStore {
 public:
  virtual void Set(URN urn, URN attribute, const RDFValue &value) = 0;
  virtual DataStoreObject Get(URN urn, URN attribute) = 0;
};

/** A purely in memory data store. */
class MemoryDataStore: public DataStore {

 private:
  // Store a collection of AFF4_Attributes at each URN.
  unordered_map<string, AFF4_Attributes> store;

 public:
  void Set(URN urn, URN attribute, const RDFValue &value);
  DataStoreObject Get(URN urn, URN attribute);

};


/** All AFF4 objects extend this basic object. **/
class AFF4Object {

 protected:
  // AFF4 objects store attributes.
  AFF4_Attributes synced_attributes;

  // By defining a virtual destructor this allows the destructor of derived
  // objects to be called when deleting a pointer to a base object.
  virtual ~AFF4Object(){};

 public:
  URN urn;

  // AFF4 objects are created using the following pattern:
  // obj = AFF4_FACTORY.Create(type)
  // obj.Set(attribute1, value1)
  // obj.Set(attribute2, value2)
  // if (!obj.finish()) {
  //     Failed to create object.
  // }
  AFF4Object() {}; // Used by the factory for generic instantiation.

  virtual bool finish();
};


class AFF4Stream: public AFF4Object {
 protected:
  size_t readptr;
  int size;             // How many bytes are used in the stream?
  bool _dirty = false;  // Is this stream modified?

 public:
  AFF4Stream(): readptr(0), size(0) {};

  // Convenience methods.
  int Write(const unique_ptr<string> &data);
  int Write(const string &data);
  int Write(const char data[]);
  int sprintf(string fmt, ...);

  // The following should be overriden by derived classes.
  virtual void Seek(int offset, int whence);
  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual size_t Tell();
  virtual int Size();
};

class StringIO: public AFF4Stream {
 protected:
  string buffer;

 public:
  StringIO(): buffer() {};

  // Convenience constructors.
  static unique_ptr<StringIO> NewStringIO() {
    unique_ptr<StringIO> result(new StringIO());

    return result;
  };

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual int Size();

  using AFF4Stream::Write;
};


class FileBackedObject: public AFF4Stream {
 protected:
  int fd;

 public:
  FileBackedObject() {};

  static unique_ptr<FileBackedObject> NewFileBackedObject(
      string filename, string mode);

  virtual string Read(size_t length);
  virtual int Write(const char *data, int length);
  virtual int Size();

};


class AFF4Volume: public AFF4Object {

};


#define DEBUG_OBJECT(fmt, ...) printf(fmt "\n", ## __VA_ARGS__)

#define CHECK(cond, fmt, ...) if(cond) {                \
    printf(fmt "\n", ## __VA_ARGS__);                   \
    exit(-1);                                           \
  }


#endif      /* !AFF4_IO_H_ */
