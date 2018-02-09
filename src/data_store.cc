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

// Implementation of data stores.
#include <typeinfo>
#include "lexicon.h"
#include "data_store.h"
#include "libaff4.h"
#include <raptor2/raptor2.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include "aff4_symstream.h"

namespace aff4 {

void aff4_init();


#ifdef AFF4_HAS_LIBYAML_CPP
# include <yaml-cpp/yaml.h>


AFF4Status MemoryDataStore::DumpToYaml(AFF4Stream& output, bool verbose) {
    // Right now this produces crashes on windows. We dont know why exactly.
    return NOT_IMPLEMENTED;

    YAML::Emitter out;
    YAML::Node node;
    int subject_statements = 0;

    for (const auto& it : store) {
        URN subject(it.first);
        URN type;
        YAML::Node subject_node;
        int emitted_statements = 0;

        // Skip this URN if it is in the suppressed_rdftypes set.
        if (Get(subject, AFF4_TYPE, type) == STATUS_OK) {
            if (!verbose &&
                suppressed_rdftypes.find(type.value) != suppressed_rdftypes.end()) {
                continue;
            }
        }

        for (const auto& attr_it : it.second) {
            URN predicate(attr_it.first);

            // Volatile predicates are suppressed.
            if (!verbose && 0 == predicate.SerializeToString().compare(
                    0, strlen(AFF4_VOLATILE_NAMESPACE), AFF4_VOLATILE_NAMESPACE)) {
                continue;
            }
            subject_node[attr_it.first] = attr_it.second->SerializeToString();
            emitted_statements++;
        }

        if (emitted_statements) {
            node[subject.SerializeToString()] = subject_node;
            subject_statements++;
        }
    }

    // Unfortunately if we try to dump and empty node yaml-cpp will crash.
    if (subject_statements) {
        out << node;
        output.Write(out.c_str());
    }

    return STATUS_OK;
}

AFF4Status MemoryDataStore::LoadFromYaml(AFF4Stream& stream) {
    UNUSED(stream);
    return NOT_IMPLEMENTED;
}

#endif

DataStore::DataStore():
    DataStore(DataStoreOptions()) {}


// Since subjects may have multiple types, we need to ensure that we
// suppress predicates properly.
bool DataStore::ShouldSuppress(const URN& subject,
                               const URN& predicate,
                               const std::string& value) {
    URN type(AFF4_TYPE);
    if (predicate == AFF4_STORED &&
        (HasURNWithAttributeAndValue(
            subject, type, URN(AFF4_ZIP_SEGMENT_TYPE)) ||
         HasURNWithAttributeAndValue(
             subject, type, URN(AFF4_ZIP_TYPE)) ||
         HasURNWithAttributeAndValue(
             subject, type, URN(AFF4_DIRECTORY_TYPE)))) {
        return true;
    }

    if (predicate == AFF4_TYPE &&
        (value == AFF4_ZIP_SEGMENT_TYPE ||
         value == AFF4_ZIP_TYPE ||
         value == AFF4_DIRECTORY_TYPE)) {
        return true;
    }

    return false;
}


DataStore::DataStore(DataStoreOptions options)
    : logger(options.logger),
      pool(std::unique_ptr<ThreadPool>(new ThreadPool(options.threadpool_size))),
      ObjectCache(logger) {

    // Add these default namespace.
    namespaces.push_back(std::pair<std::string, std::string>("aff4", AFF4_NAMESPACE));
    namespaces.push_back(std::pair<std::string, std::string>("xsd", XSD_NAMESPACE));
    namespaces.push_back(std::pair<std::string, std::string>("rdf", RDF_NAMESPACE));

    // Add the default symbolic aff4:ImageStream objects.
    SymbolicStreams[AFF4_IMAGESTREAM_ZERO] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_IMAGESTREAM_ZERO), 0)));

    SymbolicStreams[AFF4_IMAGESTREAM_FF] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_IMAGESTREAM_FF), 0xff)));

    SymbolicStreams[AFF4_IMAGESTREAM_UNKNOWN] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_IMAGESTREAM_UNKNOWN), "UNKNOWN")));

    SymbolicStreams[AFF4_IMAGESTREAM_UNREADABLE] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_IMAGESTREAM_UNREADABLE), "UNREADABLEDATA")));

    // Legacy versions of above.
    SymbolicStreams[AFF4_LEGACY_IMAGESTREAM_ZERO] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_LEGACY_IMAGESTREAM_ZERO), 0)));

    SymbolicStreams[AFF4_LEGACY_IMAGESTREAM_FF] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_LEGACY_IMAGESTREAM_FF), 0xff)));

    SymbolicStreams[AFF4_LEGACY_IMAGESTREAM_UNKNOWN] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_LEGACY_IMAGESTREAM_UNKNOWN), "UNKNOWN")));

    SymbolicStreams[AFF4_LEGACY_IMAGESTREAM_UNREADABLE] = (
        std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(AFF4_LEGACY_IMAGESTREAM_UNREADABLE), "UNREADABLEDATA")));

    // Create streams for all possible symbols.
    for (int i = 0; i < 256; i++) {
        std::string urn = aff4_sprintf(
            "%s%02X", AFF4_IMAGESTREAM_SYMBOLIC_PREFIX, i);

        SymbolicStreams[urn] = std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(urn), i));
    }

    // Legacy Versions of above.
    for (int i = 0; i < 256; i++) {
        std::string urn = aff4_sprintf(
            "%s%02X", AFF4_LEGACY_IMAGESTREAM_SYMBOLIC_PREFIX, i);

        SymbolicStreams[urn] = std::shared_ptr<AFF4Stream>(
            new AFF4SymbolicStream(this, URN(urn), i));
    }
}


class RaptorSerializer {
protected:
    raptor_world* world;
    void* output;
    size_t length;
    raptor_serializer* serializer;

    RaptorSerializer() :
        world(nullptr), output(nullptr), length(0), serializer(nullptr) {
    }

public:
    static std::unique_ptr<RaptorSerializer> NewRaptorSerializer(
        URN base,
        const std::vector<std::pair<std::string, std::string>>& namespaces) {
        std::unique_ptr<RaptorSerializer> result(new RaptorSerializer());
        raptor_uri* uri;

        result->world = raptor_new_world();

        result->serializer = raptor_new_serializer(result->world, "turtle");

        uri = raptor_new_uri(result->world, (const unsigned char*) base.SerializeToString().c_str());
        raptor_serializer_start_to_string(result->serializer, uri, &result->output, &result->length);
        raptor_free_uri(uri);

        // Add the most common namespaces.
        for (auto it : namespaces) {
            uri = raptor_new_uri(result->world, (const unsigned char*) it.second.c_str());
            raptor_serializer_set_namespace(result->serializer, uri, (const unsigned char*) it.first.c_str());
            raptor_free_uri(uri);
        }

        return result;
    }

    AFF4Status AddStatement(const URN& subject, const URN& predicate,
                            const RDFValue* value) {
        raptor_statement* triple = raptor_new_statement(world);
        triple->subject = raptor_new_term_from_uri_string(
            world,
            (const unsigned char*) subject.SerializeToString().c_str());

        triple->predicate = raptor_new_term_from_uri_string(
            world,
            (const unsigned char*) predicate.SerializeToString().c_str());

        triple->object = value->GetRaptorTerm(world);
        if (!triple->object) {
            return INCOMPATIBLE_TYPES;
        }

        raptor_serializer_serialize_statement(serializer, triple);
        raptor_free_statement(triple);

        return STATUS_OK;
    }

    std::string Finalize() {
        raptor_serializer_serialize_end(serializer);
        return std::string(reinterpret_cast<char*>(output), length);
    }

    ~RaptorSerializer() {
        raptor_free_serializer(serializer);
        raptor_free_world(world);
    }
};

static std::unique_ptr<RDFValue> RDFValueFromRaptorTerm(DataStore* resolver, raptor_term* term) {
    if (term->type == RAPTOR_TERM_TYPE_URI) {
        char* uri = reinterpret_cast<char*>(raptor_uri_to_string(term->value.uri));
        std::unique_ptr<RDFValue> result(new URN(uri));
        raptor_free_memory(uri);
        return result;
    }

    if (term->type == RAPTOR_TERM_TYPE_LITERAL) {
        // Does it have a special data type?
        if (term->value.literal.datatype) {
            char* uri = reinterpret_cast<char*>(raptor_uri_to_string(term->value.literal.datatype));

            std::unique_ptr<RDFValue> result =
                RDFValueRegistry.CreateInstance(uri, resolver);
            // If we do not know how to handle this type we skip it.
            if (!result) {
                raptor_free_memory(uri);
                return nullptr;
            }

            raptor_free_memory(uri);

            std::string value_string(
                reinterpret_cast<char*>(term->value.literal.string),
                term->value.literal.string_len);

            if (result->UnSerializeFromString(value_string) != STATUS_OK) {
                return nullptr;
            }

            return result;

            // No special type - this is just a string.
        } else {
            std::string value_string(
                reinterpret_cast<char*>(term->value.literal.string),
                term->value.literal.string_len);

            return std::unique_ptr<RDFValue>(new XSDString(value_string));
        }
    }
    return nullptr;
}

static void statement_handler(void* user_data, raptor_statement* statement) {
    DataStore* resolver = reinterpret_cast<DataStore*>(user_data);

    if (statement->subject->type == RAPTOR_TERM_TYPE_URI &&
        statement->predicate->type == RAPTOR_TERM_TYPE_URI) {
        char* subject = reinterpret_cast<char*>(
            raptor_uri_to_string(statement->subject->value.uri));

        char* predicate = reinterpret_cast<char*>(
            raptor_uri_to_string(statement->predicate->value.uri));

        std::unique_ptr<RDFValue> object(
            RDFValueFromRaptorTerm(resolver, statement->object));

        if (object.get()) {
            resolver->Set(URN(subject), URN(predicate), std::move(object),
                          /* replace = */ false);
        }

        raptor_free_memory(subject);
        raptor_free_memory(predicate);
    }
}

class RaptorParser {
protected:
    raptor_world* world;
    raptor_parser* parser;
    DataStore* resolver;

    explicit RaptorParser(DataStore* resolver) :
        world(nullptr), parser(nullptr), resolver(resolver) {
    }

public:
    static std::unique_ptr<RaptorParser> NewRaptorParser(DataStore* resolver) {
        std::unique_ptr<RaptorParser> result(new RaptorParser(resolver));

        result->world = raptor_new_world();

        result->parser = raptor_new_parser(result->world, "turtle");

        raptor_parser_set_statement_handler(
            result->parser, resolver, statement_handler);

        // Dont talk to the internet
        raptor_parser_set_option(
            result->parser, RAPTOR_OPTION_NO_NET, nullptr, 1);
        raptor_parser_set_option(
            result->parser, RAPTOR_OPTION_ALLOW_RDF_TYPE_RDF_LIST, nullptr, 1);

        raptor_uri* uri = raptor_new_uri(
            result->world, (const unsigned char*) ".");

        if (raptor_parser_parse_start(result->parser, uri)) {
            return nullptr;
        }

        raptor_free_uri(uri);

        return result;
    }

    AFF4Status Parse(std::string buffer) {
        if (raptor_parser_parse_chunk(
                parser, (const unsigned char*) buffer.c_str(),
                buffer.size(), 1)) {
            return PARSING_ERROR;
        }

        return STATUS_OK;
    }

    ~RaptorParser() {
        // Flush the parser.
        raptor_parser_parse_chunk(parser, nullptr, 0, 1);

        raptor_free_parser(parser);
        raptor_free_world(world);
    }
};

AFF4Status MemoryDataStore::DumpToTurtle(AFF4Stream& output_stream, URN base, bool verbose) {
    std::unique_ptr<RaptorSerializer> serializer(
        RaptorSerializer::NewRaptorSerializer(base, namespaces));
    if (!serializer) {
        return MEMORY_ERROR;
    }

    for (const auto& it : store) {
        URN subject = it.first;

        for (const auto& attr_it : it.second) {
            URN predicate = attr_it.first;

            // Volatile predicates are suppressed.
            if (!verbose) {
                if (0 == predicate.value.compare(
                        0,
                        strlen(AFF4_VOLATILE_NAMESPACE),
                        AFF4_VOLATILE_NAMESPACE)) {
                    continue;
                }
            }

            // Load all attributes.
            const std::vector<std::shared_ptr<RDFValue>> attr = attr_it.second;
            for (auto at_it = attr.begin(); at_it != attr.end(); at_it++) {
                const RDFValue* value = (*at_it).get();

                // Skip this URN if it is in the suppressed_rdftypes set.
                if (ShouldSuppress(
                        subject, predicate, value->SerializeToString()))
                    continue;

                serializer->AddStatement(subject, predicate, value);
            }
        }
    }

    output_stream.Write(serializer->Finalize());

    return STATUS_OK;
}

AFF4Status MemoryDataStore::LoadFromTurtle(AFF4Stream& stream) {
    std::unique_ptr<RaptorParser> parser(RaptorParser::NewRaptorParser(this));
    if (!parser) {
        return MEMORY_ERROR;
    }

    while (1) {
        std::string buffer = stream.Read(stream.Size());
        if (buffer.size() == 0) {
            break;
        }

        AFF4Status res = parser->Parse(buffer);
        if (res != STATUS_OK) {
            return res;
        }
    }

    return STATUS_OK;
}

void MemoryDataStore::Set(const URN& urn, const URN& attribute, RDFValue* value,
                          bool replace) {
    if (value == nullptr) abort();
    std::shared_ptr<RDFValue> unique_value(value);
    Set(urn, attribute, unique_value, replace);
}

void MemoryDataStore::Set(const URN& urn, const URN& attribute,
                          std::shared_ptr<RDFValue> value, bool replace) {
    // Automatically create needed keys.
    std::vector<std::shared_ptr<RDFValue>> values = store[urn.SerializeToString()][
        attribute.SerializeToString()];

    if (replace) values.clear();

    values.push_back(std::move(value));

    store[urn.SerializeToString()][attribute.SerializeToString()] = values;
}

AFF4Status MemoryDataStore::Get(const URN& urn, const URN& attribute,
                                RDFValue& value) {
    auto urn_it = store.find(urn.SerializeToString());

    if (urn_it == store.end()) {
        return NOT_FOUND;
    }

    auto attribute_itr = urn_it->second.find(attribute.SerializeToString());
    if (attribute_itr == urn_it->second.end()) {
        return NOT_FOUND;
    }

    std::vector<std::shared_ptr<RDFValue>> values = (attribute_itr->second);
    AFF4Status res = NOT_FOUND;
    for (const auto &fetched_value: values) {
        // Only collect compatible types.
        const RDFValue& fetched_value_ref = *fetched_value;
        if (typeid(value) == typeid(fetched_value_ref)) {
            res = value.UnSerializeFromString(
                fetched_value->SerializeToString());
        }
    }

    return res;
}

AFF4Status MemoryDataStore::Get(const URN& urn,
                                const URN& attribute,
                                std::vector<std::shared_ptr<RDFValue>>& values) {
    auto urn_it = store.find(urn.SerializeToString());

    if (urn_it == store.end()) {
        return NOT_FOUND;
    }

    auto attribute_itr = urn_it->second.find(attribute.SerializeToString());
    if (attribute_itr == urn_it->second.end()) {
        return NOT_FOUND;
    }

    std::vector<std::shared_ptr<RDFValue>> ivalues = (attribute_itr->second);
    if (ivalues.empty()) {
        return NOT_FOUND;
    }
    // Load up our keys.
    for(std::vector<std::shared_ptr<RDFValue>>::iterator it = ivalues.begin(); it != ivalues.end(); it++){
        std::shared_ptr<RDFValue> v = *it;
        values.push_back(v);
    }
    return STATUS_OK;
}

bool MemoryDataStore::HasURN(const URN& urn) {
    auto urn_it = store.find(urn.SerializeToString());

    if (urn_it == store.end()) {
        return false;
    }
    return true;
}

bool MemoryDataStore::HasURNWithAttribute(const URN& urn, const URN& attribute) {
    auto urn_it = store.find(urn.SerializeToString());

    if (urn_it == store.end()) {
        return false;
    }

    auto attribute_itr = urn_it->second.find(attribute.SerializeToString());
    if (attribute_itr == urn_it->second.end()) {
        return false;
    }

    std::vector<std::shared_ptr<RDFValue>> values = (attribute_itr->second);
    if (values.empty()) {
        return false;
    }
    return true;
}

bool MemoryDataStore::HasURNWithAttributeAndValue(
    const URN& urn, const URN& attribute, const RDFValue& value) {
    auto urn_it = store.find(urn.SerializeToString());
    std::string serialized_value = value.SerializeToString();

    if (urn_it == store.end()) {
        return false;
    }

    auto attribute_itr = urn_it->second.find(attribute.SerializeToString());
    if (attribute_itr == urn_it->second.end()) {
        return false;
    }

    std::vector<std::shared_ptr<RDFValue>> values = (attribute_itr->second);
    if (values.empty()) {
        return false;
    }

    // iterator over all values looking for a RDFValue that matches the attribute requested.
    for (const auto& v: values) {
        if (serialized_value == v->SerializeToString()) {
            return true;
        }
    }

    return false;
}

std::unordered_set<URN> MemoryDataStore::Query(
    const URN& attribute, const RDFValue* value) {
    std::unordered_set<URN> results;
    std::string serialized_value;
    std::string serialized_attribute = attribute.SerializeToString();

    if (value) {
        serialized_value = value->SerializeToString();
    }

    for (const auto &it: store) {
        URN subject = it.first;
        AFF4_Attributes attr = it.second;

        for (const auto &it: attr) {
            URN stored_attribute = it.first;
            if (stored_attribute.SerializeToString() != serialized_attribute)
                continue;

            const auto& value_array = it.second;

            for (const auto &stored_value: value_array) {
                if (value == nullptr ||
                    serialized_value == stored_value->SerializeToString()) {
                    results.insert(subject);
                }
            }
        }
    }

    return results;
}

AFF4_Attributes MemoryDataStore::GetAttributes(const URN& urn) {
    AFF4_Attributes attr;
    if(!HasURN(urn)){
        return attr;
    }
    auto urn_it = store.find(urn.SerializeToString());
    attr = urn_it->second;
    return attr;
}

AFF4Status MemoryDataStore::DeleteSubject(const URN& urn) {
    store.erase(urn.SerializeToString());

    return STATUS_OK;
}

std::vector<URN> MemoryDataStore::SelectSubjectsByPrefix(const URN& prefix) {
    std::vector<URN> result;

    for (const auto& it : store) {
        URN subject(it.first);
        if (subject.RelativePath(prefix) != subject.SerializeToString()) {
            result.push_back(subject);
        }
    }

    return result;
}

AFF4Status MemoryDataStore::Flush() {
    ObjectCache.Flush();

    return STATUS_OK;
}

AFF4Status MemoryDataStore::Clear() {
    ObjectCache.Flush();

    store.clear();
    return STATUS_OK;
}

MemoryDataStore::~MemoryDataStore() {
    Flush();
}

void DataStore::Dump(bool verbose) {
    StringIO output;

    DumpToTurtle(output, "", verbose);

    std::cout << output.buffer;

    ObjectCache.Dump();
}

AFF4Status AFF4ObjectCache::Flush() {
    AFF4Status res = STATUS_OK;

    // Stop trimming operations of the cache while we flush it. It is possible
    // that the trimmer will remove an object which we are just flushing.
    trimming_disabled = true;

    // It is an error to flush the object cache while there are still items in
    // use.
    if (in_use.size() > 0) {
#ifndef _WIN32
        // In Windows if the user pressed ctrl-C, destructors sometimes do not get
        // called and this sometimes happen. The output volume is incomplete but at
        // least we make it somewhat readable.
        Dump();
        logger->error("ObjectCache flushed while some objects in use!");
#endif

        for (auto it : in_use) {
            it.second->object->Flush();
        }
    }

    // First flush all objects without deleting them since some flushed objects
    // may still want to use other cached objects. It is also possible that new
    // objects are added during object deletion. Therefore we keep doing it until
    // all objects are clean.
    while (1) {
        bool dirty_objects_found = false;

        for (AFF4ObjectCacheEntry* it = lru_list.next; it != &lru_list; it = it->next) {
            if (!it->flush_failed && it->object->IsDirty()) {
                res = it->object->Flush();

                // If we fail to flush this object we must not count it as dirty.
                if (res != STATUS_OK) {
                    it->flush_failed = true;
                } else {
                    dirty_objects_found = true;
                }
            }
        }

        if (!dirty_objects_found) {
            break;
        }
    }

    // Now delete all entries.
    for (auto it : lru_map) {
        delete it.second;
    }

    // Clear the map.
    lru_map.clear();

    // The cache is empty now.
    trimming_disabled = false;

    return res;
}

void AFF4ObjectCache::Dump() {
    // Now dump the objects in use.
    logger->info("Objects in use:");
    for (auto it : in_use) {
        logger->info("{} - {}", it.first, it.second->use_count);
    }

    logger->info("Objects in cache:");
    for (AFF4ObjectCacheEntry* it = lru_list.next; it != &lru_list; it = it->next) {
        logger->info("{} - {}", it->key, it->use_count);
    }
}

AFF4Status AFF4ObjectCache::Trim_() {
    AFF4Status res = STATUS_OK;

    // Skip trimming for now.
    if (trimming_disabled) {
        return res;
    }

    // First check that the cache is not full.
    while (lru_map.size() > max_items) {
        // The back of the list is the oldest one.
        AFF4ObjectCacheEntry* older_item = lru_list.prev;

        // We now want to flush the object before destroying it. But before we do we
        // need to gain ownership of the object to ensure that it will not be
        // trimmed again due to a cascading operation. We therefore place it into
        // the in_use map and remove from the lru_map.
        lru_map.erase(older_item->key);
        older_item->unlink();

        older_item->use_count += 1;
        in_use[older_item->key] = older_item;

        // Now flush the object.
        res = older_item->object->Flush();

        // Remove from the in_use map and delete it.
        in_use.erase(older_item->key);
        older_item->use_count--;

        delete older_item;
    }

    return res;
}

AFF4Status AFF4ObjectCache::Put(AFF4Object* object, bool in_use_state) {
    URN urn = object->urn;
    std::string key = urn.SerializeToString();

    if (in_use.find(key) != in_use.end()) {
        logger->critical("Object {} put in cache while already in use.", key);
    }

    if (lru_map.find(key) != lru_map.end()) {
        logger->critical("Object {} put in cache while already in cache.", key);
    }

    AFF4ObjectCacheEntry* entry = new AFF4ObjectCacheEntry(key, object);
    // Do we need to immediately put it in the in-use list? This should only be
    // used for newly created objects which must be registered with the cache and
    // immediately returned to be used outside the cache.
    if (in_use_state) {
        entry->use_count = 1;
        in_use[key] = entry;
        return STATUS_OK;
    }

    // Newest items go on the front.
    lru_list.append(entry);
    lru_map[key] = entry;

    return Trim_();
}

AFF4Object* AFF4ObjectCache::Get(const URN urn) {
    std::string key = urn.SerializeToString();

    // Is it already in use? Ideally this should not happen because it means that
    // the state of the object may suddenly change for its previous user. We allow
    // it because it may be convenient but it is not generally recommended.
    auto in_use_itr = in_use.find(key);
    if (in_use_itr != in_use.end()) {
        in_use_itr->second->use_count++;

        return in_use_itr->second->object;
    }

    auto iter = lru_map.find(key);
    if (iter == lru_map.end()) {
        // Key not found.
        return nullptr;
    }

    // Hold onto the entry.
    AFF4ObjectCacheEntry* entry = iter->second;
    entry->use_count = 1;

    // Remove it from the LRU list.
    entry->unlink();
    lru_map.erase(iter);

    // Add it to the in use map.
    in_use[key] = entry;

    return entry->object;
}

void AFF4ObjectCache::Return(AFF4Object* object) {
    std::string key = object->urn.SerializeToString();
    auto it = in_use.find(key);

    // This should never happen - Only objects obtained from Get() should be
    // Returnable.
    if (it == in_use.end()) {
        logger->critical("Object {} returned to cache, but it is not in use!", key);
    }

    AFF4ObjectCacheEntry* entry = it->second;

    if (entry->use_count <= 0) {
        logger->critical("Returned object {} is not used.", key);
    }

    // Decrease the object's reference count.
    entry->use_count--;

    // Put it back in the LRU if it is no longer used.
    if (entry->use_count == 0) {
        // Add it to the front of the lru list.
        lru_list.append(entry);
        lru_map[key] = entry;

        // Remove it from the in use map.
        in_use.erase(it);

        // The cache has grown - check we dont exceed the limit.
        Trim_();
    }
}

AFF4Status AFF4ObjectCache::Remove(AFF4Object* object) {
    std::string key = object->urn.SerializeToString();
    AFF4Status res;

    auto it = lru_map.find(key);
    if (it != lru_map.end()) {
        AFF4ObjectCacheEntry* entry = it->second;
        res = entry->object->Flush();

        delete entry;

        lru_map.erase(it);

        return res;
    }

    // Is the item in use?
    auto in_use_it = in_use.find(key);
    if (in_use_it != in_use.end()) {
        AFF4ObjectCacheEntry* entry = in_use_it->second;
        res = entry->object->Flush();

        delete entry;

        // Maybe its in use - remove it from there.
        in_use.erase(in_use_it);

        return res;
    }

    // This is a fatal error.
    logger->critical(
        "Object {} removed from cache, but was never there.", key);

    return FATAL_ERROR;
}

} // namespace aff4
