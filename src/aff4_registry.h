/*
  Defines a library wide registration system for classes.
*/
#ifndef AFF4_REGISTRY_H
#define AFF4_REGISTRY_H
#include <glog/logging.h>

#include <unordered_map>
#include <memory>
#include <string>
#include <iostream>

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::function;

class DataStore;


template<class T>
class ClassFactory {
 public:
  unordered_map<string, function<T* (DataStore *)> > factoryFunctionRegistry;

  unique_ptr<T> CreateInstance(char *name, DataStore *resolver) {
    return CreateInstance(string(name), resolver);
  };

  unique_ptr<T> CreateInstance(string name, DataStore *data) {
    T* instance = nullptr;

    // find name in the registry and call factory method.
    auto it = factoryFunctionRegistry.find(name);
    if(it != factoryFunctionRegistry.end()) {
      instance = it->second(data);
    } else {
      LOG(ERROR) << "No implementation found for type " << name;
    };

    return unique_ptr<T>(instance);
  };

  void RegisterFactoryFunction(
      string name, function<T*(DataStore *)> classFactoryFunction) {
    // register the class factory function
    factoryFunctionRegistry[name] = classFactoryFunction;
  };

};


#endif // AFF4_REGISTRY_H
