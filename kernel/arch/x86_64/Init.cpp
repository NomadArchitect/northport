#include <arch/x86_64/Apic.h>
#include <arch/x86_64/Gdt.h>
#include <arch/x86_64/Idt.h>
#include <arch/Cpu.h>
#include <arch/Timers.h>
#include <arch/Platform.h>
#include <boot/LimineTags.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <drivers/DriverManager.h>
#include <memory/Vmm.h>
#include <cpuid.h>

#include <tasking/Clock.h>

namespace Npk
{
    void InitExtendedState()
    {
        ASSERT(CpuHasFeature(CpuFeature::FPU), "FPU required by x86_64");
        ASSERT(CpuHasFeature(CpuFeature::SSE), "SSE1 required by x86_64");
        ASSERT(CpuHasFeature(CpuFeature::SSE2), "SSE2 required by x86_64");
        ASSERT(CpuHasFeature(CpuFeature::FxSave), "FXSAVE required by x86_64");

        uint64_t cr0 = ReadCr0();
        cr0 |= 1 << 1; //monitor co-processor, required.
        cr0 |= 1 << 5; //enable x87 exceptions
        cr0 &= ~(3ul << 2); //clear bits 2 (emulate) and 3 (task switched)
        WriteCr0(cr0);

        uint64_t cr4 = ReadCr4();
        cr4 |= 3 << 9; //set bits 9 and 10: enable SSE and FXSAVE
        WriteCr4(cr4);

        CoreConfig* localConfig = static_cast<CoreConfig*>(CoreLocal()[LocalPtr::Config]);
        localConfig->xSaveBitmap = 0;

        if (CpuHasFeature(CpuFeature::XSave))
        {
            unsigned int a, b, c, d;
            ASSERT(__get_cpuid_count(0xD, 0, &a, &b, &c, &d) == 1, "Bad XSAVE cpuid leaf");
            localConfig->xSaveBitmap = -1ul; //enable everything for now
            localConfig->xSaveBufferSize = c;

            cr4 = ReadCr4();
            cr4 |= 1 << 18; //tell the processor we support XSAVE
            WriteCr4(cr4);

            Log("Enabled xsave feature-set, bufferSize=%lub", LogLevel::Verbose, 
                localConfig->xSaveBufferSize);
        }
        else
            Log("Xsave not available, using fxsave instead", LogLevel::Verbose);
    }

    void InitCore(uintptr_t id, uintptr_t acpiId)
    {
        //ensure we're using the kernel's pagemap.
        VMM::Kernel().MakeActive();

        BlockSumac();
        FlushGdt();
        LoadIdt();

        CoreLocalInfo* clb = new CoreLocalInfo();
        clb->id = id;
        clb->acpiId = acpiId;
        clb->runLevel = RunLevel::Dpc;
        clb->nextStack = nullptr;
        (*clb)[LocalPtr::IntControl] = new LocalApic();
        WriteMsr(MsrGsBase, (uintptr_t)clb);

        CoreConfig* config = new CoreConfig();
        CoreLocal()[LocalPtr::Config] = config;
        ScanLocalCpuFeatures();
        ScanLocalTopology();

        uint64_t cr0 = ReadCr0();
        cr0 |= 1 << 16; //set write-protect bit
        cr0 &= ~0x6000'0000; //ensure CD/NW are cleared, enabling caching for this core.
        WriteCr0(cr0);

        uint64_t cr4 = ReadCr4();
        if (CpuHasFeature(CpuFeature::Smap))
            cr4 |= 1 << 21; //SMA enable: prevent accessing user pages as the supervisor while AC is clear
        if (CpuHasFeature(CpuFeature::Smep))
            cr4 |= 1 << 20; //Prevent executing from user pages while in ring 0.
        if (CpuHasFeature(CpuFeature::Umip))
            cr4 |= 1 << 11; //prevents system store instructions in user mode (sidt/sgdt)
        if (CpuHasFeature(CpuFeature::GlobalPages))
            cr4 |= 1 << 7; //global pages: pages that are global.
        WriteCr4(cr4);

        if (CpuHasFeature(CpuFeature::NoExecute))
            WriteMsr(MsrEfer, ReadMsr(MsrEfer) | (1 << 11));

        //my stack overfloweth (alt title: maximum maintainability).
        Log("Core %lu enabled features: write-protect%s%s%s%s%s.", LogLevel::Info, 
            id,
            cr4 & (1 << 21) ? ", smap" : "",
            cr4 & (1 << 20) ? ", smep" : "",
            cr4 & (1 << 11) ? ", umip" : "",
            cr4 & (1 << 7) ? ", global-pages" : "",
            CpuHasFeature(CpuFeature::NoExecute) ? ", nx" : "");

        InitExtendedState();

        LocalApic::Local().Init();
        EnableInterrupts();
    }

    void ApEntry(limine_smp_info* info)
    {
        WriteMsr(MsrGsBase, 0); //ensure we wont load bogus core-local info
        InitCore(info->lapic_id, info->processor_id);

        PerCoreCommonInit();
        ExitCoreInit();
        ASSERT_UNREACHABLE();
    }

    static bool PortIoAccess(size_t width, uintptr_t addr, uintptr_t* data, bool write)
    {
        switch (width)
        {
        case 1:
            write ? Out8(addr, *data) : (void)(*data = In8(addr));
            return true;
        case 2:
            write ? Out16(addr, *data) : (void)(*data = In16(addr));
            return true;
        case 4:
            write ? Out32(addr, *data) : (void)(*data = In32(addr));
            return true;
        default:
            return false;
        }
    }

    void ThreadedArchInit()
    {
        npk_add_bus_access(BusPortIo, PortIoAccess);

        //TODO: check if ecam was not available, and add port io descriptor for pci
    }
}

extern "C"
{
#ifdef NP_X86_64_E9_ALLOWED
    static void DebugconWrite(sl::StringSpan text)
    {
        for (size_t i = 0; i < text.Size(); i++)
            Npk::Out8(Npk::PortDebugcon, text[i]);
    }

    Npk::Debug::LogOutput debugconOutput
    {
        .Write = DebugconWrite,
        .BeginPanic = nullptr
    };
#endif

    void KernelEntry()
    {
        using namespace Npk;

#ifdef NP_X86_64_E9_ALLOWED
        Debug::AddLogOutput(&debugconOutput);
#endif

        WriteMsr(MsrGsBase, 0);
        InitEarlyPlatform();
        InitMemory();

        PopulateIdt();
        InitPlatform();

        IoApic::InitAll();

        if (Boot::smpRequest.response != nullptr)
        {
            for (size_t i = 0; i < Boot::smpRequest.response->cpu_count; i++)
            {
                limine_smp_info* procInfo = Boot::smpRequest.response->cpus[i];
                if (procInfo->lapic_id == Boot::smpRequest.response->bsp_lapic_id)
                {
                    InitCore(procInfo->lapic_id, procInfo->processor_id);
                    PerCoreCommonInit();
                    continue;
                }
                
                procInfo->goto_address = ApEntry;
                Log("Sending bring-up request to core %u.", LogLevel::Verbose, procInfo->lapic_id);
            }
        }
        else
        {
            InitCore(0, 0);
            PerCoreCommonInit();
        }

        InitTimers();
        Tasking::StartSystemClock();
        ExitCoreInit();
        ASSERT_UNREACHABLE();
    }
}
