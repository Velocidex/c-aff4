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

#ifndef  SRC_DATA_STORE_H_
#define  SRC_DATA_STORE_H_

#include "aff4/config.h"

#include "aff4/aff4_base.h"
#include "aff4/threadpool.h"
#include "spdlog/spdlog.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>
#include <fstream>
#include "aff4/aff4_utils.h"
#include <string.h>

#include "aff4/rdf.h"

namespace aff4 {

// Forward declerations for basic AFF4 types.
class AFF4Object;
class AFF4Stream;
class AFF4Volume;
class DataStore;
class AFF4SymbolicStream;

// AFF4_Attributes are a collection of RDFValue objects, keyed by attributes.
typedef std::unordered_map<std::string, std::vector<std::shared_ptr<RDFValue>>> AFF4_Attributes;

// Deleter for AFF4Flusher
struct AFF4Flusher_deleter {
    template<typename AFF4ObjectType>
    void operator()(AFF4ObjectType * obj) {
        obj->Flush();
        delete obj;
    }
};

// Special type of unique_ptr that flushes an AFF4 object before freeing
template<typename AFF4ObjectType>
using AFF4Flusher = std::unique_ptr<AFF4ObjectType, AFF4Flusher_deleter>;


// Constructs an AFF4 Object and wraps it in a AFF4Flusher
template<typename AFF4ObjectType, typename ...Args>
AFF4Flusher<AFF4ObjectType> make_flusher(Args && ...args) {
    return AFF4Flusher<AFF4ObjectType>(new AFF4ObjectType(std::forward<Args>(args)...));
}


/**
 * @file   data_store.h
 * @author scudette <scudette@google.com>
 * @date   Fri Jan 23 12:11:05 2015
 *
 * @brief This file defines the AFF4 data store abstraction.
 *
 * AFF4 relies on the data store to maintain relational information about the
 * AFF4 universe.
 *
 */

struct DataStoreOptions {
    std::shared_ptr<spdlog::logger> logger = aff4::get_logger();
    int threadpool_size = 1;

    DataStoreOptions(std::shared_ptr<spdlog::logger> logger,  int threadpool_size):
        logger(logger), threadpool_size(threadpool_size){};

    DataStoreOptions() : logger(aff4::get_logger()){};
};


/** The abstract data store.

    Data stores know how to serialize RDF statements of the type:

    subject predicate value

    Where both subject and predicate are a URN into the AFF4 space, and value is
    a serialized RDFValue.
*/
class DataStore {
    friend class AFF4Object;

  public:
    DataStore();
    DataStore(DataStoreOptions options);
    virtual ~DataStore() {}

    DataStore(DataStore&&) = default;

    DataStore& operator=(DataStore&&) = default;

    // All logging directives go through this handle. It can be
    // replaced with a different handler if needed.
    std::shared_ptr<spdlog::logger> logger;

    /// You can add new namespaces here for turtle serialization.
    std::vector<std::pair<std::string, std::string>> namespaces;

    // A global thread pool for general use.
    std::unique_ptr<ThreadPool> pool;

    virtual void Set(const URN& urn, const URN& attribute,
                     RDFValue* value, bool replace = true) = 0;

    virtual AFF4Status Get(const URN& urn, const URN& attribute,
                           RDFValue& value ) = 0;

    virtual AFF4Status Get(const URN& urn, const URN& attribute,
                std::vector<std::shared_ptr<RDFValue>>& values) = 0;

    /**
     * Does the given URN have the given attribute set to the given value.
     */
    virtual bool HasURNWithAttributeAndValue(
        const URN& urn, const URN& attribute, const RDFValue& value) = 0;

    /**
     * Does the given URN have the given attribute set
     */
    virtual bool HasURNWithAttribute(const URN& urn, const URN& attribute) = 0;
    /**
     * Does the datastore know the given urn
     */
    virtual bool HasURN(const URN& urn) = 0;

    virtual void Set(const URN& urn, const URN& attribute,
                     std::shared_ptr<RDFValue> value,
                     bool replace = true) = 0;

    virtual AFF4Status DeleteSubject(const URN& urn) = 0;

    virtual std::vector<URN> SelectSubjectsByPrefix(const URN& prefix) = 0;

    /**
     * Query the data for all subjects that have the given attribute,
     * which is optionally set to the value;
     *
     * @param attribute The required attribute.
     * @param value The optional value which to check against.
     * @return A vector of Resource URNs that have the given attribute.
     */
    virtual std::unordered_set<URN> Query(
        const URN& attribute, const RDFValue* value = nullptr) = 0;

    /**
     * Get the AFF4 Attributes for the given urn.
     *
     * @param urn The Resource URN to enquire about.
     * @return All known AFF4_Attributes for the given urn. If the urn
     * is unknown, an empty attribute list is returned.
     */
    virtual AFF4_Attributes GetAttributes(const URN& urn) = 0;

#ifdef AFF4_HAS_LIBYAML_CPP
    // Dump ourselves to a yaml file.
    virtual AFF4Status DumpToYaml(AFF4Stream& output,
                                  bool verbose = false) = 0;

    virtual AFF4Status LoadFromYaml(AFF4Stream& output) = 0;
#endif

    virtual AFF4Status DumpToTurtle(AFF4Stream& output, URN base,
                                    bool verbose = false) = 0;

    virtual AFF4Status LoadFromTurtle(AFF4Stream& output) = 0;

    /**
     * Clear all data.
     *
     *
     * @return Status
     */
    virtual AFF4Status Clear() = 0;


    /**
     * Prints out the contents of the resolver to STDOUT. Used for debugging.
     *
     */
    void Dump(bool verbose = true);

  protected:
    /// These types will not be dumped * to turtle files.
    std::unordered_map<
        std::string, std::unordered_set<std::string>> suppressed_rdftypes;

    // Should we suppress this tuple from the turtle file? We do not
    // want to write facts in the turtle file which are self evident
    // from context. For example, the AFF4_STORED attribute of
    // AFF4_ZIP_SEGMENT_TYPE are inferred from the Zip container
    // itself.
    virtual bool ShouldSuppress(const URN& subject, const URN& predicate,
                                const std::string& value);

};


/** A purely in memory data store.

    This data store can be initialized and persisted into a Yaml file.
*/
class MemoryDataStore: public DataStore {
  private:
    // Store a collection of AFF4_Attributes at each URN.
    std::unordered_map<std::string, AFF4_Attributes> store;

  public:
    MemoryDataStore() = default;

    MemoryDataStore(MemoryDataStore&&) = default;

    MemoryDataStore& operator=(MemoryDataStore&&) = default;

    MemoryDataStore(DataStoreOptions options): DataStore(options) {}

    virtual ~MemoryDataStore();

    /**
     * Set the RDFValue in the data store. Note that the data store will retain
     * ownership of the value, and therefore callers may not use it after this
     * call.
     *
     * @param urn: The subject to set the attribute for.
     * @param attribute: The attribute to set.
     * @param value: The value.
     */
    virtual void Set(const URN& urn, const URN& attribute, RDFValue* value,
                     bool replace = true) override;

    virtual void Set(const URN& urn, const URN& attribute,
                     std::shared_ptr<RDFValue> value, bool replace = true) override;

    AFF4Status Get(const URN& urn, const URN& attribute, RDFValue& value) override;
    AFF4Status Get(const URN& urn, const URN& attribute,
                   std::vector<std::shared_ptr<RDFValue>>& value) override;

    bool HasURN(const URN& urn) override;
    bool HasURNWithAttribute(const URN& urn, const URN& attribute) override;
    bool HasURNWithAttributeAndValue(
        const URN& urn, const URN& attribute, const RDFValue& value) override;

    std::unordered_set<URN> Query(
        const URN& attribute, const RDFValue* value = nullptr) override;

    AFF4_Attributes GetAttributes(const URN& urn) override;

    AFF4Status DeleteSubject(const URN& urn) override;

    std::vector<URN> SelectSubjectsByPrefix(const URN& prefix) override;

#ifdef AFF4_HAS_LIBYAML_CPP
    AFF4Status DumpToYaml(AFF4Stream& output, bool verbose = false) override;
    AFF4Status LoadFromYaml(AFF4Stream& output) override;
#endif

    AFF4Status DumpToTurtle(AFF4Stream& output, URN base,
                            bool verbose = false) override;

    AFF4Status LoadFromTurtle(AFF4Stream& output) override;

    AFF4Status Clear() override;
};

} // namespace aff4

#endif  //  SRC_DATA_STORE_H_
