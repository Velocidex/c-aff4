/*
  Defines a library wide registration system for classes.
*/
#ifndef AFF4_REGISTRY_H
#define AFF4_REGISTRY_H

#include <unordered_map>
#include <memory>
#include <string>

using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::function;


template<class T>
class ClassFactory {
 public:
  unordered_map<string, function<T* (void)> > factoryFunctionRegistry;

  unique_ptr<T> CreateInstance(char *name) {
    return CreateInstance(string(name));
  };

  unique_ptr<T> CreateInstance(string name) {
    T* instance = nullptr;

    // find name in the registry and call factory method.
    auto it = factoryFunctionRegistry.find(name);
    if(it != factoryFunctionRegistry.end())
      instance = it->second();

    return unique_ptr<T>(instance);
  };

  void RegisterFactoryFunction(
      string name, function<T*(void)> classFactoryFunction) {
    // register the class factory function
    factoryFunctionRegistry[name] = classFactoryFunction;
  };

};


#endif // AFF4_REGISTRY_H
