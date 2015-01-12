#ifndef  _AFF4_DATA_STORE_H_
#define  _AFF4_DATA_STORE_H_

// This file defines the AFF4 data store abstraction.
#include <unordered_map>
#include <string>
#include <memory>
#include <fstream>

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::ofstream;
using std::ifstream;


// Forward declerations for basic AFF4 types.
class AFF4Object;
class AFF4Stream;
class AFF4Volume;


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

// Base class for all RDF Values. An RDFValue is an object which knows how to
// serialize and unserialize itself from a DataStoreObject.
class RDFValue {
 protected:
  virtual ~RDFValue(){};

 public:
  string name = "";

  // RDFValues must provide methods for serializing and unserializing.
  virtual DataStoreObject Serialize() const = 0;
  virtual int UnSerialize(DataStoreObject &data) = 0;
};

/* These are the objects which are stored in the data store */
class RDFBytes: public RDFValue {
 public:
  string name = "RDFBytes";

  string value;

  RDFBytes(string data): value(data) {};
  RDFBytes(const char * data): value(data) {};
  RDFBytes(const DataStoreObject& data): value(data.string_data) {};
  RDFBytes(){};

  DataStoreObject Serialize() const {
    return DataStoreObject(value);
  };

  int UnSerialize(DataStoreObject &data) {
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


/** The abstract data store.

    Data stores know how to serialize RDF statements of the type:

    subject predicate value

    Where both subject and predicate are a URN into the AFF4 space, and value is
    a serialized RDFValue.
 */
class DataStore {
 public:
  virtual void Set(const URN &urn, const URN &attribute, const RDFValue &value) = 0;
  virtual int Get(const URN &urn, const URN &attribute, RDFValue &value) = 0;

  // Dump ourselves to a yaml file.
  virtual int DumpToYaml(AFF4Stream &output) = 0;
};


/** A purely in memory data store.

    This data store can be initialized and persisted into a Yaml file.
*/
class MemoryDataStore: public DataStore {

 private:
  // Store a collection of AFF4_Attributes at each URN.
  unordered_map<string, AFF4_Attributes> store;

 public:
  void Set(const URN &urn, const URN &attribute, const RDFValue &value);
  int Get(const URN &urn, const URN &attribute, RDFValue &value);

  virtual int DumpToYaml(AFF4Stream &output);
};

#endif //  _AFF4_DATA_STORE_H_
