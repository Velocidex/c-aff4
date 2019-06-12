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

#ifndef  SRC_RDF_H_
#define  SRC_RDF_H_
#include <memory>
#include <raptor2/raptor2.h>
#include <spdlog/fmt/ostr.h>
#include <string>
#include <string>

// #include "aff4/config.h"
#include "aff4/aff4_errors.h"
#include "aff4/aff4_registry.h"
#include "aff4/aff4_utils.h"

namespace aff4 {

/**
 * @file
 * @author scudette <scudette@localhost>
 * @date   Mon Jan 19 09:52:39 2015
 *
 * @brief  Define some common RDF value types.
 *
 *
 */

#define URN_PATH_SEPARATOR "/"

class DataStore;
class URN;

/**
 * An RDFValue object is one which knows how to serialize itself from a string
 * and back again.
 *
 */
class RDFValue {
  protected:
    DataStore* resolver;

  public:
    explicit RDFValue(DataStore* resolver): resolver(resolver) {}
    RDFValue(): resolver(nullptr) {}
    virtual ~RDFValue() {}

    virtual raptor_term* GetRaptorTerm(raptor_world* world) const {
        UNUSED(world);
        return nullptr;
    }

    // RDFValues must provide methods for serializing and unserializing.
    virtual std::string SerializeToString() const {
        return "";
    }

    virtual AFF4Status UnSerializeFromString(const char* data, int length) {
        UNUSED(data);
        UNUSED(length);
        return GENERIC_ERROR;
    }

    AFF4Status UnSerializeFromString(const std::string data) {
        return UnSerializeFromString(data.data(), data.size());
    }

    AFF4Status Set(const std::string data) {
        return UnSerializeFromString(data.c_str(), data.size());
    }
};


// Support direct logging of RDFValues (like URNs etc.)
std::ostream& operator<<(std::ostream& os, const RDFValue& c);

// A Global Registry for RDFValue. This factory will provide the correct
// RDFValue instance based on the turtle type URN. For example xsd:integer ->
// XSDInteger().
extern ClassFactory<RDFValue> RDFValueRegistry;

template<class T>
class RDFValueRegistrar {
  public:
    explicit RDFValueRegistrar(std::string name) {
        // register the class factory function
        RDFValueRegistry.RegisterFactoryFunction(
            name,
        [](DataStore *resolver, const URN *urn) -> RDFValue * {
            UNUSED(urn);
            return new T(resolver);
        });
    }
};


static const char* const lut = "0123456789ABCDEF";

/**
 * RDFBytes is an object which stores raw bytes. It serializes into an
 * xsd:hexBinary type.
 *
 */
class RDFBytes: public RDFValue {
  public:
    std::string value;

    explicit RDFBytes(std::string data):
        RDFValue(), value(data) {}

    RDFBytes(const char* data, unsigned int length):
        RDFValue(), value(data, length) {}

    explicit RDFBytes(DataStore* resolver): RDFValue(resolver) {}

    RDFBytes() {}

    std::string SerializeToString() const;
    AFF4Status UnSerializeFromString(const char* data, int length);
    raptor_term* GetRaptorTerm(raptor_world* world) const;

    bool operator==(const RDFBytes& other) const {
        return this->value == other.value;
    }

    bool operator==(const std::string& other) const {
        return this->value == other;
    }
};

/**
 * An XSDString is a printable string. It serializes into an xsd:string type.
 *
 */
class XSDString: public RDFBytes {
  public:
    XSDString(std::string data):
        RDFBytes(data.c_str(), data.size()) {}

    XSDString(const char* data):
        RDFBytes(data, strlen(data)) {}

    explicit XSDString(DataStore* resolver): RDFBytes(resolver) {}

    XSDString() {}

    std::string SerializeToString() const;
    AFF4Status UnSerializeFromString(const char* data, int length);
    raptor_term* GetRaptorTerm(raptor_world* world) const;
};

// Hash types

class MD5Hash : public XSDString {
  public:
    using XSDString::XSDString;
    raptor_term* GetRaptorTerm(raptor_world* world) const;
};

class SHA1Hash : public XSDString {
  public:
    using XSDString::XSDString;
    raptor_term* GetRaptorTerm(raptor_world* world) const;
};

class SHA256Hash : public XSDString {
  public:
    using XSDString::XSDString;
    raptor_term* GetRaptorTerm(raptor_world* world) const;
};

class SHA512Hash : public XSDString {
  public:
    using XSDString::XSDString;
    raptor_term* GetRaptorTerm(raptor_world* world) const;
};

class Blake2BHash : public XSDString {
  public:
    using XSDString::XSDString;
    raptor_term* GetRaptorTerm(raptor_world* world) const;
};


/**
 * A XSDInteger stores an integer. We can parse xsd:integer, xsd:int and
 * xsd:long.
 *
 */
class XSDInteger: public RDFValue {
  public:
    uint64_t value;

    explicit XSDInteger(uint64_t data):
        RDFValue(nullptr), value(data) {}

    explicit XSDInteger(DataStore* resolver):
        RDFValue(resolver), value(0) {}

    XSDInteger() : value(0){}

    std::string SerializeToString() const;

    AFF4Status UnSerializeFromString(const char* data, int length);

    raptor_term* GetRaptorTerm(raptor_world* world) const;
};


/**
 * A XSDBoolean stores a boolean. We can parse xsd:boolean.
 *
 */
class XSDBoolean: public RDFValue {
  public:
    bool value;

    explicit XSDBoolean(bool data):
        RDFValue(nullptr), value(data) {}

    explicit XSDBoolean(DataStore* resolver):
        RDFValue(resolver), value(false) {}

    XSDBoolean() : value(false){}

    std::string SerializeToString() const;

    AFF4Status UnSerializeFromString(const char* data, int length);

    raptor_term* GetRaptorTerm(raptor_world* world) const;
};

/**
 * An RDFValue to store and parse a URN.
 *
 */
class URN: public XSDString {
  protected:
    //std::string original_filename;

  public:
    /**
     * Create a new URN from a filename.
     *
     * @param filename: The filename to convert.
     * @param windows_filename: If true interpret the filename as a windows
     * filename, else it will be considered a unix filename. Currently windows and
     * unix filenames are escaped differently.
     *
     * @return a URN object.
     */
    static URN NewURNFromOSFilename(std::string filename, bool windows_filename,
                                    bool absolute_path = true);

    /**
     * Create a URN from filename.
     * This variant of the function automatically selects the type.
     *
     * @param filename
     * @param absolute_path: If specified we convert the filename to an absolute
     * path first.
     *
     * @return
     */
    static URN NewURNFromFilename(std::string filename, bool absolute_path = true);

    /**
     * Returns the current URN as a filename.
     *
     *
     * @return If this is a file:// URN, returns the filename, else "".
     */
    std::string ToFilename() const;

    URN(const char* data);
    URN(const std::string& data): URN(data.c_str()) {};
    explicit URN(DataStore* resolver): URN() {
        UNUSED(resolver);
    };
    URN() {};

    URN Append(const std::string& component) const;

    raptor_term* GetRaptorTerm(raptor_world* world) const;

    // Returns the URN's Scheme, Path and Domain parts. NOTE: This is
    // not a complete URI parser! It only supports AFF4 and FILE urls.
    std::string Scheme() const;
    std::string Path() const;
    std::string Domain() const;

    /**
     * returns the path of the URN relative to ourselves.
     *
     * If the urn contains us as a common prefix, we remove that and return a
     * relative path. Otherwise we return the complete urn as an absolution path.
     *
     * @param urn: The urn to check.
     *
     * @return A string representing the path.
     */
    std::string RelativePath(const URN urn) const;

    AFF4Status Set(const URN data) {
        value = data.SerializeToString();
        return STATUS_OK;
    }

    AFF4Status Set(const std::string& data) {
        value = data;
        return STATUS_OK;
    }

    bool operator<(const URN& other) const noexcept {
        return value < other.value;
    }
};

} // namespace aff4

// custom specialization of std::hash injected into std namespace.
namespace std {
    template<> struct hash<aff4::URN> {
        typedef std::size_t result_type;
        result_type operator()(aff4::URN const& s) const {
                result_type const h1 (std::hash<std::string>{}(s.value));
                return h1;
        }
    };
}



#endif  // SRC_RDF_H_
