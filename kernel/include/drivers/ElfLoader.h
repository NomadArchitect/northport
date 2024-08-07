#pragma once

#include <debug/Symbols.h>
#include <String.h>
#include <Handle.h>
#include <memory/VmObject.h>
#include <formats/Elf.h>
#include <Atomic.h>

namespace Npk::Drivers
{
    struct DynamicElfInfo
    {
        const char* strTable;
        const sl::Elf_Sym* symTable;

        const void* pltRelocs;
        size_t pltRelocsSize;
        bool pltUsesRela;
        const sl::Elf_Rela* relaEntries;
        size_t relaCount;
        const sl::Elf_Rel* relEntries;
        size_t relCount;
    };

    struct LoadedElf
    {
        sl::Atomic<size_t> references;
        sl::Vector<VmObject> segments;
        sl::Handle<Debug::SymbolRepo> symbolRepo;
        uintptr_t loadBase;
        uintptr_t entryAddr;
    };

    struct LoadingDriverInfo
    {
        sl::StringSpan name;
        const void* manifest;
    };

    void ScanForModules(sl::StringSpan dirpath);
    bool ScanForDrivers(sl::StringSpan filepath);

    sl::Handle<LoadedElf> LoadElf(VMM* vmm, sl::StringSpan filepath, LoadingDriverInfo* driverInfo = nullptr);
}
