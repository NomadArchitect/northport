#pragma once

#include <stddef.h>
#include <Maths.h>

namespace np::Userland
{
    constexpr size_t heapStartingSize = 16 * KB;
    constexpr size_t heapStartingAddr = 1 * GB;
    constexpr size_t heapMinAllocSize = 0x20; //TODO: increase this, and smaller allocs use slabs instead
    constexpr size_t heapExpandRequestSize = 4 * KB;
    
    //called to setup a friendly environment for a user program to run in: cmd line args, and all that.
    void InitUserlandApp();
}