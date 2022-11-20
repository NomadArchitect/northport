#include <arch/riscv64/Interrupts.h>
#include <debug/Log.h>
#include <interrupts/InterruptManager.h>
#include <interrupts/Ipi.h>
#include <memory/Vmm.h>
#include <tasking/Scheduler.h>

namespace Npk
{
    extern uint8_t TrapEntry[] asm("TrapEntry");
    
    void LoadStvec()
    {
        ASSERT(((uintptr_t)TrapEntry & 3) == 0, "Trap entry address misaligned.");

        //we use direct interrupts so we leave the mode bits clear.
        constexpr uintptr_t ModeMask = ~(uintptr_t)0b11;
        WriteCsr("stvec", (uintptr_t)TrapEntry & ModeMask);

        //enable supervisor interrupts: software, timer, external.
        SetCsrBits("sie", 0x222);
    }

    extern void (*timerCallback)(size_t);
}

extern "C"
{
    void TrapDispatch(Npk::TrapFrame* frame)
    {
        using namespace Npk;
        Tasking::Scheduler::Global().SaveCurrentFrame(frame, CoreLocal().runLevel);
        CoreLocal().runLevel = RunLevel::IntHandler;

        const bool isInterrupt = frame->vector & (1ul << 63);
        frame->vector &= ~(1ul << 63);

        if (isInterrupt)
        {
            switch (frame->vector)
            {
            case 1:
                Interrupts::ProcessIpiMail();
                break;
            case 5:
                if (timerCallback != nullptr)
                    timerCallback(5);
                break;
            case 9:
                Log("Got riscv external interrupt.", LogLevel::Fatal);
                break;
            default:
                Interrupts::InterruptManager::Global().Dispatch(frame->vector);
                break;
            }
            
            ClearCsrBits("sip", 1 << frame->vector);
        }
        else if (frame->vector >= 12 && frame->vector <= 15)
        {
            using Memory::VmFaultFlags;
            VmFaultFlags flags = VmFaultFlags::None;
            if (frame->vector == 12)
                flags |= VmFaultFlags::Execute;
            else if (frame->vector == 13)
                flags |= VmFaultFlags::Read;
            else if (frame->vector == 15)
                flags |= VmFaultFlags::Write;
            
            if (frame->flags.spp == 0)
                flags |= VmFaultFlags::User;
            if (frame->ec < hhdmBase)
                VMM::Current().HandleFault(frame->ec, flags);
            else
                VMM::Kernel().HandleFault(frame->ec, flags);
        }
        else
            Log("Native CPU exception: 0x%lx, ec=0x%lx.", LogLevel::Fatal, frame->vector, frame->ec);

        Tasking::Scheduler::Global().RunNextFrame();
        ExecuteTrapFrame(frame);
    }
}
