/*
  Defines a library wide registration system for classes.
*/
#ifndef SRC_AFF4_REGISTRY_H_
#define SRC_AFF4_REGISTRY_H_

#include "config.h"

#include <glog/logging.h>

#include <unordered_map>
#include <memory>
#include <string>
#include <iostream>

//using std::string;
//using std::unique_ptr;
//using std::unordered_map;
//using std::function;

class DataStore;
class URN;

template<class T>
class ClassFactory {
  public:
    std::unordered_map<
    std::string, std::function<T* (DataStore*, const URN*)> > factoryFunctionRegistry;

    std::unique_ptr<T> CreateInstance(char* name, DataStore* resolver,
                                      const URN* urn = nullptr) {
        return CreateInstance(std::string(name), resolver, urn);
    }

    std::unique_ptr<T> CreateInstance(std::string name, DataStore* data,
                                      const URN* urn = nullptr) {
        T* instance = nullptr;

        // find name in the registry and call factory method.
        auto it = factoryFunctionRegistry.find(name);
        if (it != factoryFunctionRegistry.end()) {
            instance = it->second(data, urn);
        }

        return std::unique_ptr<T>(instance);
    }

    void RegisterFactoryFunction(
        std::string name, std::function<
        T*(DataStore*, const URN*)> classFactoryFunction) {
        // register the class factory function
        factoryFunctionRegistry[name] = classFactoryFunction;
    }
};

#endif  // SRC_AFF4_REGISTRY_H_
