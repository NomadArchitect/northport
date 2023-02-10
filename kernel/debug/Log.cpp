#include <debug/Log.h>
#include <arch/Platform.h>
#include <debug/NanoPrintf.h>
#include <interrupts/Ipi.h>
#include <memory/Pmm.h>
#include <tasking/Clock.h>
#include <tasking/Thread.h>
#include <containers/Queue.h>
#include <Locks.h>
#include <Memory.h>

namespace Npk::Debug
{
    constexpr size_t MaxEarlyLogOuts = 4;
    constexpr size_t CoreLogBufferSize = 4 * PageSize;
    constexpr size_t EarlyBufferSize = 4 * PageSize;
    constexpr size_t MaxEloItemsPrinted = 20;

    constexpr const char* LevelStrs[] = 
    {
        "[ Fatal ] ", "[ Error ] ", "[Warning] ",
        "[ Info  ] ", "[Verbose] ", "[ Debug ] "
    };

    constexpr const char* LevelColourStrs[] =
    {
        "\e[91m", "\e[31m", "\e[93m",
        "\e[97m", "\e[90m", "\e[94m",
    };

    constexpr const char* LevelColourReset = "\e[39m";
    constexpr size_t LevelStrLength = 10;
    constexpr size_t LevelColourLength = 5;

    struct LogBuffer
    {
        char* buffer;
        size_t length;
        size_t head;
    };

    struct LogMessage
    {
        LogBuffer* buff;
        size_t begin;
        size_t length;
    };

    sl::QueueMpSc<LogMessage> msgQueue {};
    char earlyBufferStore[EarlyBufferSize];
    LogBuffer earlyBuffer = 
    { 
        .buffer = earlyBufferStore, 
        .length = EarlyBufferSize, 
        .head = 0
    };

    EarlyLogWrite earlyOuts[MaxEarlyLogOuts];
    size_t eloCount = 0;
    sl::SpinLock eloLock;

    void WriteElos(const LogMessage& msg)
    {
        if (msg.length == 0)
            return;

        size_t runover = 0;
        size_t length = msg.length;
        if (msg.begin + msg.length >= msg.buff->length)
        {
            runover = msg.begin + msg.length - msg.buff->length;
            length -= runover;
        }

        for (size_t i = 0; i < MaxEarlyLogOuts; i++)
        {
            if (earlyOuts[i] != nullptr)
                earlyOuts[i](&msg.buff->buffer[msg.begin], length);
            if (runover > 0)
                earlyOuts[i](msg.buff->buffer, runover);
        }
    }

    void WriteLog(const char* message, size_t messageLen, LogBuffer* buffer)
    {
        using QueueItem = sl::QueueMpSc<LogMessage>::Item;

        //we allocate space for message data and the queue item in the same go
        const size_t allocLength = messageLen + 2 * sizeof(QueueItem);
        const size_t beginWrite = __atomic_fetch_add(&buffer->head, allocLength, __ATOMIC_SEQ_CST) % buffer->length;
        
        //TODO: we should suspend scheduling here until the list append, so we dont leak buffer space.
        uintptr_t itemAddr = 0;
        size_t messageBegin = beginWrite;
        if (beginWrite + allocLength > buffer->length)
        {
            //we're going to wrap around, this is fine for message text but not for the queue struct,
            //as it needs to be contiguous in memory.
            size_t startLength = (beginWrite + allocLength) - buffer->length;
            size_t endLength = allocLength - startLength;

            //place struct before message if there's space
            if (startLength >= 2 * sizeof(QueueItem))
            {
                itemAddr = beginWrite;
                startLength -= 2 * sizeof(QueueItem);
                messageBegin += 2 * sizeof(QueueItem);
            }
            else //otherwise place it after the message
            {
                endLength -= 2 * sizeof(QueueItem);
                itemAddr = endLength;
            }
            
            sl::memcopy(message, 0, buffer->buffer, messageBegin, startLength);
            sl::memcopy(message, startLength, buffer->buffer, 0, endLength);
        }
        else
        {
            //no wraparound, this is very easy
            itemAddr = beginWrite;
            messageBegin += 2 * sizeof(QueueItem);
            sl::memcopy(message, 0, buffer->buffer, messageBegin, messageLen);
        }

        QueueItem* item = new(sl::AlignUp(&buffer->buffer[itemAddr], sizeof(QueueItem))) QueueItem();
        item->data.begin = messageBegin;
        item->data.buff = buffer;
        item->data.length = messageLen;

        //at this point we just need to add this log message to the queue
        msgQueue.Push(item);

        //if we're in the early state (eloCount > 0), try to flush the message queue to the outputs
        if (__atomic_load_n(&eloCount, __ATOMIC_RELAXED) > 0 && eloLock.TryLock())
        {
            for (size_t printed = 0; printed < MaxEloItemsPrinted; printed++)
            {
                QueueItem* item = msgQueue.Pop();
                if (item == nullptr)
                    break;
                
                WriteElos(item->data);
            }

            eloLock.Unlock();
        }
    }
    
    void Log(const char* str, LogLevel level, ...)
    {
        //get length of uptime
        const size_t uptime = Tasking::GetUptime();
        const size_t uptimeLen = npf_snprintf(nullptr, 0, "%lu.%03lu", uptime / 1000, uptime % 1000) + 1;

        //get length of header (processor id + thread id)
        const size_t processorId = CoreLocalAvailable() ? CoreLocal().id : 0;
        size_t threadId = 0;
        if (CoreLocalAvailable() && CoreLocal().schedThread != nullptr)
            threadId = static_cast<Tasking::Thread*>(CoreLocal().schedThread)->Id();
        const size_t headerLen = npf_snprintf(nullptr, 0, "p%lut%lu", processorId, threadId) + 1;

        //get formatted message length
        va_list argsList;
        va_start(argsList, level);
        const size_t strLen = npf_vsnprintf(nullptr, 0, str, argsList) + 1;
        va_end(argsList);

        //create buffer on stack, +2 to allow space for "\r\n".
        const size_t bufferLen = uptimeLen + LevelStrLength + headerLen + 
            (LevelColourLength * 2) + strLen + 2; //TODO: limit buffer length
        char buffer[bufferLen];
        size_t bufferStart = 0;

        //print uptime
        npf_snprintf(buffer, uptimeLen, "%lu.%03lu", uptime / 1000, uptime % 1000);
        bufferStart += uptimeLen;
        buffer[bufferStart - 1] = ' ';

        //print processor + thread ids
        npf_snprintf(&buffer[bufferStart], headerLen, "p%lut%lu", processorId, threadId);
        bufferStart += headerLen;
        buffer[bufferStart - 1] = ' ';

        //add level string (+ colouring escape codes)
        sl::memcopy(LevelColourStrs[(size_t)level], 0, buffer, bufferStart, LevelColourLength);
        bufferStart += LevelColourLength;
        sl::memcopy(LevelStrs[(size_t)level], 0, buffer, bufferStart, LevelStrLength);
        bufferStart += LevelStrLength;
        sl::memcopy(LevelColourReset, 0, buffer, bufferStart, LevelColourLength);
        bufferStart += LevelColourLength;

        //format and print log message
        va_start(argsList, level);
        npf_vsnprintf(&buffer[bufferStart], strLen, str, argsList);
        va_end(argsList);
        bufferStart += strLen;
        
        buffer[bufferStart++] = '\r';
        buffer[bufferStart++] = '\n';

        if (CoreLocalAvailable() && CoreLocal().logBuffers != nullptr)
            WriteLog(buffer, bufferLen, reinterpret_cast<LogBuffer*>(CoreLocal().logBuffers));
        else
            WriteLog(buffer, bufferLen, &earlyBuffer);

        if (level == LogLevel::Fatal)
            Panic(nullptr, buffer);
    }

    void InitCoreLogBuffers()
    {
        LogBuffer* buffer = new LogBuffer;
        buffer->head = 0;
        buffer->length = CoreLogBufferSize;
        buffer->buffer = reinterpret_cast<char*>(PMM::Global().Alloc(CoreLogBufferSize / PageSize) + hhdmBase);

        CoreLocal().logBuffers = buffer;
    }

    void AddEarlyLogOutput(EarlyLogWrite callback)
    {
        sl::ScopedLock scopeLock(eloLock);
        if (__atomic_load_n(&eloCount, __ATOMIC_RELAXED) == 0) //deferred init
        {
            for (size_t i = 0; i < MaxEarlyLogOuts; i++)
                earlyOuts[i] = nullptr;
        }

        for (size_t i = 0; i < MaxEarlyLogOuts; i++)
        {
            if (earlyOuts[i] != nullptr)
                continue;

            __atomic_add_fetch(&eloCount, 1,__ATOMIC_RELAXED);
            earlyOuts[i] = callback;
            return;
        }
    }

    void AttachLogDriver(size_t deviceId)
    {
        (void)deviceId;
        ASSERT_UNREACHABLE();
    }

    void DetachLogDriver(size_t deviceId)
    {
        (void)deviceId;
        ASSERT_UNREACHABLE();
    }

    void LogWriterServiceMain(void*)
    {
        //TODO: if ELOs are active, flush those, otherwise update each driver with the new
        //read head.
    }
}

extern "C"
{
    static_assert(Npk::Debug::EarlyBufferSize > 0x2000, "Early buffer is too small to used as a panic stack");
    void* panicStack = &Npk::Debug::earlyBufferStore[Npk::Debug::EarlyBufferSize];

    void PanicWrite(const char* message)
    {
        using namespace Npk::Debug;

        const size_t messageLength = sl::memfirst(message, 0, 0);
        for (size_t i = 0; i < MaxEarlyLogOuts; i++)
        {
            if (earlyOuts[i] != nullptr)
                earlyOuts[i](message, messageLength);
        }
    }

    void PanicLanding(Npk::TrapFrame* exceptionFrame, const char* reason)
    {
        using namespace Npk;
        using namespace Npk::Debug;

        Interrupts::BroadcastPanicIpi();
        eloLock.TryLock();

        PanicWrite("\r\n\r\n");
        PanicWrite("Kernel Panic :(\r\n");
        PanicWrite("System has halted indefinitely, manual reset required.\r\n");

        Halt();
    }
}
