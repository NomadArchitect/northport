#pragma once

#include <memory/virtual/VmDriver.h>

namespace Npk::Memory::Virtual
{
    sl::Span<VmRange> GetKernelRanges();

    class KernelVmDriver : public VmDriver
    {
    public:
        void Init(uintptr_t enableFeatures) override;

        EventResult HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags) override;
        bool ModifyRange(VmDriverContext& context, ModifyRangeArgs args) override;
        QueryResult Query(size_t length, VmFlags flags, uintptr_t attachArg) override;
        SplitResult Split(VmDriverContext& context, size_t offset) override;
        AttachResult Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg) override;
        bool Detach(VmDriverContext& context) override;
    };
}
