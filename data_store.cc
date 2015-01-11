// Implementation of data stores.
#include "data_store.h"
#include "aff4.h"
#include <yaml-cpp/yaml.h>

int MemoryDataStore::DumpToYaml(AFF4Stream &output) {
  YAML::Emitter out;

  out << YAML::BeginMap;
  for(auto it=store.begin(); it != store.end(); it++) {
    out << YAML::Key << it->first;

    AFF4_Attributes attributes = it->second;
    out << YAML::Value << YAML::BeginMap;
    for(auto attr_it=attributes.begin(); attr_it != attributes.end(); attr_it++) {
      out << YAML::Key << attr_it->first;
      DataStoreObject value = attr_it->second;
      out << YAML::Value;;
      switch (value.wire_type) {
        case STRING:
          out << value.string_data;
          break;
        case INTEGER:
          out << value.int_data;
          break;
        default:
          out << "<Unknown>";
          break;
      };
    };
  };

  output.Write(out.c_str());

  return out.size();
};


void MemoryDataStore::Set(URN urn, URN attribute, const RDFValue &value) {
  // Automatically create needed keys.
  store[urn.value][attribute.value] = value.Serialize();
};


int MemoryDataStore::Get(URN urn, URN attribute, RDFValue &value) {
  auto urn_it = store.find(urn.value);

  if (urn_it == store.end())
    return 0;

  auto attribute_itr = urn_it->second.find(attribute.value);
  if (attribute_itr == urn_it->second.end())
    return 0;

  return value.UnSerialize(attribute_itr->second);
};
