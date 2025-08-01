/*
 * Copyright (C) 2016-2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <initializer_list>
#include <iterator>
#include <optional>
#include <type_traits>
#include <wtf/Assertions.h>
#include <wtf/EnumTraits.h>
#include <wtf/FastMalloc.h>
#include <wtf/MathExtras.h>
#include <wtf/StdLibExtras.h>

namespace WTF {

template<typename E> class OptionSet;

// OptionSet is a class that represents a set of enumerators in a space-efficient manner. The enumerators
// must be powers of two greater than 0. This class is useful as a replacement for passing a bitmask of
// enumerators around.
template<typename E> class OptionSet {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(OptionSet);
    static_assert(std::is_enum<E>::value, "T is not an enum type");

public:
    using StorageType = std::make_unsigned_t<std::underlying_type_t<E>>;

    template<typename StorageType> class Iterator {
        WTF_DEPRECATED_MAKE_FAST_ALLOCATED(Iterator);
    public:
        // Isolate the rightmost set bit.
        E operator*() const { return static_cast<E>(m_value & -m_value); }

        // Iterates from smallest to largest enum value by turning off the rightmost set bit.
        Iterator& operator++()
        {
            m_value &= m_value - 1;
            return *this;
        }

        Iterator& operator++(int) = delete;

        friend bool operator==(const Iterator&, const Iterator&) = default;

    private:
        Iterator(StorageType value) : m_value(value) { }
        friend OptionSet;

        StorageType m_value;
    };

    using iterator = Iterator<StorageType>;

    static constexpr OptionSet fromRaw(StorageType rawValue)
    {
        return OptionSet(static_cast<E>(rawValue), FromRawValue);
    }

    constexpr OptionSet() = default;

    constexpr OptionSet(E e)
        : m_storage(static_cast<StorageType>(e))
    {
        ASSERT(!m_storage || hasOneBitSet(static_cast<StorageType>(e)));
    }

    constexpr OptionSet(std::initializer_list<E> initializerList)
    {
        for (auto& option : initializerList) {
            ASSERT(hasOneBitSet(static_cast<StorageType>(option)));
            m_storage |= static_cast<StorageType>(option);
        }
    }

    constexpr OptionSet(std::optional<E> optional)
        : m_storage(optional ? static_cast<StorageType>(*optional) : 0)
    {
    }

    constexpr StorageType toRaw() const { return m_storage; }

    constexpr bool isEmpty() const { return !m_storage; }

    constexpr iterator begin() const { return m_storage; }
    constexpr iterator end() const { return 0; }

    constexpr explicit operator bool() const { return !isEmpty(); }

    constexpr bool contains(E option) const
    {
        return containsAny(option);
    }

    constexpr bool containsAny(OptionSet optionSet) const
    {
        return !!(*this & optionSet);
    }

    constexpr bool containsAll(OptionSet optionSet) const
    {
        return (*this & optionSet) == optionSet;
    }

    constexpr bool containsOnly(OptionSet optionSet) const
    {
        return *this == (*this & optionSet);
    }

    constexpr void add(OptionSet optionSet)
    {
        m_storage |= optionSet.m_storage;
    }

    constexpr void remove(OptionSet optionSet)
    {
        m_storage &= ~optionSet.m_storage;
    }

    constexpr void set(OptionSet optionSet, bool value)
    {
        if (value)
            add(optionSet);
        else
            remove(optionSet);
    }

    constexpr bool hasExactlyOneBitSet() const
    {
        return m_storage && !(m_storage & (m_storage - 1));
    }

    constexpr std::optional<E> toSingleValue() const
    {
        return hasExactlyOneBitSet() ? std::optional<E>(static_cast<E>(m_storage)) : std::nullopt;
    }

    friend constexpr bool operator==(const OptionSet&, const OptionSet&) = default;

    constexpr friend OptionSet operator|(OptionSet lhs, OptionSet rhs)
    {
        return fromRaw(lhs.m_storage | rhs.m_storage);
    }

    constexpr OptionSet& operator|=(const OptionSet& other)
    {
        add(other);
        return *this;
    }

    constexpr friend OptionSet operator&(OptionSet lhs, OptionSet rhs)
    {
        return fromRaw(lhs.m_storage & rhs.m_storage);
    }

    constexpr friend OptionSet operator-(OptionSet lhs, OptionSet rhs)
    {
        return fromRaw(lhs.m_storage & ~rhs.m_storage);
    }

    constexpr friend OptionSet operator^(OptionSet lhs, OptionSet rhs)
    {
        return fromRaw(lhs.m_storage ^ rhs.m_storage);
    }

    static OptionSet all() { return fromRaw(-1); }

private:
    enum InitializationTag { FromRawValue };
    constexpr OptionSet(E e, InitializationTag)
        : m_storage(static_cast<StorageType>(e))
    {
    }
    StorageType m_storage { 0 };
};

namespace IsValidOptionSetHelper {
template<typename T, typename E> struct OptionSetValueChecker;
template<typename T, typename E, E e, E... es>
struct OptionSetValueChecker<T, EnumValues<E, e, es...>> {
    static constexpr T allValidBits() { return static_cast<T>(e) | OptionSetValueChecker<T, EnumValues<E, es...>>::allValidBits(); }
};
template<typename T, typename E>
struct OptionSetValueChecker<T, EnumValues<E>> {
    static constexpr T allValidBits() { return 0; }
};
}

template<typename E>
WARN_UNUSED_RETURN constexpr bool isValidOptionSet(OptionSet<E> optionSet)
{
    // FIXME: Remove this when all OptionSet enums are migrated to generated serialization.
    auto allValidBitsValue = IsValidOptionSetHelper::OptionSetValueChecker<std::make_unsigned_t<std::underlying_type_t<E>>, typename EnumTraits<E>::values>::allValidBits();
    return (optionSet.toRaw() | allValidBitsValue) == allValidBitsValue;
}

} // namespace WTF

using WTF::OptionSet;
