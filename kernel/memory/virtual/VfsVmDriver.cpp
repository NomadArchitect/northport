#include <memory/virtual/VfsVmDriver.h>
#include <boot/CommonInit.h>
#include <debug/Log.h>
#include <filesystem/Filesystem.h>
#include <filesystem/FileCache.h>
#include <Memory.h>
#include <Maths.h>

namespace Npk::Memory::Virtual
{
    constexpr size_t FaultMaxMapAhead = 2 * 0x1000;

    void VfsVmDriver::Init(uintptr_t enableFeatures)
    { 
        features.faultHandler = enableFeatures & (uintptr_t)VfsFeature::FaultHandler;

        Log("VmDriver init: vfs, faultHandler=%s", LogLevel::Info, 
            features.faultHandler ? "yes" : "no");
    }

    EventResult VfsVmDriver::HandleFault(VmDriverContext& context, uintptr_t where, VmFaultFlags flags)
    {
        (void)flags;
        using namespace Filesystem;

        auto link = static_cast<VfsVmLink*>(context.range.token);
        ASSERT(link != nullptr, "VFS link is nullptr");
        
        auto node = VfsGetNode(link->node);
        VALIDATE_(node.Valid(), { .goodFault = false });
        auto cache = node->cache;
        VALIDATE_(cache.Valid(), { .goodFault = false });

        const FileCacheInfo fcInfo = GetFileCacheInfo();
        const size_t granuleSize = GetHatLimits().modes[fcInfo.hatMode].granularity;
        const HatFlags hatFlags = ConvertFlags(context.range.flags);

        where = sl::AlignDown(where, granuleSize);
        const size_t mapLength = sl::Min(FaultMaxMapAhead, context.range.Top() - where);
        const uintptr_t mappingOffset = where - context.range.base;

        sl::ScopedLock scopeLock(context.lock);
        auto cachePart = GetFileCache(cache, where - context.range.base + link->fileOffset, !link->isReadonly);
        for (size_t i = 0; i < mapLength; i += granuleSize)
        {
            if (i % fcInfo.unitSize == 0)
            {
                cachePart = GetFileCache(cache, link->fileOffset + mappingOffset + i, !link->isReadonly);
                VALIDATE_(cachePart.Valid(), { .goodFault = false });
            }

            Map(context.map, context.range.base + mappingOffset + i, cachePart->physBase + (i % fcInfo.unitSize), 
                fcInfo.hatMode, hatFlags, false);
            context.stats.fileResidentSize += granuleSize;
        }

        return { .goodFault = true };
    }

    QueryResult VfsVmDriver::Query(size_t length, VmFlags flags, uintptr_t attachArg)
    {
        using namespace Filesystem;
        auto arg = reinterpret_cast<const VmoFileInitArg*>(attachArg);

        //do a tentative check that the file exists, and is actually a file.
        //NOTE: this looks like a toctou bug, but its only an optimization - the authoratative check
        //is performed in Attach().
        auto file = VfsLookup(arg->filepath);
        if (!file.HasValue())
            return { .success = false };
        auto attribs = VfsGetAttribs(*file);
        if (!attribs.HasValue() || attribs->type != NodeType::File)
            return { .success = false };

        QueryResult result;
        result.success = true;
        result.hatMode = GetFileCacheInfo().hatMode;
        result.alignment = GetHatLimits().modes[result.hatMode].granularity;
        result.length = sl::AlignUp(length, result.alignment);
        
        if (flags.Has(VmFlag::Guarded))
            result.length += 2 * result.alignment;

        return result;
    }

    bool VfsVmDriver::ModifyRange(VmDriverContext& context, ModifyRangeArgs args)
    {
        (void)context; (void)args;
        ASSERT_UNREACHABLE();
    }

    SplitResult VfsVmDriver::Split(VmDriverContext& context, size_t offset)
    {
        //TODO:
        ASSERT_UNREACHABLE();
    }

    AttachResult VfsVmDriver::Attach(VmDriverContext& context, const QueryResult& query, uintptr_t attachArg)
    {
        using namespace Filesystem;
        auto arg = reinterpret_cast<const VmoFileInitArg*>(attachArg);

        auto fileId = VfsLookup(arg->filepath);
        if (!fileId.HasValue())
            return { .success = false };
        auto node = VfsGetNode(*fileId);
        if (!node.Valid() || node->type != NodeType::File)
            return { .success = false };
        //TODO: should we call open() here? do we *need* to?

        auto cache = node->cache;
        VfsVmLink* link = new VfsVmLink();
        link->isReadonly = !context.range.flags.Has(VmFlag::Write);
        link->isPrivate = false; //TODO: private mappings
        link->node = *fileId;
        link->fileOffset = arg->offset;

        const size_t granuleSize = GetHatLimits().modes[query.hatMode].granularity;
        const AttachResult result
        {
            .token = link,
            .offset = arg->offset % granuleSize,
            .success = true,
        };
        context.stats.fileWorkingSize += context.range.length - result.offset;

        //we found the file and were able to acquire it's cache, next step depends on our backing strategy.
        //if we're mapping on a page fault then we can exit now. Otherwise map the entire
        //file cache contents.
        if (features.faultHandler && !arg->noDeferBacking)
            return result;

        const HatFlags hatFlags = ConvertFlags(context.range.flags);

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += granuleSize)
        {
            auto handle = GetFileCache(cache, arg->offset + i, !link->isReadonly);
            if (!handle.Valid())
                break;
            Map(context.map, context.range.base + i, handle->physBase, query.hatMode, hatFlags, false);
            context.stats.fileResidentSize += granuleSize;
        }

        return result;
    }

    bool VfsVmDriver::Detach(VmDriverContext& context)
    {
        context.stats.fileWorkingSize -= context.range.length - context.range.offset;
        using namespace Filesystem;
        const FileCacheInfo fcInfo = GetFileCacheInfo();

        VfsVmLink* link = static_cast<VfsVmLink*>(context.range.token);

        const size_t granuleSize = GetHatLimits().modes[fcInfo.hatMode].granularity;
        size_t mode;
        size_t phys;

        sl::ScopedLock scopeLock(context.lock);
        for (size_t i = 0; i < context.range.length; i += granuleSize)
        {
            //TODO: if page has dirty bit set, we'll need to mark the file cache entry as dirtied as well
            if (Unmap(context.map, context.range.base + i, phys, mode, true))
                context.stats.fileResidentSize -= granuleSize;
        }

        delete link;
        return true;
    }
}
