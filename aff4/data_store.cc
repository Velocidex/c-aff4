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
#include "aff4/attributes.h"
#include <raptor2/raptor2.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <mutex>
#include "aff4/aff4_symstream.h"

#include <absl/memory/memory.h>

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
      pool(absl::make_unique<ThreadPool>(options.threadpool_size)) {

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
        std::unique_ptr<RaptorSerializer> result {new RaptorSerializer()}; // "new" needed here due to protected constructor
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
                            const AttributeValue& value) {
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

static AttributeValue RDFValueFromRaptorTerm(raptor_term* term) {
    if (term->type == RAPTOR_TERM_TYPE_URI) {
        char* uri = reinterpret_cast<char*>(raptor_uri_to_string(term->value.uri));
        AttributeValue result = URN(uri);
        raptor_free_memory(uri);
        return result;
    }

    const std::string value_string(reinterpret_cast<char*>(term->value.literal.string),
                term->value.literal.string_len);

    if (term->type == RAPTOR_TERM_TYPE_LITERAL) {
        // Does it have a special data type?
        if (term->value.literal.datatype) {
            char* uri = reinterpret_cast<char*>(raptor_uri_to_string(term->value.literal.datatype));

            AttributeValue result = AttributeRegistry.CreateInstance(uri);
            raptor_free_memory(uri);

            // If we do not know how to handle this type we skip it.
            if (!result) {
                return {};
            }

            if (result->UnSerializeFromString(value_string) != STATUS_OK) {
                return {};
            }

            return result;
        }

        // No special type - this is just a string.
        return XSDString(value_string);
    }

    return {};
}

static void statement_handler(void* user_data, raptor_statement* statement) {
    DataStore* resolver = reinterpret_cast<DataStore*>(user_data);

    if (statement->subject->type == RAPTOR_TERM_TYPE_URI &&
        statement->predicate->type == RAPTOR_TERM_TYPE_URI) {
        char* subject = reinterpret_cast<char*>(
            raptor_uri_to_string(statement->subject->value.uri));

        char* predicate = reinterpret_cast<char*>(
            raptor_uri_to_string(statement->predicate->value.uri));

        AttributeValue object = RDFValueFromRaptorTerm(statement->object);
        if (object) {
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
        std::unique_ptr<RaptorParser> result {new RaptorParser(resolver)}; // "new" needed here due to protected constructor

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

    for (const auto it : store) {
        const URN & subject = it.first;
        const AttributeSet & attributes = it.second;

        for (const auto attr_it : attributes) {
            const URN & predicate = attr_it.first;
            const std::vector<AttributeValue> & values = attr_it.second;

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
            for (const auto& value: values) {
                // Skip this URN if it is in the suppressed_rdftypes set.
                if (ShouldSuppress(subject, predicate, 
                    value->SerializeToString())) {
                    continue;
                }

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

void MemoryDataStore::Set(const URN& urn, const URN& attribute, AttributeValue && value,
                          bool replace) {
    std::vector<AttributeValue> & values = store[urn][attribute];

    if (replace) {
        values.clear();
    }

    values.emplace_back(std::forward<AttributeValue>(value));
}

AFF4Status MemoryDataStore::Get(const URN& urn, const URN& attribute,
                                AttributeValue& value) const {
    const auto urn_it = store.find(urn);
    if (urn_it == store.end()) {
        return NOT_FOUND;
    }

    const auto & attributes = urn_it->second;

    const auto attr_it = attributes.find(attribute);
    if (attr_it == attributes.end()) {
        // Since the majority of AFF4 objects in practice are zip segments,
        // as an optimization we don't store type attributes for these
        // objects.  Instead, objects without type attriutes are assumed to
        // be zip segments.
        if (attribute == AFF4_TYPE) {
            value = URN(AFF4_ZIP_SEGMENT_TYPE);
            return STATUS_OK;
        }

        return NOT_FOUND;
    }

    const auto & values = attr_it->second;
    value = values.front();

    return STATUS_OK;
}

AFF4Status MemoryDataStore::Get(const URN& urn, const URN& attribute,
                std::vector<AttributeValue>& values) const {
    const auto urn_it = store.find(urn);
    if (urn_it == store.end()) {
        return NOT_FOUND;
    }

    const auto & attributes = urn_it->second;

    const auto attr_it = attributes.find(attribute);

    if (attr_it == attributes.end()) {
        // Since the majority of AFF4 objects in practice are zip segments,
        // as an optimization we don't store type attributes for these
        // objects.  Instead, objects without type attriutes are assumed to
        // be zip segments.
        if (attribute == AFF4_TYPE) {
            values.emplace_back(URN(AFF4_ZIP_SEGMENT_TYPE));
            return STATUS_OK;
        }
        return NOT_FOUND;
    }

    values = attr_it->second;

    return STATUS_OK;
}

bool MemoryDataStore::HasURN(const URN& urn) const {
    return (store.find(urn) != store.end());
}

bool MemoryDataStore::HasURNWithAttribute(const URN& urn, const URN& attribute) const {
    const auto urn_it = store.find(urn);
    if (urn_it == store.end()) {
        return false;
    }

    const AttributeSet & attributes = urn_it->second;

    return (attributes.find(attribute) != attributes.end());
}

bool MemoryDataStore::HasURNWithAttributeAndValue(const URN& urn, const URN& attribute, 
                                                  const AttributeValue& value) const {
    const auto urn_it = store.find(urn);
    if (urn_it == store.end()) {
        return false;
    }

    const AttributeSet & attributes = urn_it->second;

    const auto attr_it = attributes.find(attribute);
    if (attr_it == attributes.end()) {
        return false;
    }

    const std::vector<AttributeValue> & values = attr_it->second;

    return (std::find(values.begin(), values.end(), value) != values.end());
}

std::unordered_set<URN> MemoryDataStore::Query(const URN& attribute, 
                                               const AttributeValue & value) const {
    std::unordered_set<URN> results{};
    
    for (const auto it: store) {
        const URN & subject = it.first;

        const AttributeSet & attributes = it.second;

        const auto attr_it = attributes.find(attribute);
        if (attr_it == attributes.end()) {
            // No candidate attribute found.  Go to next subject
            continue;
        }

        if (!value.IsValid()) {
            // We're not looking for a specific value, so add the subject
            // to the result set
            results.insert(subject);
            continue;
        }

        const std::vector<AttributeValue> & values = attr_it->second;

        const auto val_it = std::find(values.begin(), values.end(), value);
        if (val_it != values.end()) {
            // We've got a matching pair
            results.insert(subject);
        }
    }

    return results;
}

AttributeSet MemoryDataStore::GetAttributes(const URN& urn) const {
    const auto urn_it = store.find(urn);
    if (urn_it == store.end()) {
        return {};
    }

    return urn_it->second;
}

AFF4Status MemoryDataStore::DeleteSubject(const URN& urn) {
    store.erase(urn);

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
