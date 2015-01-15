#ifndef AFF4_RDF_H_
#define AFF4_RDF_H_

#include <string>
#include <memory>
#include "string.h"
#include "aff4_utils.h"
#include "aff4_errors.h"

using std::string;
using std::unique_ptr;
using std::vector;

// Base class for all RDF Values. An RDFValue is an object which knows how to
// serialize and unserialize itself from a DataStoreObject.
class RDFValue {
 public:
  string name = "";
  string rdf_type = "";

  // RDFValues must provide methods for serializing and unserializing.
  virtual string SerializeToString() const {
    return "";
  };

  virtual AFF4Status UnSerializeFromString(const char *data, int length) {
    return GENERIC_ERROR;
  };

  AFF4Status UnSerializeFromString(const string data) {
    return UnSerializeFromString(data.data(), data.size());
  };

  AFF4Status Set(const string data) {
    return UnSerializeFromString(data.c_str(), data.size());
  };

  virtual ~RDFValue(){};
};

static const char* const lut = "0123456789ABCDEF";

/* These are the objects which are stored in the data store */
class RDFBytes: public RDFValue {
 public:
  string name = "RDFBytes";
  string rdf_type = "xsd:hexBinary";
  string value;

  RDFBytes(string data): RDFBytes(data.c_str(), data.size()) {};

  RDFBytes(const char * data, unsigned int length): value(data, length) {}

  RDFBytes() {};

  string SerializeToString() const;
  AFF4Status UnSerializeFromString(const char *data, int length);
};


class XSDString: public RDFBytes {
 public:
  string name = "XSDString";
  string rdf_type = "xsd:string";

  XSDString(const XSDString &other): RDFBytes(other) {};
  XSDString(string data): RDFBytes(data.c_str(), data.size()) {};
  XSDString(const char * data): RDFBytes(data, strlen(data)) {};
  XSDString() {};

  string SerializeToString() const;
  AFF4Status UnSerializeFromString(const char *data, int length);
};


class URN: public XSDString {
 public:
  URN(const char * data): XSDString(data) {};
  URN(string data): XSDString(data) {};
  URN() {};
};

#endif // AFF4_RDF_H_
