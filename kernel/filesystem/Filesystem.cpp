#include <filesystem/Filesystem.h>
#include <boot/LimineTags.h>
#include <debug/Log.h>
#include <filesystem/TempFs.h>
#include <containers/Vector.h>
#include <Memory.h>
#include <formats/Tar.h>
#include <formats/Url.h>
#include <UnitConverter.h>

namespace Npk::Filesystem
{
    sl::Vector<VfsDriver*> filesystems;

    void LoadInitdiskFile(Node* root, const sl::TarHeader* header)
    {
        VALIDATE(root != nullptr,, "Initdisk root is nullptr");
        VALIDATE(header != nullptr,, "Tar entry is nullptr");

        auto conv = sl::ConvertUnits(header->SizeBytes());
        Log("Loading initdisk file: %s (%lu.%lu%sB)", LogLevel::Verbose, 
            header->Filename().Begin(), conv.major, conv.minor, conv.prefix);
        
        const sl::Url filepath = sl::Url::Parse(header->Filename());
        sl::StringSpan segment {};
        sl::Handle<Node> parent = root;

        //create any parent directories needed
        while (true)
        {
            segment = filepath.GetNextSeg(segment);
            if (filepath.GetNextSeg(segment).Empty())
                break; //last segment is actually the filename, handle that outside the loop

            sl::Handle<Node> dir = parent->FindChild(segment, KernelFsCtxt);
            if (!dir.Valid())
            {
                NodeProps props { .name = segment, .created = sl::TimePoint::Now() };
                dir = root->Create(NodeType::Directory, props, KernelFsCtxt);

                if (!dir.Valid())
                {
                    Log("Initdisk file loading failed: %s", LogLevel::Error, header->Filename().Begin());
                    return;
                }
            }
            parent = dir;
        }

        //load the file itself
        NodeProps props 
        { 
            .name = segment, 
            .created = sl::TimePoint::Now(), 
            .size = header->SizeBytes() 
        };
        sl::Handle<Node> file = parent->Create(NodeType::File, props, KernelFsCtxt);
        if (!file.Valid())
        {
            Log("Initdisk file loading failed: %s", LogLevel::Error, header->Filename().Begin());
            return;
        }

        RwPacket writePacket { .write = true, .length = header->SizeBytes() };
        //NOTE: this is okay, since we're only reading from the buffer.
        writePacket.buffer = const_cast<void*>(header->Data()); //TODO: memory map and write directory to the filecache
        if (size_t written = file->ReadWrite(writePacket, KernelFsCtxt) != header->SizeBytes())
        {

            Log("Initdisk file wrote %lu (out of %lu) bytes.", LogLevel::Error, 
                written, header->SizeBytes());
        }
    }

    void PrintVfsNode(sl::Handle<Node>& node, size_t level)
    {
        NodeProps props {};
        node->GetProps(props, KernelFsCtxt);
        Log("%*c%s%s (%lu bytes)", LogLevel::Debug, (int)level * 2, ' ', 
            props.name.C_Str(), node->type == NodeType::Directory ? "/" : "", props.size);

        if (node->type != NodeType::Directory)
            return;

        for (size_t i = 0; true; i++)
        {
            auto child = node->GetChild(i, KernelFsCtxt);
            if (!child.Valid())
                break;

            PrintVfsNode(child, level + 1);
        }
    }

    void LoadInitdisk(VfsDriver* mountpoint, void* base, size_t length)
    {
        VALIDATE(mountpoint != nullptr,, "Bad mountpoint");
        VALIDATE(base != nullptr,, "Bad initdisk base address");
        
        size_t filesLoaded = 0;
        const sl::TarHeader* scan = static_cast<sl::TarHeader*>(base);
        while ((uintptr_t)scan < (uintptr_t)base + length)
        {
            if (scan->IsZero() && scan->Next()->IsZero())
                break; //two zero sectors indicate the end of the archive

            if (scan->Type() != sl::TarEntryType::File)
            {
                scan = scan->Next();
                continue;
            }

            LoadInitdiskFile(mountpoint->Root(), scan);
            filesLoaded++;
            scan = scan->Next();
        }

        Log("Initdisk loaded %lu files.", LogLevel::Info, filesLoaded);
    }

    void TryFindInitdisk()
    {
        if (Boot::modulesRequest.response == nullptr)
            return;

        const auto* resp = Boot::modulesRequest.response;
        for (size_t i = 0; i < resp->module_count; i++)
        {
            const auto* module = resp->modules[i];
            const size_t nameLen = sl::memfirst(module->cmdline, 0, 0);
            if (sl::memcmp(module->cmdline, "northport-initdisk", nameLen) != 0)
                continue;
            
            Log("Loading module \"%s\" (@ 0x%lx) as initdisk.", LogLevel::Info, 
                module->cmdline, (uintptr_t)module->address);
            
            //create a mountpoint for the initdisk under `/initdisk/`
            NodeProps mountProps { .name = "initdisk" };
            auto mountpoint = RootFs()->Root()->Create(NodeType::Directory, mountProps, KernelFsCtxt);
            ASSERT(mountpoint, "Failed to create initdisk mountpoint");

            //create filesystem, load module as initdisk
            TempFs* initdiskFs = new TempFs();
            LoadInitdisk(initdiskFs, module->address, module->size);

            //mount it!
            filesystems.EmplaceBack(initdiskFs);
            MountArgs mountArgs {};
            const bool mounted = initdiskFs->Mount(*mountpoint, mountArgs);

            if (!mounted)
            {
                Log("Mounting initdisk failed, freeing resources.", LogLevel::Debug);
                initdiskFs->Unmount(); //try to unmount, since we dont know how far the mounting process got
                filesystems.PopBack();
                delete initdiskFs;
            }

            //only load the first module with this name for now, since we dont support
            //multiple init ramdisks.
            break;
        }
    }
    
    void InitVfs()
    {
        const bool noRootFs = true; //TODO: set this if no config data for mounting the root fs is found.
        if (noRootFs)
        {
            filesystems.EmplaceBack(new TempFs());
            Log("VFS initialized: No root filesystem found, using tempfs.", LogLevel::Info);
        }
        else
            ASSERT_UNREACHABLE()

        TryFindInitdisk();
        sl::Handle<Node> rootHandle = RootFs()->Root();
        PrintVfsNode(rootHandle, 0);
    }

    VfsDriver* RootFs()
    {
        return filesystems.Size() > 0 ? filesystems[0] : nullptr;
    }

    sl::Handle<Node> VfsLookup(sl::StringSpan path, const FsContext& context)
    {
        VfsDriver* root = RootFs();
        if (root == nullptr)
            return {};

        if (path[0] == '/')
            path = path.Subspan(1, path.Size() - 1);
        if (path.Empty())
            return root->Root();
        
        return root->Resolve(path, context);
    }
}
