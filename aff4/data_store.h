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

struct AFF4ObjectCacheEntry {
  public:
    std::string key;
    AFF4Object* object = nullptr;
    int use_count = 0;
    bool flush_failed = false;

    AFF4ObjectCacheEntry* next, *prev;

    AFF4ObjectCacheEntry() {
        next = prev = this;
    }

    AFF4ObjectCacheEntry(std::string key, AFF4Object* object):
        key(key), object(object) {
        next = prev = this;
    }

    void unlink() {
        next->prev = prev;
        prev->next = next;
        next = prev = this;
    }

    void append(AFF4ObjectCacheEntry* entry) {
        // The entry must not exist on the list already.
        if (entry->next != entry->prev) {
            abort();
        }

        entry->next = next;
        next->prev = entry;

        entry->prev = this;
        next = entry;
    }

    ~AFF4ObjectCacheEntry() {
        unlink();

        // We can not call Flush on destruction because Flushing an object might try
        // to access another object in the cache, which we can not guarantee is not
        // already destroyed. Therefore we destroy the cache in two passes - first
        // we call Flush on all objects, then we destroy all objects without calling
        // their Flush methods.
        if (object) {
            delete object;
        }
    }
};

/**
 * This is the AFF4 object cache. We maintain an LRU cache of AFF4 objects so we
 * do not need to recreate them all the time.
 *
 * @param max_items
 */
class AFF4ObjectCache {
    friend DataStore;

  protected:
    AFF4ObjectCacheEntry lru_list;
    std::shared_ptr<spdlog::logger> logger;

    // When objects are returned from Get() they are placed on this map. This
    // ensures that they can not be deleted while in use. When objects are
    // returned to the cache with Return() they are placed in the normal lru.
    std::unordered_map<std::string, AFF4ObjectCacheEntry*> in_use;
    std::unordered_map<std::string, AFF4ObjectCacheEntry*> lru_map;
    size_t max_items = 10;

    /**
     *   Trim the size of the cache if needed.
     *
     * @return STATUS_OK if flushing objects is successful. Objects which can not
     * be flushed are not removed from the cache.
     */
    AFF4Status Trim_();

  public:
    bool trimming_disabled = false;

    explicit AFF4ObjectCache(
        const std::shared_ptr<spdlog::logger>& logger,
        int max_items = 10000)
        : logger(logger), max_items(max_items) {}

    virtual ~AFF4ObjectCache() {
        Flush();
    }

    /**
     * Remove all objects from the cache.
     *
     * @return
     */
    AFF4Status Flush();

    /**
     * Store a new object in the cache. The cache will own it from now on.
     *
     * @param urn
     * @param object
     */
    AFF4Status Put(AFF4Object* object, bool in_use = false);

    /**
     * Get an AFF4 object from the cache. The cache will always own the object,
     * only the reference is passed. The object is marked as in use in the cache
     * and will not be deleted until it returned with the Return() method. Callers
     * may not maintain external references or delete the object themselves.
     *
     * @param urn
     *
     * @return
     */
    AFF4Object* Get(const URN urn);

    /**
     * Objects are returned to the cache by calling this method. If the object is
     * not in the cache yet, we call Put() automatically. After this function
     * returns, references to object are no longer valid. This function is
     * normally called automatically from AFF4ScopedPtr and therefore we cant
     * provide a meaningful return value.
     *
     * @param object
     */
    void Return(AFF4Object* object);


    /**
     * Remove the object from the cache.
     *
     * @param urn
     *
     * @return STATUS_OK if Flushing the object worked. If there is an error the
     * object can not be removed from the cache.
     */
    AFF4Status Remove(AFF4Object* object);

    void Dump();
};


/**
 * AFF4 objects returned from a resolver remain owned by the resolver. When the
 * caller no longer uses them, they will be returned to the resolver. This
 * scoped ptr retains the ownership of the object. When the AFF4ScopedPtr goes
 * out of scope the object will be returned to the resolver.
 *
 * @param p: An AFF4 object
 * @param resolver: A reference to the resolver. Note that this resolver is
 *   assumed to outlive the AFF4ScopedPtr itself.
 */
template<typename AFF4ObjectType>
class AFF4ScopedPtr {
  protected:
    AFF4ObjectType* ptr_;
    DataStore* resolver_;

  public:
    AFF4ScopedPtr(): ptr_(0), resolver_(nullptr) {}
    explicit AFF4ScopedPtr(AFF4ObjectType* p, DataStore* resolver):
    ptr_(p), resolver_(resolver) {
        if (resolver == nullptr) abort();
    }

    ~AFF4ScopedPtr() {
        // When we destruct we return the underlying pointer to the DataStore.
        if (ptr_) {
            ptr_->Return();
        }
    }

    template<class AFF4ObjectOtherType>
    AFF4ScopedPtr<AFF4ObjectOtherType> cast() {
        return AFF4ScopedPtr<AFF4ObjectOtherType>(release(), resolver_);
    }

    AFF4ObjectType* operator->() const {
        if(ptr_ == nullptr) abort();
        return ptr_;
    }

    bool operator!(void)  {
        return ptr_ ? false : true;
    }

    AFF4ObjectType& operator*()  {
        return *ptr_;
    }

    AFF4ObjectType* get() const {
        return ptr_;
    }

    AFF4ObjectType* release() {
        AFF4ObjectType* ret = ptr_;
        ptr_ = nullptr;
        return ret;
    }

    void reset(AFF4ObjectType* p) {
        ptr_ = p;
    }

    AFF4ScopedPtr(AFF4ScopedPtr&& other) {
        ptr_ = other.release();
        resolver_ = other.resolver_;
    }

    AFF4ScopedPtr& operator=(AFF4ScopedPtr&& other) {
        if (this != &other) {
            this->~AFF4ScopedPtr();
            new (this) AFF4ScopedPtr(std::forward<AFF4ScopedPtr>(other));
        }

        return (*this);
    }

    AFF4ScopedPtr(const AFF4ScopedPtr& other) = delete;
    void operator=(const AFF4ScopedPtr& other) = delete;
};


/**
 * @file   data_store.h
 * @author scudette <scudette@google.com>
 * @date   Fri Jan 23 12:11:05 2015
 *
 * @brief This file defines the AFF4 data store abstraction.
 *
 * AFF4 relies on the data store to maintain relational information about the
 * AFF4 universe. This relation information is used to reconstruct objects which
 * have been previously stored in this data store.
 *
 * Note: In this implementation the data store caches all AFF4 objects which
 * have been produced and flushes them when the DataStore::Flush() method is
 * called. The Flush() method is also called during object destruction.

 * This essentially defines a transaction, for example, to open an AFF4 Zip
 * volume, add a new image to it and close it:

~~~~~~~~~~~~~{.c}
  // This essentially starts a transaction in the Volume
  unique_ptr<DataStore> resolver(new MemoryDataStore());

  // This will open and reparse the zip file, populating the resolver.
  AFF4ScopedPtr<AFF4Volume> zip = ZipFile::NewZipFile(resolver.get(), "file.zip");

  // This creates a new image with URN "image.dd" inside the zip file's URN.
  AFF4ScopedPtr<AFF4Image> image = AFF4Image::NewAFF4Image(
       resolver.get(), "image.dd", zip->urn);

  // Write something on the image.
  image->sprintf("Hello world!");

  // This will flush all images, close the zip file etc. This method is also
  // automatically called when the resolver is destructed so it is unnecessary
  // here.
  resolver->Flush();
~~~~~~~~~~~~~

 */

struct DataStoreOptions {
    std::shared_ptr<spdlog::logger> logger = aff4::get_logger();
    int threadpool_size = 1;
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

    // All logging directives go through this handle. It can be
    // replaced with a different handler if needed.
    std::shared_ptr<spdlog::logger> logger;

    /// You can add new namespaces here for turtle serialization.
    std::vector<std::pair<std::string, std::string>> namespaces;

    // A global thread pool for general use.
    std::unique_ptr<ThreadPool> pool;

    template<typename T>
    AFF4ScopedPtr<T> CachePut(AFF4Object* object) {
        ObjectCache.Put(object, true);
        return AFF4ScopedPtr<T>(dynamic_cast<T*>(object), this);
    }

    template<typename T>
    AFF4ScopedPtr<T> CacheGet(const URN urn) {
        AFF4Object* object = ObjectCache.Get(urn);
        return AFF4ScopedPtr<T>(dynamic_cast<T*>(object), this);
    }

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
     * Flush all objects cached in the data store.
     *
     *
     * @return Status.
     */
    virtual AFF4Status Flush() = 0;

    /**
     * Prints out the contents of the resolver to STDOUT. Used for debugging.
     *
     */
    void Dump(bool verbose = true);

    /**
       This is the main entry point into the AFF4 library. Callers use this factory
       method to instantiate an AFF4Object of a particular type based on its
       URN. The factory is passed a resolver which contains the AFF4 RDF metadata
       about the subset of the AFF4 universe we are dealing with.

       The object returned is of the type specified in the resolver (or its base
       type). Callers to the factory must declare their expected types in the template
       arg. If the object is not of the required type, the factory will not
       instantiate it.

       Note that all objects instantiated by the factory are owned by the factory at
       all times. Callers just receive a reference to the object. This allows the
       resolver to maintain a cache of objects and reuse them. Typically, therefore,
       callers may not hold the returned objects for long periods of time. Instead,
       callers should record the URN and use it to retrieve the object in future.

       When the resolver is destroyed, the objects cached by it are flushed. Therefore
       callers may use the lifetime of the resolver as a transaction for created AFF4
       objects.

       The following is an example of how to create an AFF4Image instance and write to
       it:

       ~~~~~~~~~~~{.c}
       void test_ZipFileCreate() {
       unique_ptr<DataStore> resolver(new MemoryDataStore());
       AFF4ScopedPtr<AFF4Stream> file = resolver->AFF4FactoryOpen<AFF4Stream>(
         "test.zip");

       // The backing file is given to the zip.
       AFF4ScopedPtr<AFF4Volume> zip = ZipFile::NewZipFile(resolver.get(), file->urn);

       AFF4ScopedPtr<AFF4Stream> segment = zip->CreateMember("Foobar.txt");
       segment->Write("I am a segment!");
       };
       ~~~~~~~~~~~

       * @param resolver: The resolver to use.
       * @param urn: The URN to instantiate.
       *
       * @return A instance of T or NULL if an object of this type is not known at the
       *         specified URN. Note that callers do not own the object and must not
       *         hold persistent references to it.
       */
    template<typename T>
    AFF4ScopedPtr<T> AFF4FactoryOpen(const URN& urn) {
        logger->debug("AFF4FactoryOpen : {}", urn);

        // Check the symbolic aff4:ImageStream cache.
        auto it = SymbolicStreams.find(urn.value);
        if(it != SymbolicStreams.end()){
            // This is a symbolic stream
            std::shared_ptr<AFF4Stream> stream = it->second;
            T *result = dynamic_cast<T*>(stream.get());
            if (result != nullptr) {
                return AFF4ScopedPtr<T>(result, this);
            }
        }

        // It is in the cache, just return it.
        AFF4Object* cached_obj = ObjectCache.Get(urn);
        if (cached_obj) {
            logger->debug("AFF4FactoryOpen (cached): {}", cached_obj->urn);

            cached_obj->Prepare();
            return AFF4ScopedPtr<T>(dynamic_cast<T*>(cached_obj), this);
        }

        std::unique_ptr<AFF4Object> obj;
        const uri_components components = urn.Parse();

        // Check if there is a resolver triple for it.
        std::vector<std::shared_ptr<RDFValue>> types;
        if(Get(urn, AFF4_TYPE, types) == STATUS_OK) {
            // Try to instantiate all the types until one works.
            for(const auto& v : types) {
                std::unique_ptr<AFF4Object> tmp_obj = GetAFF4ClassFactory()->
                    CreateInstance(v->SerializeToString(), this, &urn);
                if(tmp_obj == nullptr) {
                    continue;
                }

                // Have the object load and initialize itself.
                tmp_obj->urn = urn;
                if (tmp_obj->LoadFromURN() != STATUS_OK) {
                    logger->debug("Failed to load {} as {}", urn, *v);
                    continue;
                }

                obj.swap(tmp_obj);
            }
        }

        // Try to instantiate the handler based on the URN scheme alone.
        if (!obj) {
            obj = GetAFF4ClassFactory()->CreateInstance(components.scheme, this, &urn);
            if (obj) {
                obj->urn = urn;
                if (obj->LoadFromURN() != STATUS_OK) {
                    obj.release();
                }
            }
        }

        // Failed to find the object.
        if (!obj) {
            return AFF4ScopedPtr<T>();
        }


        // Cache the object for next time.
        T* result = dynamic_cast<T*>(obj.get());

        if(result == nullptr){
                // bad type cast.
                return AFF4ScopedPtr<T>();
        }

        // Store the object in the cache but place it immediate in the in_use list.
        ObjectCache.Put(obj.release(), true);

        logger->debug("AFF4FactoryOpen (new instance): {}", result->urn);

        result->Prepare();
        return AFF4ScopedPtr<T>(result, this);
    }

    // Closing an object means to flush it and remove it from the cache so it no
    // longer exists in memory.
    template<typename T>
    AFF4Status Close(AFF4ScopedPtr<T>& object) {
        URN tmp_urn = object->urn;
        AFF4Status res = ObjectCache.Remove(object.release());
        logger->debug("Closing object {} {}", tmp_urn, res);

        return res;
    }

  protected:
    /**
     * Returns the AFF4 object to the cache.
     *
     *
     * @param object
     */
    void Return(AFF4Object* object) {
        std::string urn = object->urn.SerializeToString();
        // Don't return symbolics.
        auto it = SymbolicStreams.find(urn);
        if (it != SymbolicStreams.end()) {
            return;
        }

        logger->debug("Returning: {}", urn);
        ObjectCache.Return(object);
    }

    /**
     * An object cache for objects created via the AFF4FactoryOpen()
     * interface. Note that the cache owns all objects at all times.
     *
     */
    AFF4ObjectCache ObjectCache;

    /**
     * Collection of Symbolic AFF4 Streams.
     */
    std::unordered_map<std::string, std::shared_ptr<AFF4Stream>> SymbolicStreams;

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
    AFF4Status Flush() override;
};

} // namespace aff4

#endif  //  SRC_DATA_STORE_H_
