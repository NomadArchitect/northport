#pragma once

#include <NativePtr.h>

namespace sl
{
    template <typename WordType>
    __attribute__((always_inline)) inline void MemWrite(sl::NativePtr where, WordType val)
    {
        *reinterpret_cast<volatile WordType*>(where.ptr) = val;
    }

    template <typename WordType>
    __attribute__((always_inline)) inline WordType MemRead(sl::NativePtr where)
    {
        return *reinterpret_cast<volatile WordType*>(where.ptr);
    }

    __attribute__((always_inline)) inline void* StackAlloc(size_t size)
    {
        return __builtin_alloca(size);
    }

    void memset(void* const start, uint8_t val, size_t count);

    void memcopy(const void* const source, void* const destination, size_t count);
    void memcopy(const void* const source, size_t sourceOffset, void* const destination, size_t destOffset, size_t count);

    int memcmp(const void* const a, const void* const b);

    size_t memfirst(const void* const buff, uint8_t target, size_t upperLimit);
    size_t memfirst(const void* const buff, size_t offset, uint8_t target, size_t upperLimit);
}