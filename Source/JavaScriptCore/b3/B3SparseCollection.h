/*
 * Copyright (C) 2016-2021 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#if ENABLE(DFG_JIT)

#include <wtf/StdLibExtras.h>
#include <wtf/Vector.h>

namespace JSC { namespace B3 {

// B3::Procedure and Air::Code have a lot of collections of indexed things. This has all of the
// logic.

template<typename T>
class SparseCollection {
    typedef Vector<std::unique_ptr<T>> VectorType;
    
public:
    SparseCollection()
    {
    }

    T* add(std::unique_ptr<T> value)
    {
        T* result = value.get();
        
        size_t index;
        if (m_indexFreeList.isEmpty()) {
            index = m_vector.size();
            m_vector.append(nullptr);
        } else
            index = m_indexFreeList.takeLast();

        value->m_index = index;
        ASSERT(!m_vector[index]);
        new (NotNull, &m_vector[index]) std::unique_ptr<T>(WTFMove(value));

        return result;
    }

    T* cloneAndAdd(const T& node)
    {
        return add(makeUnique<T>(node));
    }

    template<typename... Arguments>
    T* addNew(Arguments&&... arguments)
    {
        return add(std::unique_ptr<T>(new T(std::forward<Arguments>(arguments)...)));
    }

    void remove(T* value)
    {
        RELEASE_ASSERT(m_vector[value->m_index].get() == value);
        m_indexFreeList.append(value->m_index);
        m_vector[value->m_index] = nullptr;
    }

    void packIndices()
    {
        if (m_indexFreeList.isEmpty())
            return;

        unsigned holeIndex = 0;
        unsigned endIndex = m_vector.size();

        while (true) {
            while (holeIndex < endIndex && m_vector[holeIndex])
                ++holeIndex;

            if (holeIndex == endIndex)
                break;
            ASSERT(holeIndex < m_vector.size());
            ASSERT(!m_vector[holeIndex]);

            do {
                --endIndex;
            } while (!m_vector[endIndex] && endIndex > holeIndex);

            if (holeIndex == endIndex)
                break;
            ASSERT(endIndex > holeIndex);
            ASSERT(m_vector[endIndex]);

            auto& value = m_vector[endIndex];
            value->m_index = holeIndex;
            m_vector[holeIndex] = WTFMove(value);
            ++holeIndex;
        }

        m_indexFreeList.shrink(0);
        m_vector.shrink(endIndex);
    }

    void clearAll()
    {
        m_indexFreeList.clear();
        m_vector.clear();
    }

    unsigned size() const { return m_vector.size(); }
    bool isEmpty() const { return m_vector.isEmpty(); }
    
    T* at(unsigned index) const { return m_vector[index].get(); }
    T* operator[](unsigned index) const { return at(index); }

    class iterator {
    public:
        iterator()
            : m_collection(nullptr)
            , m_index(0)
        {
        }

        iterator(const SparseCollection& collection, unsigned index)
            : m_collection(&collection)
            , m_index(findNext(index))
        {
        }

        T* operator*()
        {
            return m_collection->at(m_index);
        }

        iterator& operator++()
        {
            m_index = findNext(m_index + 1);
            return *this;
        }

        bool operator==(const iterator& other) const
        {
            ASSERT(m_collection == other.m_collection);
            return m_index == other.m_index;
        }

    private:
        friend class SparseCollection;

        unsigned findNext(unsigned index)
        {
            while (index < m_collection->size() && !m_collection->at(index))
                index++;
            return index;
        }

        const SparseCollection* m_collection;
        unsigned m_index;
    };

    iterator begin() const { return iterator(*this, 0); }
    iterator end() const { return iterator(*this, size()); }

private:
    Vector<std::unique_ptr<T>, 0, UnsafeVectorOverflow> m_vector;
    Vector<size_t, 0, UnsafeVectorOverflow> m_indexFreeList;
};

} } // namespace JSC::B3

#endif // ENABLE(DFG_JIT)
