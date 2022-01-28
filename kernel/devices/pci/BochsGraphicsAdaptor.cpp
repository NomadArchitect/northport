#include <devices/pci/BochsGraphicsAdaptor.h>
#include <devices/interfaces/GenericGraphicsAdaptor.h>
#include <memory/Paging.h>
#include <Memory.h>
#include <Maths.h>
#include <Log.h>

#define BGA_DISPI_DISABLE 0x0
#define BGA_DISPI_ENABLE 0x1
#define BGA_DISPI_LFB_ENABLED 0x40
#define BGA_DISPI_NO_CLEAR_MEM 0x80

namespace Kernel::Devices::Pci
{
    //peak c++ efficiency right here ;-;
    namespace BochsGraphics
    {
        void* InitNew(Drivers::DriverInitInfo* initInfo)
        { return new BochsGraphicsDriver(initInfo); }

        void Destroy(void* inst)
        { delete static_cast<BochsGraphicsDriver*>(inst); }

        void HandleEvent(void* inst, Drivers::DriverEventType type, void* arg)
        { static_cast<BochsGraphicsDriver*>(inst)->HandleEvent(type, arg); }
    }

    using namespace Interfaces;

    BochsGraphicsDriver::BochsGraphicsDriver(Drivers::DriverInitInfo* info)
    {
        (void)info;

        adaptor = new BochsGraphicsAdaptor();
        adaptor->Init();
    }

    BochsGraphicsDriver::~BochsGraphicsDriver()
    {
        adaptor->Destroy();
        delete adaptor;
        adaptor = nullptr;
    }

    void BochsGraphicsDriver::HandleEvent(Drivers::DriverEventType type, void* arg)
    { 
        (void)type; (void)arg;
    }

    BochsGraphicsAdaptor* BochsGraphicsDriver::GetAdaptor() const
    { return adaptor; }

    enum BgaDispiReg : uint16_t
    {
        Id = 0,
        XRes = 1,
        YRes = 2,
        Bpp = 3,
        Enable = 4,
        Bank = 5,
        VirtWidth = 6,
        VirtHeight = 7,
        XOffset = 8,
        YOffset = 9,
    };

    void BochsFramebuffer::WriteVgaReg(uint16_t reg, uint16_t data) const
    {
        if (mmioBase.ptr != nullptr)
            sl::MemWrite<uint16_t>(mmioBase.raw + reg - 0x3c0 + 0x400, data);
        else
            CPU::PortWrite16(reg, data);
    }

    uint16_t BochsFramebuffer::ReadVgaReg(uint16_t reg) const
    {
        if (mmioBase.ptr != nullptr)
            return sl::MemRead<uint16_t>(mmioBase.raw + reg - 0x3c0 + 0x400);
        else
            return CPU::PortRead16(reg);
    }

    void BochsFramebuffer::WriteDispiReg(uint16_t reg, uint16_t data) const
    {
        if (mmioBase.ptr != nullptr)
            sl::MemWrite<uint16_t>(mmioBase.raw + 0x500 + (reg << 1), data);
        else
        {
            //magic numbers here are BGA DISPI index and data ports, respectively.
            CPU::PortWrite16(0x01CE, reg);
            CPU::PortWrite16(0x01CF, data);
        }
    }

    uint16_t BochsFramebuffer::ReadDispiReg(uint16_t reg) const
    {
        if (mmioBase.ptr != nullptr)
            return sl::MemRead<uint16_t>(mmioBase.raw + 0x500 + (reg << 1));
        else
        {
            CPU::PortWrite16(0x01CE, reg);
            return CPU::PortRead16(0x01CF);
        }
    }

    void BochsFramebuffer::Init()
    {
        ScopedSpinlock scopeLock(&lock);
        ready = false;

        auto maybePciDevice = PciBridge::Global()->FindDevice(0x1234, 0x1111);
        if (!maybePciDevice)
        {
            Log("Cannot initialize bochs framebuffer: matching pci device not found.", LogSeverity::Error);
            return;
        };

        auto maybePciFunction = maybePciDevice.Value()->GetFunction(0);
        if (!maybePciFunction)
        {
            Log("Cannot initialize bochs framebuffer: pci device does not have function 0.", LogSeverity::Error);
            return;
        }

        const PciFunction* pciFunc = *maybePciFunction;
        bool mmioRegsAvailable = false;
        if (pciFunc->GetHeader()->ids.deviceSubclass == 0x80)
            mmioRegsAvailable = true; //qemu legacy free variant
        else if (pciFunc->GetHeader()->bars[2].size > 0)
            mmioRegsAvailable = true; //bochs standard variant, but BAR2 is populated so mmio regs are available

        using VMM = Memory::PageTableManager;
        linearFramebufferBase.raw = pciFunc->GetHeader()->bars[0].address;
        const size_t fbPageCount = pciFunc->GetHeader()->bars[0].size / PAGE_FRAME_SIZE; //TODO: investigate map issues here, puts framebuffer in a weird state
        // VMM::Local()->MapRange(EnsureHigherHalfAddr(linearFramebufferBase.raw), linearFramebufferBase, fbPageCount, Memory::MemoryMapFlag::AllowWrites);
        linearFramebufferBase.raw = EnsureHigherHalfAddr(linearFramebufferBase.raw);

        if (mmioRegsAvailable)
        {
            mmioBase.raw = pciFunc->GetHeader()->bars[2].address;
            const size_t mmioPageCount = pciFunc->GetHeader()->bars[2].size / PAGE_FRAME_SIZE;
            VMM::Local()->MapRange(EnsureHigherHalfAddr(mmioBase.raw), mmioBase, mmioPageCount, Memory::MemoryMapFlag::AllowWrites);
            mmioBase.raw = EnsureHigherHalfAddr(mmioBase.raw);

            Log("Bochs framebuffer is legacy free variant, using mmio registers.", LogSeverity::Verbose);
        }
        else
        {
            mmioBase.ptr = nullptr;
            Log("Bochs framebuffer does not support registers via mmio, using port io instead.", LogSeverity::Warning);
        }

        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_DISABLE);
        width = ReadDispiReg(BgaDispiReg::XRes);
        height = ReadDispiReg(BgaDispiReg::YRes);
        bpp = ReadDispiReg(BgaDispiReg::Bpp);
        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_ENABLE | BGA_DISPI_LFB_ENABLED | BGA_DISPI_NO_CLEAR_MEM);

        ready = true;
    }

    void BochsFramebuffer::Destroy()
    {
        ScopedSpinlock scopeLock(&lock);
    }

    bool BochsFramebuffer::IsAvailable() const
    { return ready; }

    bool BochsFramebuffer::CanModeset() const
    { return true; }

    void BochsFramebuffer::SetMode(FramebufferModeset& modeset)
    {
        ScopedSpinlock scopeLock(&lock);

        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_DISABLE);

        WriteDispiReg(BgaDispiReg::XRes, sl::clamp<uint16_t>((uint16_t)modeset.width, 1, 1024));
        WriteDispiReg(BgaDispiReg::YRes, sl::clamp<uint16_t>((uint16_t)modeset.height, 1, 768));
        WriteDispiReg(BgaDispiReg::Bpp, sl::clamp<uint16_t>(modeset.bitsPerPixel, 4, 32));

        WriteDispiReg(BgaDispiReg::Enable, BGA_DISPI_ENABLE | BGA_DISPI_LFB_ENABLED | BGA_DISPI_NO_CLEAR_MEM);
    }

    FramebufferModeset BochsFramebuffer::GetCurrentMode() const
    { return { width, height, bpp }; }

    sl::Opt<sl::NativePtr> BochsFramebuffer::GetAddress() const
    {
        if (!IsAvailable())
            return {};

        return linearFramebufferBase;
    }

    void BochsGraphicsAdaptor::Init()
    {
        if (framebuffer == nullptr)
            framebuffer = new BochsFramebuffer();
        framebuffer->Init();
    }

    void BochsGraphicsAdaptor::Destroy()
    {
        if (framebuffer != nullptr)
        {
            framebuffer->Destroy();
            delete framebuffer;
            framebuffer = nullptr;
        }
    }

    size_t BochsGraphicsAdaptor::GetFramebuffersCount() const
    { return 1; }

    GenericFramebuffer* BochsGraphicsAdaptor::GetFramebuffer(size_t index) const
    { 
        if (index == 0)
            return framebuffer;
        else
            return nullptr;
    }
}