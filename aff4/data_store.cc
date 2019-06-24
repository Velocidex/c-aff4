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
#include "aff4/lexicon.h"
#include "aff4/data_store.h"
#include "aff4/libaff4.h"
#include <raptor2/raptor2.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <mutex>
#include "aff4/aff4_symstream.h"

namespace aff4 {

DataStore::DataStore():
    DataStore(DataStoreOptions()) {}


// Since subjects may have multiple types, we need to ensure that we
// suppress predicates properly.
bool DataStore::ShouldSuppress(const URN& subject,
                               const URN& predicate,
                               const std::string& value) {
    URN type(AFF4_TYPE);

    if (!HasURNWithAttribute(subject, type)) {
        // This is assumed to be an object of type AFF4_ZIP_SEGMENT_TYPE
        return true;
    }

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
      pool(std::unique_ptr<ThreadPool>(new ThreadPool(options.threadpool_size))) {

    // Add these default namespace.
    namespaces.push_back(std::pair<std::string, std::string>("aff4", AFF4_NAMESPACE));
    namespaces.push_back(std::pair<std::string, std::string>("xsd", XSD_NAMESPACE));
    namespaces.push_back(std::pair<std::string, std::string>("rdf", RDF_NAMESPACE));
}

class RaptorWorldPool {
    std::vector<raptor_world*> pool{};
    std::mutex pool_mutex{};

public:
    raptor_world * get() {
        std::lock_guard<std::mutex> lock(pool_mutex);

        raptor_world * world;

        if (pool.empty()) {
            world = raptor_new_world();
            raptor_world_open(world);
        } else {
            world = pool.back();
            pool.pop_back();
        }

        return world;
    }

    void put(raptor_world * world) {
        std::lock_guard<std::mutex> lock(pool_mutex);

        pool.push_back(world);
    }
};

static RaptorWorldPool raptor_world_pool{};

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

        result->world = raptor_world_pool.get();

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
        if (serializer != nullptr) {
            raptor_free_serializer(serializer);
        }

        if (world != nullptr) {
            raptor_world_pool.put(world);
        }
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

        result->world = raptor_world_pool.get();

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

        if (parser != nullptr) {
            raptor_free_parser(parser);
        }

        if (world != nullptr) {
            raptor_world_pool.put(world);
        }
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
            for (const auto& a: attr_it.second) {
                const RDFValue* value = a.get();

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
        // Since the majority of AFF4 objects in practice are zip segments,
        // as an optimization we don't store type attributes for these
        // objects.  Instead, objects without type attriutes are assumed to
        // be zip segments.
        if (attribute == AFF4_TYPE) {
            value.UnSerializeFromString(AFF4_ZIP_SEGMENT_TYPE);
            return STATUS_OK;
        }

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
        // Since the majority of AFF4 objects in practice are zip segments,
        // as an optimization we don't store type attributes for these
        // objects.  Instead, objects without type attriutes are assumed to
        // be zip segments.
        if (attribute == AFF4_TYPE) {
            values.emplace_back(new URN(AFF4_ZIP_SEGMENT_TYPE));
            return STATUS_OK;
        }
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

AFF4Status MemoryDataStore::Clear() {
    store.clear();
    return STATUS_OK;
}

MemoryDataStore::~MemoryDataStore() {}

void DataStore::Dump(bool verbose) {
    StringIO output;

    DumpToTurtle(output, "", verbose);

    std::cout << output.buffer;
}

} // namespace aff4
