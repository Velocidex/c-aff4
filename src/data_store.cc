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
#include <yaml-cpp/yaml.h>
#include <raptor2/raptor2.h>
#include <glog/logging.h>

DataStore::DataStore() {
  // By default suppress ZipFileSegment objects since all their metadata comes
  // directly from the ZIP container. This keeps the turtle files a bit cleaner.
  suppressed_rdftypes.insert(AFF4_ZIP_SEGMENT_TYPE);
  suppressed_rdftypes.insert(AFF4_ZIP_TYPE);
};


DataStore::~DataStore() {
};

AFF4Status MemoryDataStore::DumpToYaml(AFF4Stream &output) {
  YAML::Emitter out;

  out << YAML::BeginMap;
  for(const auto &it: store) {
    out << YAML::Key << it.first;

    out << YAML::Value << YAML::BeginMap;
    for(const auto &attr_it: it.second) {
      out << YAML::Key << attr_it.first;
      out << YAML::Value << attr_it.second->SerializeToString();
    };
    out << YAML::EndMap;
  };
  out << YAML::EndMap;

  output.Write(out.c_str());

  return STATUS_OK;
};


class RaptorSerializer {
 protected:
  raptor_world* world;
  void* output;
  size_t length;
  raptor_serializer* serializer;

  RaptorSerializer() {};

 public:
  static unique_ptr<RaptorSerializer> NewRaptorSerializer(URN base) {
    unique_ptr<RaptorSerializer> result(new RaptorSerializer());
    raptor_uri *uri;

    result->world = raptor_new_world();

    result->serializer = raptor_new_serializer(result->world, "turtle");

    uri = raptor_new_uri(
        result->world, (const unsigned char *)base.SerializeToString().c_str());
    raptor_serializer_start_to_string(
        result->serializer, uri, &result->output, &result->length);
    raptor_free_uri(uri);

    // Add the most common namespaces.
    uri = raptor_new_uri(
        result->world, (const unsigned char *)AFF4_NAMESPACE);
    raptor_serializer_set_namespace(result->serializer, uri,
                                    (const unsigned char *)"aff4");
    raptor_free_uri(uri);

    uri = raptor_new_uri(
        result->world, (const unsigned char *)XSD_NAMESPACE);
    raptor_serializer_set_namespace(result->serializer, uri,
                                    (const unsigned char *)"xsd");
    raptor_free_uri(uri);

    uri = raptor_new_uri(
        result->world,
        (const unsigned char *)RDF_NAMESPACE);
    raptor_serializer_set_namespace(result->serializer, uri,
                                    (const unsigned char *)"rdf");
    raptor_free_uri(uri);

    return result;
  };

  AFF4Status AddStatement(const URN &subject, const URN &predicate,
                          const RDFValue *value) {
    raptor_statement* triple = raptor_new_statement(world);
    triple->subject = raptor_new_term_from_uri_string(
        world, (const unsigned char*)
        subject.SerializeToString().c_str());

    triple->predicate = raptor_new_term_from_uri_string(
        world, (const unsigned char*)
        predicate.SerializeToString().c_str());

    triple->object = value->GetRaptorTerm(world);
    if (!triple->object) {
      return INCOMPATIBLE_TYPES;
    };

    raptor_serializer_serialize_statement(serializer, triple);
    raptor_free_statement(triple);

    return STATUS_OK;
  };

  string Finalize() {
    raptor_serializer_serialize_end(serializer);
    return string((char *)output, length);
  };

  ~RaptorSerializer() {
    raptor_free_serializer(serializer);
    raptor_free_world(world);
  };
};


static unique_ptr<RDFValue> RDFValueFromRaptorTerm(
    DataStore *resolver, raptor_term *term) {
  if (term->type == RAPTOR_TERM_TYPE_URI) {
    char *uri = (char *)raptor_uri_to_string(term->value.uri);
    unique_ptr<RDFValue> result(new URN(uri));
    raptor_free_memory(uri);
    return result;
  };

  if (term->type == RAPTOR_TERM_TYPE_LITERAL) {
    char *uri = (char *)raptor_uri_to_string(term->value.literal.datatype);

    unique_ptr<RDFValue> result = RDFValueRegistry.CreateInstance(uri, resolver);
    raptor_free_memory(uri);

    string value_string((char *)term->value.literal.string,
                        term->value.literal.string_len);

    if(result->UnSerializeFromString(value_string) != STATUS_OK) {
      LOG(ERROR) << "Unable to parse " << value_string.c_str();
      return NULL;
    };

    return result;
  };
  return NULL;
};


static void statement_handler(void *user_data,
                              raptor_statement *statement) {
  DataStore *resolver = (DataStore *)user_data;

  if (statement->subject->type == RAPTOR_TERM_TYPE_URI &&
      statement->predicate->type == RAPTOR_TERM_TYPE_URI) {
    char *subject = (char *)raptor_uri_to_string(statement->subject->value.uri);

    char *predicate = (char *)raptor_uri_to_string(
        statement->predicate->value.uri);

    unique_ptr<RDFValue> object(RDFValueFromRaptorTerm(
        resolver, statement->object));

    resolver->Set(URN(subject), URN(predicate), std::move(object));

    raptor_free_memory(subject);
    raptor_free_memory(predicate);
  };
};


class RaptorParser {
 protected:
  raptor_world *world;
  raptor_parser *parser;
  DataStore *resolver;

  RaptorParser(DataStore *resolver): resolver(resolver) {};

 public:
  static unique_ptr<RaptorParser> NewRaptorParser(DataStore *resolver) {
    unique_ptr<RaptorParser> result(new RaptorParser(resolver));

    result->world = raptor_new_world();

    result->parser = raptor_new_parser(result->world, "turtle");

    raptor_parser_set_statement_handler(
        result->parser, resolver, statement_handler);

    // Dont talk to the internet
    raptor_parser_set_option(result->parser, RAPTOR_OPTION_NO_NET, NULL, 1);

    raptor_uri *uri = raptor_new_uri(
        result->world, (const unsigned char *)".");

    if(raptor_parser_parse_start(result->parser, uri)) {
      LOG(ERROR) << "Unable to initialize the parser.";
      return NULL;
    };

    raptor_free_uri(uri);

    return result;
  };

  AFF4Status Parse(string buffer) {
    if(raptor_parser_parse_chunk(
           parser, (const unsigned char *)buffer.data(),
           buffer.size(), 1)) {
      return PARSING_ERROR;
    };

    return STATUS_OK;
  };

  ~RaptorParser() {
    // Flush the parser.
    raptor_parser_parse_chunk(parser, NULL, 0, 1);

    raptor_free_parser(parser);
    raptor_free_world(world);
  };
};


AFF4Status MemoryDataStore::DumpToTurtle(AFF4Stream &output_stream, URN base,
                                         bool verbose) {
  unique_ptr<RaptorSerializer> serializer(
      RaptorSerializer::NewRaptorSerializer(base));
  if(!serializer) {
    return MEMORY_ERROR;
  };

  for(const auto &it: store) {
    URN subject(it.first);
    URN type;

    // Skip this URN if it is in the suppressed_rdftypes set.
    if (Get(subject, AFF4_TYPE, type) == STATUS_OK) {
      if(!verbose &&
         suppressed_rdftypes.find(type.value) != suppressed_rdftypes.end()) {
        continue;
      };
    };

    for(const auto &attr_it: it.second) {
      URN predicate(attr_it.first);

      // Volatile predicates are suppressed.
      if(!verbose && 0 == predicate.value.compare(
             0, strlen(AFF4_VOLATILE_NAMESPACE), AFF4_VOLATILE_NAMESPACE)) {
        continue;
      };

      serializer->AddStatement(
          subject, predicate, attr_it.second.get());
    };
  };

  output_stream.Write(serializer->Finalize());

  return STATUS_OK;
};


AFF4Status MemoryDataStore::LoadFromTurtle(AFF4Stream &stream) {
  unique_ptr<RaptorParser> parser(
      RaptorParser::NewRaptorParser(this));
  if (!parser) {
    return MEMORY_ERROR;
  };

  while (1) {
    string buffer = stream.Read(1000000);
    if (buffer.size() == 0) {
      break;
    };

    AFF4Status res = parser->Parse(buffer);
    if (res != STATUS_OK) {
      return res;
    };
  };

  return STATUS_OK;
}

AFF4Status MemoryDataStore::LoadFromYaml(AFF4Stream &stream) {
  return NOT_IMPLEMENTED;
};


void MemoryDataStore::Set(const URN &urn, const URN &attribute,
                          RDFValue *value) {
  unique_ptr<RDFValue> unique_value(value);
  // Automatically create needed keys.
  store[urn.SerializeToString()][attribute.SerializeToString()] = (
      std::move(unique_value));
};

void MemoryDataStore::Set(const URN &urn, const URN &attribute,
                          unique_ptr<RDFValue> value) {
  // Automatically create needed keys.
  store[urn.SerializeToString()][attribute.SerializeToString()] = (
      std::move(value));
};

AFF4Status MemoryDataStore::Get(const URN &urn, const URN &attribute,
                                RDFValue &value) {
  auto urn_it = store.find(urn.SerializeToString());

  if (urn_it == store.end())
    return NOT_FOUND;

  auto attribute_itr = urn_it->second.find(attribute.SerializeToString());
  if (attribute_itr == urn_it->second.end())
    return NOT_FOUND;

  // The RDFValue type is incompatible with what the caller provided.
  if (typeid(value) != typeid(*attribute_itr->second)) {
    return INCOMPATIBLE_TYPES;
  };

  return value.UnSerializeFromString(
      attribute_itr->second->SerializeToString());
};


AFF4Status MemoryDataStore::DeleteSubject(const URN &urn) {
  store.erase(urn.SerializeToString());

  return STATUS_OK;
};

AFF4Status MemoryDataStore::Flush() {
  for(auto it=ObjectCache.begin(); it!=ObjectCache.end(); it++) {
    LOG(INFO) << "Flushing " << it->first.c_str();
    it->second->Flush();
  };

  return STATUS_OK;
};

AFF4Status MemoryDataStore::Clear() {
  Flush();
  ObjectCache.clear();

  store.clear();
  return STATUS_OK;
};

MemoryDataStore::~MemoryDataStore() {
  Flush();
};

void DataStore::Dump() {
  StringIO output;

  DumpToTurtle(output, "", true);

  std::cout << output.buffer;
};
