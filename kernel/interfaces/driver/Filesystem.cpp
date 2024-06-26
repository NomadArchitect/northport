#include <filesystem/Filesystem.h>
#include <debug/Log.h>
#include <interfaces/driver/Filesystem.h>
#include <interfaces/Helpers.h>

extern "C"
{
    using namespace Npk::Filesystem;

    DRIVER_API_FUNC
    npk_fs_id npk_fs_lookup(npk_string path)
    {
        VALIDATE_(path.data != nullptr, {});
        VALIDATE_(path.length > 0, {});

        const sl::StringSpan span(path.data, path.length);
        auto found = VfsLookup(span);
        VALIDATE_(found.HasValue(), {});

        return { .device_id = found->driverId, .node_id = found->vnodeId };
    }

    DRIVER_API_FUNC
    npk_string npk_fs_get_path(npk_fs_id id)
    {
        VALIDATE_(id.device_id != 0 && id.node_id != 0, {});

        sl::String path = VfsGetPath({ .driverId = id.device_id, .vnodeId = id.node_id });
        if (path.IsEmpty())
            return { .length = 0, .data = nullptr };

        const npk_string string = { .length = path.Size(), .data = path.DetachBuffer() };
        return string;
    }

    DRIVER_API_FUNC
    bool npk_fs_mount(npk_fs_id mountpoint, npk_handle fs_driver_id, npk_mount_options opts)
    {
        VALIDATE_(mountpoint.device_id != 0 && mountpoint.node_id != 0, false);
        VALIDATE_(fs_driver_id != 0, false);

        const VfsId id { .driverId = mountpoint.device_id, .vnodeId = mountpoint.node_id };
        const MountOptions mountOpts 
        {
            .writable = opts.writable,
            .uncachable = opts.uncachable,
        };

        return VfsMount(id, fs_driver_id, mountOpts);
    }

    DRIVER_API_FUNC
    bool npk_fs_create(REQUIRED npk_fs_id* new_id, npk_fs_id dir, npk_fsnode_type type, npk_string name)
    {
        VALIDATE_(new_id != nullptr, false);
        VALIDATE_(dir.device_id != 0 && dir.node_id != 0, false);
        VALIDATE_(name.length > 0 && name.data != nullptr, false);

        const sl::StringSpan nameSpan(name.data, name.length);
        const VfsId dirId = { .driverId = dir.device_id, .vnodeId = dir.node_id };
        auto created = VfsCreate(dirId, static_cast<NodeType>(type), nameSpan);
        VALIDATE_(created.HasValue(), false);

        new_id->device_id = created->driverId;
        new_id->node_id = created->vnodeId;
        return true;
    }

    DRIVER_API_FUNC
    bool npk_fs_remove(npk_fs_id node)
    {
        ASSERT_UNREACHABLE(); (void)node;
    }

    DRIVER_API_FUNC
    bool npk_fs_find_child(REQUIRED npk_fs_id* found_id, npk_fs_id dir, npk_string name)
    {
        ASSERT_UNREACHABLE(); (void)found_id; (void)dir; (void)name;
    }
}
