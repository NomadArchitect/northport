#include <devices/LApic.h>
#include <acpi/AcpiTables.h>
#include <Platform.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel::Devices
{
    void LvtEntry::Set(uint8_t vector)
    {
        raw = vector | (1 << 16); 
    }

    void LvtEntry::Set(uint8_t vector, ApicDeliveryMode mode, bool levelTriggered)
    {
        Set(vector, mode, false, levelTriggered);
    }

    void LvtEntry::Set(uint8_t vector, ApicDeliveryMode mode, bool activeLow, bool levelTriggered)
    {
        raw = vector | ((uint64_t)mode << 8) | (activeLow ? 1 << 13 : 0) | (levelTriggered ? 1 << 15 : 0) | (1 << 16);
    }

    void LvtEntry::Set(uint8_t vector, ApicTimerMode mode)
    {
        //timer specific entry
        raw = vector | (1 << 16) | ((uint64_t)mode << 17);
    }

    void IcrPacket::Set(uint8_t vector, ApicDeliveryMode mode, bool levelTrigger, uint8_t destinationId)
    {
        raw = vector | ((uint64_t)mode << 8) | (levelTrigger ? 1 << 15 : 0) | ((uint64_t)destinationId << 56);
    }

    void LApic::WriteReg(LocalApicRegister reg, uint32_t value)
    { sl::MemWrite<uint32_t>(baseAddress + (uint64_t)reg, value); }

    uint32_t LApic::ReadReg(LocalApicRegister reg)
    { return sl::MemRead<uint32_t>(baseAddress + (uint64_t)reg); }

    void LApic::CalibrateTimer()
    {}

    LApic* LApic::Local()
    {
        return GetCoreLocal()->ptrs[CoreLocalIndices::LAPIC].As<LApic>();
    }

    void LApic::Init()
    {
        if (!CPU::FeatureSupported(CpuFeature::APIC))
            Log("CPUID says APIC is not unavailable, cannot initialize local apic.", LogSeverity::Fatal);

        //TODO: x2APIC support
        
        baseAddress = CPU::ReadMsr(MSR_APIC_BASE) & ~(0xFFF);
        apicId = ReadReg(LocalApicRegister::Id) >> 24;

        if ((CPU::ReadMsr(MSR_APIC_BASE) & (1 << 11)) == 0)
            Log("IA32_APIC_BASE_MSR bit 11 (global enable) is cleared. Cannot initialize local apic.", LogSeverity::Fatal);

        ACPI::MADT* madt = reinterpret_cast<ACPI::MADT*>(ACPI::AcpiTables::Global()->Find(ACPI::SdtSignature::MADT));
        if (madt == nullptr)
            Log("APIC unable to get madt data, does it exist? Cannot check for existence of 8259 PICs.", LogSeverity::Error);
        else if (IsBsp() && sl::EnumHasFlag(madt->flags, ACPI::MadtFlags::Dual8259sInstalled))
        {
            Log("BSP Local APIC is disabling dual 8259 PICs.", LogSeverity::Verbose);

            //remap pics above native interrupts range, then mask them
            CPU::PortWrite8(0x20, 0x11); //start standard init squence (in cascade mode)
            CPU::PortWrite8(0xA0, 0x11);

            CPU::PortWrite8(0x21, 0x20); //setting pic offsets (in case they mis-trigger by accident)
            CPU::PortWrite8(0xA1, 0x28);

            CPU::PortWrite8(0x21, 0x4); //setting slave yes/no and ids
            CPU::PortWrite8(0xA1, 0x2);

            CPU::PortWrite8(0x21, 0x1); //setting mode (8086)
            CPU::PortWrite8(0xA1, 0x1);

            CPU::PortWrite8(0x21, 0xFF); //masking all interrupts
            CPU::PortWrite8(0xA1, 0xFF);
        }

        //make sure software enable flag is set, and a spurrious vector is set
        WriteReg(LocalApicRegister::SpuriousInterruptVector, INTERRUPT_GSI_SPURIOUS | (1 << 8));
    }

    void LApic::SendEOI()
    {
        WriteReg(LocalApicRegister::EOI, 0);
    }

    bool LApic::IsBsp()
    {
        return (CPU::ReadMsr(MSR_APIC_BASE) & (1 << 8)) != 0;
    }

    void LApic::SetLvtMasked(LocalApicRegister lvtReg, bool masked)
    {
        uint32_t current = ReadReg(lvtReg);
        current = ~(1 << 16); //clear it, and then reset it if needed
        if (masked)
            current |= (1 << 16);
        WriteReg(lvtReg, current);
    }

    bool LApic::GetLvtMasked(LocalApicRegister lvtReg)
    {
        return (ReadReg(lvtReg) & (1 << 16)) != 0;
    }

    void LApic::SetupTimer(size_t millis, uint8_t vector, bool periodic)
    {}

}