#include <devices/IoApic.h>
#include <acpi/AcpiTables.h>
#include <Algorithms.h>
#include <Memory.h>
#include <Log.h>

namespace Kernel::Devices
{
    void IoApicRedirectEntry::Set(uint8_t destApicId, uint8_t vector, IoApicTriggerMode triggerMode, IoApicPinPolarity pinPolarity)
    {
        //setting bit 16 as well, to mask by default.
        raw = ((uint64_t)destApicId << 56) | (1 << 16) | (((uint64_t)triggerMode & 0b1) << 15) | (((uint64_t)pinPolarity & 0b1) << 13) | vector;
    }

    void IoApicRedirectEntry::SetMask(bool masked)
    {
        raw &= ~(1 << 16);
        if (masked)
            raw |= (1 << 16);
    }

    bool IoApicRedirectEntry::IsMasked()
    {
        return (raw & (1 << 16)) != 0;
    }

    void IoApic::WriteReg(IoApicRegister reg, uint32_t value)
    {
        sl::MemWrite<uint32_t>(baseAddress.raw + IoApicRegisterSelectOffset, (uint32_t)reg);
        sl::MemWrite<uint32_t>(baseAddress.raw + IoApicRegisterWindowOffset, value);
    }

    uint32_t IoApic::ReadReg(IoApicRegister reg)
    {
        sl::MemWrite<uint32_t>(baseAddress.raw + IoApicRegisterSelectOffset, (uint32_t)reg);
        return sl::MemRead<uint32_t>(baseAddress.raw + IoApicRegisterWindowOffset);
    }


    void IoApic::Init(uint64_t address, uint8_t id, uint8_t gsiBase)
    {
        baseAddress = address;
        apicId = id;
        this->gsiBase = gsiBase;
        maxRedirects = (ReadReg(IoApicRegister::Version) >> 16) & 0xFF;
        maxRedirects++; //stored value is n - 1

        Logf("New IO APIC initialized: base=0x%x, id=%u, gsiBase=%u, maxRedirects=%u", LogSeverity::Info, baseAddress.raw, apicId, gsiBase, maxRedirects);
    }

    //these have to be pointers so the compiler dosnt generate global ctors
    sl::Vector<IoApic>* ioApics;
    sl::Vector<IoApicEntryModifier>* modifiers;
    sl::Vector<IoApicEntryModifier>* nmis;
    void IoApic::InitAll()
    {
        ioApics = new sl::Vector<IoApic>();
        modifiers = new sl::Vector<IoApicEntryModifier>();
        nmis = new sl::Vector<IoApicEntryModifier>();
        
        using namespace ACPI;
        MADT* madt = static_cast<MADT*>(AcpiTables::Global()->Find(SdtSignature::MADT));
        if (!madt)
        {
            Log("MADT not found, cannot initialize IO APICs.", LogSeverity::Error);
            return;
        }

        size_t madtEnd = (size_t)madt + madt->length;
        sl::NativePtr scan = madt->controllerEntries;
        while (scan.raw < madtEnd)
        {
            switch (scan.As<MadtEntry>()->type)
            {
            case MadtEntryType::IoApic:
                {
                    MadtEntries::IoApicEntry* realEntry = scan.As<MadtEntries::IoApicEntry>();
                    ioApics->EmplaceBack();
                    ioApics->Back().Init(realEntry->mmioAddress, realEntry->apicId, realEntry->gsiBase);
                    break;
                }
            case MadtEntryType::InterruptSourceOverride:
                {
                    MadtEntries::InterruptSourceOverrideEntry* realEntry = scan.As<MadtEntries::InterruptSourceOverrideEntry>();
                    modifiers->EmplaceBack();
                    modifiers->Back().irqNum = realEntry->source;
                    modifiers->Back().gsiNum = realEntry->mappedSource;

                    if (((uint16_t)realEntry->flags & 0b10) != 0)
                        modifiers->Back().polarity = IoApicPinPolarity::ActiveLow;
                    else if (((uint16_t)realEntry->flags & 0b01) != 0)
                        modifiers->Back().polarity = IoApicPinPolarity::ActiveHigh;
                    else
                        modifiers->Back().polarity = IoApicPinPolarity::Default;

                    if (((uint16_t)realEntry->flags & 0b1000) != 0)
                        modifiers->Back().triggerMode = IoApicTriggerMode::Level;
                    else if (((uint16_t)realEntry->flags & 0b0100) != 0)
                        modifiers->Back().triggerMode = IoApicTriggerMode::Edge;
                    else
                        modifiers->Back().triggerMode = IoApicTriggerMode::Default;
                    
                    break;
                }
            case MadtEntryType::NmiSource:
                {
                    MadtEntries::NmiSourceEntry* realEntry = scan.As<MadtEntries::NmiSourceEntry>();
                    nmis->EmplaceBack();
                    nmis->Back().irqNum = 0;
                    nmis->Back().gsiNum = realEntry->gsiNumber;

                    if (((uint16_t)realEntry->flags & 0b10) != 0)
                        nmis->Back().polarity = IoApicPinPolarity::ActiveLow;
                    else if (((uint16_t)realEntry->flags & 0b01) != 0)
                        nmis->Back().polarity = IoApicPinPolarity::ActiveHigh;
                    else
                        nmis->Back().polarity = IoApicPinPolarity::Default;

                    if (((uint16_t)realEntry->flags & 0b1000) != 0)
                        nmis->Back().triggerMode = IoApicTriggerMode::Level;
                    else if (((uint16_t)realEntry->flags & 0b0100) != 0)
                        nmis->Back().triggerMode = IoApicTriggerMode::Edge;
                    else
                        nmis->Back().triggerMode = IoApicTriggerMode::Default;

                    break;
                }

            default:
                break;
            }

            scan.raw += scan.As<MadtEntry>()->length;
        }

        //apply the nmis now
        for (size_t i = 0; i < nmis->Size(); i++)
            Global(nmis->At(i).gsiNum)->WriteRedirect(nmis->At(i).gsiNum, IoApicRedirectEntry{});

        Logf("IO APIC parsing finished, nmis=%u, sourceOverrides=%u", LogSeverity::Verbose, nmis->Size(), modifiers->Size());
    }

    IoApic* IoApic::Global(size_t ownsGsi)
    {
        //NOTE: this assumes vector is ordered, otherwise this will likely return junk
        IoApic* last = ioApics->Begin();
        while (last + 1 < ioApics->End())
        {
            IoApic* next = last + 1;
            if (next->gsiBase > ownsGsi)
                break;
            last++;
        }
        return last;
    }

    IoApicEntryModifier IoApic::TranslateToGsi(uint8_t irqNumber)
    {
        IoApicEntryModifier* mod = sl::FindIf(modifiers->Begin(), modifiers->End(), [&] (IoApicEntryModifier* mod) { return mod->irqNum == irqNumber; });
        if (mod == modifiers->End())
            return 
            { 
                .irqNum = irqNumber, 
                .gsiNum = irqNumber, 
                .polarity = IoApicPinPolarity::Default, 
                .triggerMode = IoApicTriggerMode::Default 
            };
        return *mod;
    }

    void IoApic::WriteRedirect(uint8_t destApicId, IoApicEntryModifier entryMod)
    {
        IoApicRedirectEntry entry;
        entry.Set(destApicId, entryMod.gsiNum, entryMod.triggerMode, entryMod.polarity);
        WriteRedirect(entryMod.gsiNum, entry);
    }

    void IoApic::WriteRedirect(uint8_t pinNum, IoApicRedirectEntry entry)
    {
        if (pinNum >= maxRedirects)
        {
            Logf("Attempted to write IO APIC redirect %u - too high.", LogSeverity::Error, pinNum);
            return;
        }

        //check we dont overwrite any nmis
        for (size_t i = 0; i < nmis->Size(); i++)
        {
            if (nmis->At(i).gsiNum == pinNum)
            {
                Logf("Attempted to overwrite NMI IO APIC redirect %u.", LogSeverity::Error, pinNum);
                return;
            }
        }

        IoApicRegister regLow = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum);
        IoApicRegister regHigh = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum + 1);
        WriteReg(regLow, entry.raw);
        WriteReg(regHigh, entry.raw >> 32);
    }

    IoApicRedirectEntry IoApic::ReadRedirect(uint8_t pinNum)
    {
        if (pinNum >= maxRedirects)
        {
            Logf("Attempted to read IO APIC redirect %u - too high.", LogSeverity::Error, pinNum);
            return {};
        }

        IoApicRegister regLow = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum);
        IoApicRegister regHigh = (IoApicRegister)((uint64_t)IoApicRegister::Redirect0 + pinNum + 1);
        
        IoApicRedirectEntry entry;
        entry.raw = ReadReg(regLow) | ((uint64_t)ReadReg(regHigh) << 32);

        return entry;
    }
}