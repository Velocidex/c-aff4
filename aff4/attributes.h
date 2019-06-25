/*
Copyright 2019 BlackBag Technologies, Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License.  You may obtain a copy of the
License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.  See the License for the
specific language governing permissions and limitations under the License.
*/

#pragma once

#include "aff4/rdf.h"

#include <absl/types/variant.h>

#include <unordered_map>

namespace aff4 { 

class AttributeValue {
    // Stub
    template<typename, typename>
    struct is_variant_member;

    /**
     * Determines if type T is a valid member of a variant
     */
    template<typename T, typename... ALL_T>
    struct is_variant_member<T, absl::variant<ALL_T...>> 
        : public absl::disjunction<std::is_same<absl::decay_t<T>, ALL_T>...> {};

    // Stub
    template<typename, typename>
    struct validate_base;

    /**
     * Determines if all members of a variant have a base of type T
     */
    template<typename T, typename... ALL_T>
    struct validate_base<T, absl::variant<ALL_T...>> 
        : public absl::conjunction<std::is_base_of<T, ALL_T>...> {};

    /**
     * A default constructable state to represent an uninitialized or
     * invalid AttributeValue.
     */
    struct invalid : public RDFValue{
        std::string SerializeToString() const final { 
            return ""; 
        }

        /**
         * Invalid states aren't comparable
         */
        constexpr bool operator==(const invalid &) const noexcept {
            return false;
        }
    };

    /**
     * AttributeValue storage type
     *
     * To add additional storage types add them to this variant.
     * Types must inherit from RDFValue and also need to be added
     * to the type enum below (in order).
     */
    using storage = absl::variant<invalid, RDFBytes,  
        XSDString, MD5Hash, SHA1Hash, SHA256Hash, SHA512Hash,
        Blake2BHash, XSDInteger, XSDBoolean, URN>;
    static_assert(validate_base<RDFValue, storage>::value, 
        "storage types must all inherit from RDFValue");

  public:
    /**
     * AttributeValue storage types
     * 
     * This enum must stay in sync with the types in the storage variant
     */
    enum type {
        INVALID,
        RDFBytes,
        XSDString,
        MD5Hash,
        SHA1Hash,
        SHA256Hash,
        SHA512Hash,
        Blake2BHash,
        XSDInteger,
        XSDBoolean,
        URN,
        _COUNT_, // This must be last and is not a valid type
    };
    static_assert(absl::variant_size<storage>::value == type::_COUNT_,
        "storage types and type enum are out of sync");

    /**
     * Determines if type T is a valid AttributeValue type
     */
    template<typename T>
    using is_valid_type = is_variant_member<T, storage>;

    /**
     * Exception type that is thrown if trying to access
     * the wrong AttributeValue type
     */
    using bad_access = absl::bad_variant_access;
  
  public:
    /**
     * Constructs an AttributeValue in an invalid state
     */
    constexpr AttributeValue() = default;

    /**
     * Constructs an AttributeValue based on a member type
     */
    template <typename T, 
              typename = absl::enable_if_t<is_valid_type<T>::value>>
    constexpr AttributeValue(T &&t) noexcept 
        : val{std::forward<T>(t)} {}

    /**
     * Returns the type of the AttributeValue
     */
    constexpr type Type() const noexcept {
        return static_cast<type>(val.index());
    }

    /**
     * Tests if the AttributeValue is of type t
     */
    constexpr bool IsType(type t) const noexcept {
        return (Type() == t);
    }

    /**
     * Test is the AttributeValue is of type T
     *
     * This overload is useful for template metaprogramming
     */
    template <typename T, 
              typename = absl::enable_if_t<is_valid_type<T>::value>>
    constexpr bool IsType() const noexcept {
        return absl::holds_alternative<T>(val);
    }

    /**
     * Test whether the AttributeValue is in a valid state
     */
    constexpr bool IsValid() const noexcept {
        return !(IsType(INVALID) || val.valueless_by_exception());
    }

    /** 
     * Allows implicit casting to a boolean
     * 
     * Returns true if the AttributeValue is in a valid state
     */
    constexpr operator bool() const noexcept {
        return IsValid();
    }

    /**
     * Allows implicit casting to valid AttributeValue types.
     *
     * If the AttributeValue is not of type T, then an AttributeValue::bad_access
     * exception is thrown.
     */
    template <typename T, 
              typename = absl::enable_if_t<is_valid_type<T>::value>>
    constexpr operator const T&() const {
        return absl::get<T>(val);
    }

    /**
     * Allows implicit casting to valid AttributeValue type pointers.
     *
     * If the AttributeValue is not of type T, then nullptr is returned
     */
    template <typename T, 
              typename = absl::enable_if_t<is_valid_type<T>::value>>
    constexpr operator const T*() const noexcept {
        return absl::get_if<T>(&val);
    }

    /**
     * Allows casting the AttributeValue to a RDFValue pointer
     */
    operator const RDFValue *() const noexcept {
        return absl::visit(
            [](const RDFValue & base) { return &base; },
            val);
    }

    /**
     * Allows casting the AttributeValue to a RDFValue pointer
     */
    operator RDFValue *() noexcept {
        return absl::visit(
            [](RDFValue & base) { return &base; },
            val);
    }

    /**
     * Provides convenient access to the AttributeValue's RDFValue members.
     */ 
    const RDFValue * operator->() const noexcept {
        return (*this);
    }

    /**
     * Provides convenient access to the AttributeValue's RDFValue members.
     */ 
    RDFValue * operator->() noexcept {
        return (*this);
    }

    /**
     * Allows comparision with valid AttributeValue types
     */
    template <typename T, 
              typename = absl::enable_if_t<is_valid_type<T>::value>>
    constexpr bool operator==(const T& other) const noexcept {
        return IsType<T>() && (static_cast<const T&>(*this) == other);
    }

    /**
     * Allows comparision with valid AttributeValue types
     */
    template <typename T, 
              typename = absl::enable_if_t<is_valid_type<T>::value>>
    constexpr bool operator!=(const T& other) const noexcept {
        return !operator==(other);
    }

    /**
     * Allows comparision with other Attributes
     */
    constexpr bool operator==(const AttributeValue& other) const noexcept {
        return (val == other.val);
    }

    /**
     * Allows comparision with other Attributes
     */
    constexpr bool operator!=(const AttributeValue& other) const noexcept {
        return !operator==(other);
    }

  private:  
    storage val{}; // The base storage for the AttributeValue
};

/**
 * A one-to-many set of attributes
 */
using AttributeSet = std::unordered_map<URN, std::vector<AttributeValue>>;

class AttributeFactory {
  public:
    AttributeValue CreateInstance(const std::string & name) const {
        // find name in the registry and call factory method.
        const auto it = storage.find(name);
        if (it == storage.end()) {
            // Invalid type
            return {};
        }

        return it->second;
    }

    template<typename T,
             typename = absl::enable_if_t<AttributeValue::is_valid_type<T>::value>>
    void Register(std::string && name) {
        storage.emplace(std::forward<std::string>(name), T{});
    }

  private:
    std::unordered_map<std::string, const AttributeValue> storage{};
};

// A Global Registry for RDFValue. This factory will provide the correct
// RDFValue instance based on the turtle type URN. For example xsd:integer ->
// XSDInteger().
extern AttributeFactory AttributeRegistry;

} // namespace aff4