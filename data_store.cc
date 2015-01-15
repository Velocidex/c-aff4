// Implementation of data stores.
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

  RaptorSerializer(): world(raptor_new_world()) {
    raptor_uri* uri = raptor_new_uri(
        world, (const unsigned char *)"http://example.org/base");

    serializer = raptor_new_serializer(world, "turtle");
    raptor_serializer_start_to_string(
        serializer, uri, &output, &length);

    raptor_free_uri(uri);
  };

 public:
  static unique_ptr<RaptorSerializer> NewRaptorSerializer() {
    unique_ptr<RaptorSerializer> result(new RaptorSerializer());

    return result;
  };

  AFF4Status AddStatement(const URN &subject, const URN &predicate,
                          const RDFValue &value) {
    raptor_statement* triple = raptor_new_statement(world);
    triple->subject = raptor_new_term_from_uri_string(
        world, (const unsigned char*)
        subject.SerializeToString().c_str());

    triple->predicate = raptor_new_term_from_uri_string(
        world, (const unsigned char*)
        predicate.SerializeToString().c_str());

    string value_string = value.SerializeToString();
    triple->object = raptor_new_term_from_counted_literal(
        world,
        (const unsigned char *)value_string.c_str(),
        value_string.size(),
        NULL,
        NULL, 0);

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
          subject, predicate, *attr_it.second);
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

  RDFValue *found_value = attribute_itr->second.get();

  // Incompatible types stored.
  if (value.name != found_value->name) {
    return INCOMPATIBLE_TYPES;
  };

  return value.UnSerializeFromString(
      attribute_itr->second->SerializeToString());
};


// A global resolver.
MemoryDataStore oracle;
