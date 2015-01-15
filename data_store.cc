// Implementation of data stores.
#include "lexicon.h"
#include "data_store.h"
#include "aff4.h"
#include <yaml-cpp/yaml.h>
#include <raptor2/raptor2.h>


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
  static unique_ptr<RaptorSerializer> NewRaptorSerializer() {
    unique_ptr<RaptorSerializer> result(new RaptorSerializer());
    raptor_uri *uri;

    result->world = raptor_new_world();

    result->serializer = raptor_new_serializer(result->world, "turtle");
    raptor_serializer_start_to_string(
        result->serializer, NULL, &result->output, &result->length);

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


AFF4Status MemoryDataStore::DumpToTurtle(AFF4Stream &output_stream) {
  unique_ptr<RaptorSerializer> serializer(
      RaptorSerializer::NewRaptorSerializer());
  if(!serializer) {
    return MEMORY_ERROR;
  };

  for(const auto &it: store) {
    URN subject(it.first);

    for(const auto &attr_it: it.second) {
      URN predicate(attr_it.first);
      serializer->AddStatement(
          subject, predicate, attr_it.second.get());
    };
  };

  output_stream.Write(serializer->Finalize());

  return STATUS_OK;
};


void MemoryDataStore::Set(const URN &urn, const URN &attribute,
                          RDFValue *value) {
  unique_ptr<RDFValue> unique_value(value);
  // Automatically create needed keys.
  store[urn.SerializeToString()][attribute.SerializeToString()] = (
      std::move(unique_value));
};


AFF4Status MemoryDataStore::Get(const URN &urn, const URN &attribute,
                                RDFValue &value) {
  auto urn_it = store.find(urn.SerializeToString());

  if (urn_it == store.end())
    return NOT_FOUND;

  auto attribute_itr = urn_it->second.find(attribute.SerializeToString());
  if (attribute_itr == urn_it->second.end())
    return NOT_FOUND;

  return value.UnSerializeFromString(
      attribute_itr->second->SerializeToString());
};


// A global resolver.
MemoryDataStore oracle;
