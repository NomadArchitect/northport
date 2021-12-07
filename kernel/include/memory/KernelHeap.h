#pragma once

#include <stddef.h>
#include <Platform.h>
#include <NativePtr.h>

#define HEAP_ALLOC_ALIGN 0x10

namespace Kernel::Memory
{
    class KernelHeap;
    
    struct HeapNode
    {
#ifdef NORTHPORT_DEBUG_USE_HEAP_CANARY
        uint64_t canary;
#endif
        HeapNode* prev;
        HeapNode* next;
        size_t length;
        bool free;

        void CombineWithNext(KernelHeap& heap);
        void CarveOut(size_t allocSize, KernelHeap& heap);
    };
    
    class KernelHeap
    {
    friend HeapNode;
    private:
        HeapNode* head;
        HeapNode* tail;
        char lock;

        void ExpandHeap(size_t nextAllocSize);

    public:
        static KernelHeap* Global();

        void Init(sl::NativePtr baseAddress);

        void* Alloc(size_t size);
        void Free(void* ptr);
    };
}

void* malloc(size_t size);
void free(void* ptr);
