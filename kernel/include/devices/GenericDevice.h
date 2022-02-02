#pragma once

#include <stddef.h>
#include <Optional.h>

namespace Kernel::Devices
{
    enum class DeviceState : size_t
    {
        Unknown,
        Initializing,
        Ready,
        Deinitializing,
        Shutdown,
        NonAvailable,
        Error,
    };

    enum class DeviceType : size_t
    {
        Unknown,
        
        GraphicsAdaptor,
        GraphicsFramebuffer,

        EnumCount,
    };

    class DeviceManager;

    class GenericDevice
    {
    friend DeviceManager;
    private:
        size_t deviceId;

    protected:
        DeviceState state = DeviceState::Unknown;
        DeviceType type = DeviceType::Unknown;
        char lock;

        virtual void Init() = 0;
        virtual void Deinit() = 0;

    public:
        virtual ~GenericDevice() = default;

        inline DeviceState GetState() const
        { return state; }

        inline size_t GetId() const
        { return deviceId; }

        virtual void Reset() = 0;
        virtual sl::Opt<void*> GetDriverInstance() = 0;

    };
}
