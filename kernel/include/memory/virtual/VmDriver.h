#pragma once

#include <stdint.h>
#include <stddef.h>
#include <Optional.h>
#include <Locks.h>
#include <memory/Vmm.h>
#include <arch/Hat.h>

namespace Npk::Memory
{ class VirtualMemoryManager; }

namespace Npk::Memory::Virtual
{
    constexpr HatFlags ConvertFlags(VmFlags flags)
    {
        /*  VMFlags are stable across platforms, while HatFlags have different meanings
            depending on the ISA. This provides source-level translation between the two. */
        HatFlags value = HatFlags::None;
        if ((flags & VmFlags::Write) != VmFlags::None)
            value |= HatFlags::Write;
        if ((flags & VmFlags::Execute) != VmFlags::None)
            value |= HatFlags::Execute;
        if ((flags & VmFlags::User) != VmFlags::None)
            value |= HatFlags::User;
        
        return value;
    }

    enum class EventResult
    {
        Continue,   //thread is allowed to continue.
        Kill,       //thread did something bad, kill it.
        Suspend,    //op will need time to complete, suspend thread or return async-handle.
    };

    enum class EventType
    {
        PageFault,
    };

    enum class VmDriverType : size_t
    {
        //0 is reserved, and will result in nullptr being returned.
        //This forces alloc requests to set the `type` field.
        Anon = 1,
        Kernel = 2,
        
        EnumCount
    };

    constexpr inline const char* VmDriverTypeStrs[] = 
    {
        "null", "kernel/mmio", "anonymous"
    };

    struct VmDriverContext
    {
        sl::TicketLock& lock;
        HatMap* map;
        VmRange range;
    };
    
    class VmDriver
    {
    friend VirtualMemoryManager;
    private:
        static VmDriver* GetDriver(VmDriverType name);

    public:
        static void InitEarly();

        virtual void Init() = 0;
        virtual VmDriverType Type() = 0;
        
        //driver needs to handle an event.
        virtual EventResult HandleEvent(VmDriverContext& context, EventType type, uintptr_t addr, uintptr_t eventArg) = 0;
        //driver should attach itself to a range, and prepare to back it accordingly.
        virtual sl::Opt<size_t> AttachRange(VmDriverContext& context, uintptr_t attachArg) = 0;
        //driver should release resources backing a range.
        virtual bool DetachRange(VmDriverContext& context) = 0;
    };
}
