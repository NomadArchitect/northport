#pragma once

#include <interfaces/driver/Api.h>
#include <interfaces/driver/Drivers.h>
#include <PciAddress.h>
#include <VmObject.h>
#include <Locks.h>

namespace Qemu
{
    enum class DispiReg : uint16_t
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

    class GraphicsAdaptor
    {
    private:
        dl::VmObject framebuffer;
        dl::VmObject mmio;
        dl::PciAddress pciAddr;
        npk_handle descriptorId;

        sl::TicketLock metadataLock;
        npk_framebuffer_mode mode;
        npk_string summaryString;

        void WriteDispiReg(DispiReg reg, uint16_t data) const;
        uint16_t ReadDispiReg(DispiReg reg) const;
        void RegenSummary();

    public:
        bool Init(const npk_event_add_device* event);
        
        [[gnu::always_inline]]
        inline npk_framebuffer_mode GetMode()
        { 
            sl::ScopedLock scopeLock(metadataLock);
            return mode; 
        }

        [[gnu::always_inline]]
        inline npk_string GetSummary()
        { 
            sl::ScopedLock scopeLock(metadataLock);
            return summaryString; 
        }
    };
}
