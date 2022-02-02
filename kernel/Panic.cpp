#include <Panic.h>
#include <Log.h>
#include <Cpu.h>
#include <StackTrace.h>
#include <StringCulture.h>
#include <elf/HeaderParser.h>
#include <scheduling/Scheduler.h>
#include <LinearFramebuffer.h>
#include <TerminalFramebuffer.h>
#include <formats/XPixMap.h>
#include <Locks.h>

extern const char* panicBitmapData[];

namespace Kernel
{
    struct PanicDetails
    {
        char panicLock; //helps prevent panic loops
        bool initialized;
        const char* message;

        np::Graphics::LinearFramebuffer* fb;
    };

    constexpr const char* generalRegStrings[] = 
    {
        "rax:",
        "rbx:",
        "rcx:",
        "rdx:",
        "rsi:",
        "rdi:",
        "rsp:",
        "rbp:",
        " r8:",
        " r9:",
        "r10:",
        "r11:",
        "r12:",
        "r13:",
        "r14:",
        "r15:",
    };

    PanicDetails* panicDetails;
    void InitPanic()
    {
        panicDetails = new PanicDetails();
        sl::SpinlockRelease(&panicDetails->panicLock);

        Log("Panic subsystem is ready.", LogSeverity::Info);
    }

    using namespace np::Graphics;
    void SetPanicFramebuffer(void* linearFramebuffer)
    {
        panicDetails->fb = reinterpret_cast<LinearFramebuffer*>(linearFramebuffer);
    }

    constexpr size_t borderLeft = 3;
    constexpr size_t registerPrintStride = 30;
    constexpr uint32_t bgColour = 0x54'00'00'00;

    void RenderBackgroundBitmap(TerminalFramebuffer& fb)
    {
        sl::XPixMap pixmap(panicBitmapData);

        sl::Vector<uint32_t> xColours = pixmap.GetColours();
        sl::Vector<size_t> pixels  = pixmap.GetPixels();

        sl::Vector<Colour> colours(xColours.Size());
        for (size_t i = 0; i < xColours.Size(); i++)
        {
            colours.PushBack(xColours[i] << 8); //x uses 24bpp rgb, we're exepected 32bpp rgba
        }
        colours[0] = fb.GetColour(true); //set white (we know its the first in this case) to the background

        const size_t renderScale = 2;
        const sl::Vector2u renderOffset = 
        {
            fb.GetBackingBuffer()->Size().x / 2 - (pixmap.Size().x * renderScale) / 2,
            fb.GetBackingBuffer()->Size().y - (pixmap.Size().y * renderScale)
        };

        //lock the fb once here, and we'll use NoLock within the loop
        sl::ScopedSpinlock scopeLock(fb.GetBackingBuffer()->GetLock());
        for (size_t y = 0; y < pixmap.Size().y; y++)
        {
            for (size_t x = 0; x < pixmap.Size().x; x++)
            {
                fb.GetBackingBuffer()->DrawPixel({renderOffset.x + x * renderScale, renderOffset.y + y * renderScale}, colours[pixels[y * pixmap.Size().x + x]], NoLock);
            }
        }
    }

    void PrintStackTrace(TerminalFramebuffer& fb, const StoredRegisters* regs)
    {
        size_t sectionStart = fb.GetCursorPos().y + 2;
        
        sl::Elf64HeaderParser elfInfo(currentProgramElf);
        sl::Vector<NativeUInt> trace = GetStackTrace(regs->rbp);
        PrintStackTrace(trace); //print trace to text-based outputs as well

        for (size_t i = 0; i < trace.Size(); i++)
        {
            fb.SetCursorPos({borderLeft, sectionStart + i});
            fb.Print(sl::StringCulture::current->ToString(trace[i], sl::Base::HEX).C_Str());
            fb.SetCursorPos({23, sectionStart + i});
            if (currentProgramElf.ptr != nullptr)
                fb.Print(elfInfo.GetSymbolName(trace[i]));
        }
    }

    void PrintGeneralRegs(TerminalFramebuffer& fb, const StoredRegisters* regs)
    {
        size_t sectionStart = fb.GetCursorPos().y + 2;
        const uint64_t* generalRegs = sl::NativePtr((size_t)regs).As<uint64_t>(7 * sizeof(uint64_t));

        fb.SetCursorPos({borderLeft, sectionStart});
        for (size_t reg = 0; reg < 16; reg++)
        {
            fb.SetCursorPos({borderLeft + ((reg % 4) * registerPrintStride), sectionStart + (reg / 4)});
            fb.Print(generalRegStrings[reg]);
            fb.OffsetCursorPos({1, 0});

            fb.Print("0x");
            fb.Print(sl::StringCulture::current->ToString(generalRegs[reg], sl::Base::HEX));
        }
    }

    void PrintSpecialRegs(TerminalFramebuffer& fb)
    {
        size_t sectionStart = fb.GetCursorPos().y + 2;

        //config regs first: cr0, cr4, EFER
        fb.SetCursorPos({borderLeft, sectionStart});
        fb.Print("CR0: 0x");
        fb.Print(sl::StringCulture::current->ToString(ReadCR0(), sl::Base::HEX));

        fb.SetCursorPos({borderLeft + registerPrintStride, sectionStart});
        fb.Print("CR4: 0x");
        fb.Print(sl::StringCulture::current->ToString(ReadCR4(), sl::Base::HEX));

        fb.SetCursorPos({borderLeft + registerPrintStride * 2, sectionStart});
        fb.Print("EFER: 0x");
        fb.Print(sl::StringCulture::current->ToString(CPU::ReadMsr(MSR_IA32_EFER), sl::Base::HEX));

        //print paging stuff: cr2, cr3
        fb.SetCursorPos({borderLeft, sectionStart + 1});
        fb.Print("CR2: 0x");
        fb.Print(sl::StringCulture::current->ToString(ReadCR2(), sl::Base::HEX));

        fb.SetCursorPos({borderLeft + registerPrintStride * 1, sectionStart + 1});
        fb.Print("CR3: 0x");
        fb.Print(sl::StringCulture::current->ToString(ReadCR3(), sl::Base::HEX));
    }

    void PrintIretFrame(TerminalFramebuffer& fb, const StoredRegisters* regs)
    {
        size_t sectionStart = fb.GetCursorPos().y + 2;

        fb.SetCursorPos({borderLeft, sectionStart});
        fb.Print("prev stack: ss");
        fb.Print(sl::StringCulture::current->ToString(regs->iret_ss, sl::Base::HEX));
        fb.Print(":0x");
        fb.Print(sl::StringCulture::current->ToString(regs->iret_rsp, sl::Base::HEX));

        fb.SetCursorPos({borderLeft + 2 * registerPrintStride, sectionStart});
        fb.Print("prev ip: cs");
        fb.Print(sl::StringCulture::current->ToString(regs->iret_cs, sl::Base::HEX));
        fb.Print(":0x");
        fb.Print(sl::StringCulture::current->ToString(regs->iret_rip, sl::Base::HEX));

        fb.SetCursorPos({borderLeft, sectionStart + 1});
        fb.Print("prev flags: 0x");
        fb.Print(sl::StringCulture::current->ToString(regs->iret_flags, sl::Base::HEX));

        fb.SetCursorPos({borderLeft + registerPrintStride, sectionStart + 1});
        fb.Print("error code: 0x");
        fb.Print(sl::StringCulture::current->ToString(regs->errorCode, sl::Base::HEX));

        fb.SetCursorPos({borderLeft + 2 * registerPrintStride, sectionStart + 1});
        fb.Print("vector number: 0x");
        fb.Print(sl::StringCulture::current->ToString(regs->vectorNumber, sl::Base::HEX));
    }

    [[noreturn]]
    void Panic(const char* reason)
    {
        sl::SpinlockAcquire(&panicDetails->panicLock);
        panicDetails->message = reason;
        asm volatile("int $0xFE" :: "m"(reason));
        __builtin_unreachable();
    }

    [[noreturn]]
    void PanicInternal(const char* reason, StoredRegisters* regs)
    {
        Scheduling::Scheduler::Global()->Suspend();
        EnableLogDestinaton(LogDestination::FramebufferOverwrite, false); //we're taking control of the framebuffer, make sure we dont overwrite ourselves
        Log("Panic() called, attempting to dump info", LogSeverity::Info);

        //initial renderer setup - make sure we're in a known state
        TerminalFramebuffer fb = TerminalFramebuffer(panicDetails->fb);
        fb.SetColour(true, np::Graphics::Colour(bgColour));
        fb.SetColour(false, np::Graphics::Colours::White);
        fb.ClearScreen();
        fb.SetCursorPos({ 0, 1 });

        // RenderBackgroundBitmap();

        fb.PrintLine("Panic! In the kernel. Reason:");
        fb.PrintLine(reason);

        PrintGeneralRegs(fb, regs);
        PrintSpecialRegs(fb);
        PrintIretFrame(fb, regs);
        PrintStackTrace(fb, regs);

        Log("Final log: All cpus are being placed in an infinite halt.", LogSeverity::Info);
        while (true)
            CPU::Halt();
        __builtin_unreachable();
    }
}
