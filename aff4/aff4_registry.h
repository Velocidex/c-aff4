/*
  Defines a library wide registration system for classes.
*/
#ifndef SRC_AFF4_REGISTRY_H_
#define SRC_AFF4_REGISTRY_H_

#include "aff4/config.h"

#include <unordered_map>
#include <memory>
#include <string>
#include <iostream>
#include <functional>


namespace aff4 {


class DataStore;
class URN;

template<class T>
class ClassFactory {
  public:
    std::unique_ptr<T> CreateInstance(const char* name, DataStore* resolver,
                                      const URN* urn = nullptr) const {
        return CreateInstance(std::string(name), resolver, urn);
    }

    std::unique_ptr<T> CreateInstance(std::string name, DataStore* data,
                                      const URN* urn = nullptr) const {
        T* instance = nullptr;

        // find name in the registry and call factory method.
        const auto it = factoryFunctionRegistry.find(name);
        if (it != factoryFunctionRegistry.end()) {
            instance = it->second(data, urn);
        }

        return std::unique_ptr<T>(instance);
    }

    void RegisterFactoryFunction(
        std::string name, std::function<
        T*(DataStore*, const URN*)> classFactoryFunction)
    {
        // register the class factory function
        factoryFunctionRegistry[name] = classFactoryFunction;
    }

  private:
    std::unordered_map<
      std::string,
      std::function<T* (DataStore*, const URN*)>
    > factoryFunctionRegistry;
};

#endif  // SRC_AFF4_REGISTRY_H_


} // namespace aff4
