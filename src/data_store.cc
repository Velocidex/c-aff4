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
#include <glog/logging.h>
#include <iostream>

#ifdef AFF4_HAS_LIBYAML_CPP
# include <yaml-cpp/yaml.h>

AFF4Status MemoryDataStore::DumpToYaml(AFF4Stream &output, bool verbose) {
  // Right now this produces crashes on windows. We dont know why exactly.
  return NOT_IMPLEMENTED;

  YAML::Emitter out;
  YAML::Node node;
  int subject_statements = 0;

  for (const auto &it : store) {
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

    for (const auto &attr_it : it.second) {
      URN predicate(attr_it.first);

      // Volatile predicates are suppressed.
      if (!verbose && 0 == predicate.SerializeToString().compare(
             0, strlen(AFF4_VOLATILE_NAMESPACE), AFF4_VOLATILE_NAMESPACE)) {
        continue;
      }

      LOG(ERROR) << attr_it.first << " : " <<
          attr_it.second->SerializeToString();
      subject_node[attr_it.first] = attr_it.second->SerializeToString();
      emitted_statements++;
    }

    if (emitted_statements) {
      LOG(ERROR) << "Node : " << subject.SerializeToString() <<
          subject_node.size();
      node[subject.SerializeToString()] = subject_node;
      subject_statements++;
    }
  }

  // Unfortunately if we try to dump and empty node yaml-cpp will crash.
  if (subject_statements) {
    out << node;
    output.Write(out.c_str());

    LOG(ERROR) << "Yaml output: " << out.c_str();
  }

  return STATUS_OK;
}

AFF4Status MemoryDataStore::LoadFromYaml(AFF4Stream &stream) {
  return NOT_IMPLEMENTED;
}

#endif


DataStore::DataStore() {
  // By default suppress ZipFileSegment objects since all their metadata comes
  // directly from the ZIP container. This keeps the turtle files a bit cleaner.
  suppressed_rdftypes[AFF4_ZIP_SEGMENT_TYPE].insert(AFF4_TYPE);
  suppressed_rdftypes[AFF4_ZIP_SEGMENT_TYPE].insert(AFF4_STORED);

  // The following are obvious due to the type of the container.
  suppressed_rdftypes[AFF4_ZIP_TYPE].insert(AFF4_TYPE);
  suppressed_rdftypes[AFF4_ZIP_TYPE].insert(AFF4_STORED);
  suppressed_rdftypes[AFF4_DIRECTORY_TYPE].insert(AFF4_TYPE);
  suppressed_rdftypes[AFF4_DIRECTORY_TYPE].insert(AFF4_STORED);

  // Add these default namespace.
  namespaces.push_back(std::pair<string, string>("aff4", AFF4_NAMESPACE));
  namespaces.push_back(std::pair<string, string>("xsd", XSD_NAMESPACE));
  namespaces.push_back(std::pair<string, string>("rdf", RDF_NAMESPACE));
}


DataStore::~DataStore() {}


class RaptorSerializer {
 protected:
  raptor_world* world;
  void* output;
  size_t length;
  raptor_serializer* serializer;

  RaptorSerializer() {}

 public:
  static unique_ptr<RaptorSerializer> NewRaptorSerializer(
      URN base, const vector<std::pair<string, string>> &namespaces) {
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
    for (auto it : namespaces) {
      uri = raptor_new_uri(
          result->world, (const unsigned char *)it.second.c_str());
      raptor_serializer_set_namespace(result->serializer, uri,
                                      (const unsigned char *)it.first.c_str());
      raptor_free_uri(uri);
    }

    return result;
  }

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
    }

    raptor_serializer_serialize_statement(serializer, triple);
    raptor_free_statement(triple);

    return STATUS_OK;
  }

  string Finalize() {
    raptor_serializer_serialize_end(serializer);
    return string(reinterpret_cast<char *>(output), length);
  }

  ~RaptorSerializer() {
    raptor_free_serializer(serializer);
    raptor_free_world(world);
  }
};


static unique_ptr<RDFValue> RDFValueFromRaptorTerm(
    DataStore *resolver, raptor_term *term) {
  if (term->type == RAPTOR_TERM_TYPE_URI) {
    char *uri = reinterpret_cast<char *>(raptor_uri_to_string(term->value.uri));
    unique_ptr<RDFValue> result(new URN(uri));
    raptor_free_memory(uri);
    return result;
  }

  if (term->type == RAPTOR_TERM_TYPE_LITERAL) {
    // Does it have a special data type?
    if (term->value.literal.datatype) {
      char *uri = reinterpret_cast<char *>(
          raptor_uri_to_string(term->value.literal.datatype));

      unique_ptr<RDFValue> result = RDFValueRegistry.CreateInstance(
          uri, resolver);
      // If we do not know how to handle this type we skip it.
      if (!result) {
        LOG(INFO) << "Unable to handle RDF type " << uri;
        raptor_free_memory(uri);
        return NULL;
      }

      raptor_free_memory(uri);

      string value_string(reinterpret_cast<char *>(term->value.literal.string),
                          term->value.literal.string_len);

      if (result->UnSerializeFromString(value_string) != STATUS_OK) {
        LOG(ERROR) << "Unable to parse " << value_string.c_str();
        return NULL;
      }

      return result;

      // No special type - this is just a string.
    } else {
      string value_string(reinterpret_cast<char *>(term->value.literal.string),
                          term->value.literal.string_len);

      return unique_ptr<RDFValue>(new XSDString(value_string));
    }
  }
  return NULL;
}


static void statement_handler(void *user_data,
                              raptor_statement *statement) {
  DataStore *resolver = reinterpret_cast<DataStore *>(user_data);

  if (statement->subject->type == RAPTOR_TERM_TYPE_URI &&
      statement->predicate->type == RAPTOR_TERM_TYPE_URI) {
    char *subject = reinterpret_cast<char *>(
        raptor_uri_to_string(statement->subject->value.uri));

    char *predicate = reinterpret_cast<char *>(
        raptor_uri_to_string(statement->predicate->value.uri));

    unique_ptr<RDFValue> object(RDFValueFromRaptorTerm(
        resolver, statement->object));

    if (object.get()) {
      resolver->Set(URN(subject), URN(predicate), std::move(object));
    }

    raptor_free_memory(subject);
    raptor_free_memory(predicate);
  }
}

class RaptorParser {
 protected:
  raptor_world *world;
  raptor_parser *parser;
  DataStore *resolver;

  explicit RaptorParser(DataStore *resolver): resolver(resolver) {}

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

    if (raptor_parser_parse_start(result->parser, uri)) {
      LOG(ERROR) << "Unable to initialize the parser.";
      return NULL;
    }

    raptor_free_uri(uri);

    return result;
  }

  AFF4Status Parse(string buffer) {
    if (raptor_parser_parse_chunk(
           parser, (const unsigned char *)buffer.data(),
           buffer.size(), 1)) {
      return PARSING_ERROR;
    }

    return STATUS_OK;
  }

  ~RaptorParser() {
    // Flush the parser.
    raptor_parser_parse_chunk(parser, NULL, 0, 1);

    raptor_free_parser(parser);
    raptor_free_world(world);
  }
};


AFF4Status MemoryDataStore::DumpToTurtle(AFF4Stream &output_stream, URN base,
                                         bool verbose) {
  unique_ptr<RaptorSerializer> serializer(
      RaptorSerializer::NewRaptorSerializer(base, namespaces));
  if (!serializer) {
    return MEMORY_ERROR;
  }

  for (const auto &it : store) {
    URN subject(it.first);
    URN type;

    if (Get(subject, AFF4_TYPE, type) != STATUS_OK) {
      continue;
    }

    for (const auto &attr_it : it.second) {
      URN predicate(attr_it.first);

      // Volatile predicates are suppressed.
      if (!verbose) {
	if (0 == predicate.value.compare(
	    0, strlen(AFF4_VOLATILE_NAMESPACE), AFF4_VOLATILE_NAMESPACE)) {
        continue;
	}

	// Skip this URN if it is in the suppressed_rdftypes set.
	auto suppressed_predicate_it = suppressed_rdftypes.find(type.value);
	if (suppressed_predicate_it != suppressed_rdftypes.end()) {
	  if (suppressed_predicate_it->second.find(predicate.value) !=
	      suppressed_predicate_it->second.end())
	    continue;
	}	
      }

      serializer->AddStatement(
          subject, predicate, attr_it.second.get());
    }
  }

  output_stream.Write(serializer->Finalize());

  return STATUS_OK;
}


AFF4Status MemoryDataStore::LoadFromTurtle(AFF4Stream &stream) {
  unique_ptr<RaptorParser> parser(
      RaptorParser::NewRaptorParser(this));
  if (!parser) {
    return MEMORY_ERROR;
  }

  while (1) {
    string buffer = stream.Read(1000000);
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

void MemoryDataStore::Set(const URN &urn, const URN &attribute,
                          RDFValue *value) {
  CHECK(value != NULL) << "RDF value is NULL";

  unique_ptr<RDFValue> unique_value(value);
  // Automatically create needed keys.
  store[urn.SerializeToString()][attribute.SerializeToString()] = (
      std::move(unique_value));
}

void MemoryDataStore::Set(const URN &urn, const URN &attribute,
                          unique_ptr<RDFValue> value) {
  // Automatically create needed keys.
  store[urn.SerializeToString()][attribute.SerializeToString()] = (
      std::move(value));
}

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
  }

  return value.UnSerializeFromString(
      attribute_itr->second->SerializeToString());
}


AFF4Status MemoryDataStore::DeleteSubject(const URN &urn) {
  store.erase(urn.SerializeToString());

  return STATUS_OK;
}

vector<URN> MemoryDataStore::SelectSubjectsByPrefix(const URN &prefix) {
  vector<URN> result;

  for (const auto &it : store) {
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
    LOG(ERROR) << "ObjectCache flushed while some objects in use!";
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

    for (AFF4ObjectCacheEntry *it = lru_list.next; it != &lru_list;
         it = it->next) {
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

    if (!dirty_objects_found)
      break;
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
  std::cout << "Objects in use:\n";
  for (auto it : in_use) {
    std::cout << it.first << " - " << it.second->use_count << "\n";
  }

  std::cout << "Objects in cache:\n";
  for (AFF4ObjectCacheEntry *it = lru_list.next; it != &lru_list;
       it = it->next) {
    std::cout << it->key << " - " << it->use_count << "\n";
  }
}


AFF4Status AFF4ObjectCache::Trim_() {
  AFF4Status res = STATUS_OK;

  // Skip trimming for now.
  if (trimming_disabled)
    return res;

  // First check that the cache is not full.
  while (lru_map.size() > max_items) {
    // The back of the list is the oldest one.
    AFF4ObjectCacheEntry *older_item = lru_list.prev;

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


AFF4Status AFF4ObjectCache::Put(AFF4Object *object, bool in_use_state) {
  URN urn = object->urn;
  string key = urn.SerializeToString();

  CHECK(in_use.find(key) == in_use.end()) << "Object " <<
      key << " Put in cache while already in use.";

  CHECK(lru_map.find(key) == lru_map.end()) << "Object " <<
      key << " Put in cache while already in cache.";

  AFF4ObjectCacheEntry *entry = new AFF4ObjectCacheEntry(key, object);
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


AFF4Object *AFF4ObjectCache::Get(const URN urn) {
  string key = urn.SerializeToString();

  // Is it already in use? Ideally this should not happen because it means that
  // the state of the object may suddenly change for its previous user. We allow
  // it because it may be convenient but it is not generally recommended.
  auto in_use_itr = in_use.find(key);
  if (in_use_itr != in_use.end()) {
    LOG(INFO) << "URN " << key << " is already in use.";

    in_use_itr->second->use_count++;

    return in_use_itr->second->object;
  }

  auto iter = lru_map.find(key);
  if (iter == lru_map.end()) {
    // Key not found.
    return NULL;
  }

  // Hold onto the entry.
  AFF4ObjectCacheEntry *entry = iter->second;
  entry->use_count = 1;

  // Remove it from the LRU list.
  entry->unlink();
  lru_map.erase(iter);

  // Add it to the in use map.
  in_use[key] = entry;

  return entry->object;
}

void AFF4ObjectCache::Return(AFF4Object *object) {
  string key = object->urn.SerializeToString();
  auto it = in_use.find(key);

  // This should never happen - Only objects obtained from Get() should be
  // Returnable.
  CHECK(it != in_use.end()) <<
      "Object " << key << " Returned to cache, but it is not in use!";

  AFF4ObjectCacheEntry *entry = it->second;

  CHECK_GT(entry->use_count, 0) <<
      "Returned object is not used.";

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

AFF4Status AFF4ObjectCache::Remove(AFF4Object *object) {
  string key = object->urn.SerializeToString();
  AFF4Status res;

  auto it = lru_map.find(key);
  if (it != lru_map.end()) {
    AFF4ObjectCacheEntry *entry = it->second;
    res = entry->object->Flush();

    delete entry;

    lru_map.erase(it);

    return res;
  }

  // Is the item in use?
  auto in_use_it = in_use.find(key);
  if (in_use_it != in_use.end()) {
    AFF4ObjectCacheEntry *entry = in_use_it->second;
    res = entry->object->Flush();

    delete entry;

    // Maybe its in use - remove it from there.
    in_use.erase(in_use_it);

    return res;
  }

  // This is a fatal error.
  LOG(FATAL) << "Object " << key <<
      " removed from cache, but was never there.";

  return FATAL_ERROR;
}
