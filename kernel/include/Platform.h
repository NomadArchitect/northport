#pragma once

#include <stdint.h>

#define PAGE_FRAME_SIZE 0x1000
#define PORT_DEBUGCON 0xE9
#define MSR_IA32_EFER 0xC0000080
#define MSR_FS_BASE 0xC0000100
#define MSR_GS_BASE 0xC0000101
#define MSR_GS_KERNEL_BASE 0xC0000102

#define FORCE_INLINE __attribute__((always_inline)) inline

namespace Kernel
{
    FORCE_INLINE void SpinlockAcquire(void* lock)
    {
        while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE));
    }

    FORCE_INLINE void SpinlockRelease(void* lock)
    {
        __atomic_clear(lock, __ATOMIC_RELEASE);
    }

    FORCE_INLINE uint64_t ReadCR0()
    {
        uint64_t value;
        asm volatile("mov %%cr0, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE void WriteCR0(uint64_t value)
    {
        asm volatile("mov %0, %%cr0" :: "r"(value));
    }

    FORCE_INLINE uint64_t ReadCR2()
    {
        uint64_t value;
        asm volatile("mov %%cr2, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE uint64_t ReadCR3()
    {
        uint64_t value;
        asm volatile("mov %%cr3, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE void WriteCR3(uint64_t value)
    {
        asm volatile("mov %0, %%cr3" :: "r"(value));
    }

    FORCE_INLINE uint64_t ReadCR4()
    {
        uint64_t value;
        asm volatile("mov %%cr4, %0" : "=r"(value));
        return value;
    }

    FORCE_INLINE void WriteCR4(uint64_t value)
    {
        asm volatile("mov %0, %%cr4" :: "r"(value));
    }
}