#ifndef HEAPTRACK_BUFFEREDSTREAM
#define HEAPTRACK_BUFFEREDSTREAM

#include <mutex>
#include <condition_variable>
#include <thread>

class BufferedStream
{
    static constexpr int MaxMsgs = 1000000;

    enum class MsgType
    {
        Empty,
        Alloc,
        Dealloc,
    };

    struct AllocMsg {
        std::size_t size;
        std::uint32_t index;
        void* ptr;
    };

    struct DeallocMsg {
        void* ptr;
    };

    struct Msg
    {
        Msg() = default;
        Msg(const AllocMsg& msg)
            : m_type(MsgType::Alloc)
        {
            m_data.allocMsg = msg;
        }

        Msg(const DeallocMsg& msg)
            : m_type(MsgType::Dealloc)
        {
            m_data.deallocMsg = msg;
        }

        MsgType m_type = MsgType::Empty;

        union
        {
            AllocMsg allocMsg;
            DeallocMsg deallocMsg;

        } m_data;
    };

  public:
    BufferedStream(FILE* f)
        : m_stream(f)
    {
        m_msgQueue            = new Msg[MaxMsgs];
        m_serializationBuffer = new Msg[MaxMsgs];
        initSerializationThread();
    }

    ~BufferedStream() {
        disableBuffering();
        delete[] m_msgQueue;
        delete[] m_serializationBuffer;
    }

    operator bool() const { return bool(m_stream); }
    bool operator!() const { return !bool(m_stream); }

    bool sendTimestamp(std::size_t timeCnt) {
        return fprintf("c %" PRIx64 "\n", timeCnt) < 0;
    }

    bool sendRSS(std::size_t byteCnt) {
        return fprintf("R %zx\n", byteCnt) < 0;
    }

    bool sendAllocation(std::size_t size, std::uint32_t index, void* ptr) {
        return m_buffering ? (enqueueAllocation(size, index, ptr) < 0)
                           : (fprintf("+ %zx %x %" PRIxPTR "\n", size, index, reinterpret_cast<uintptr_t>(ptr)) < 0);
    }

    bool sendDeallocation(void* ptr) {
        return m_buffering ? (enqueueDeallocation(ptr) < 0)
                           : (fprintf("- %" PRIxPTR "\n", reinterpret_cast<uintptr_t>(ptr)) < 0);
    }

    template<class...T>
    int fprintf(const char* format, T&&... args) {
        flush();
        return ::fprintf(m_stream, format, std::forward<T>(args)...);
    }

    int fputc(int c) {
        flush();
        return ::fputc(c, m_stream);
    }

    int fputs(const char* str) {
        flush();
        return ::fputs(str, m_stream);
    }

    void clear() {
        flush();
        m_stream = nullptr;
    }

    void fclose() {
        flush();
        ::fclose(m_stream);
    }

    // Buffering stuff
    int enqueueAllocation(std::size_t size, std::uint32_t index, void* ptr) {
        bool notify = false;
        {
            std::unique_lock<std::mutex> lock(m_msgQueueMutex);

            while (m_msgIdx == MaxMsgs)
                m_msgQueueCv.wait(lock);

            m_msgQueue[m_msgIdx++] = Msg(AllocMsg{size, index, ptr});

            notify = m_msgIdx == MaxMsgs/2;
        }
        if (notify)
            m_msgQueueCv.notify_one();
        return 1;
    }

    int enqueueDeallocation(void* ptr) {
        bool notify = false;
        {
            std::unique_lock<std::mutex> lock(m_msgQueueMutex);

            while (m_msgIdx == MaxMsgs)
                m_msgQueueCv.wait(lock);

            m_msgQueue[m_msgIdx++] = Msg(DeallocMsg{ptr});
            notify = m_msgIdx == MaxMsgs/2;
        }
        if (notify)
            m_msgQueueCv.notify_one();
        return 1;
    }

    void flush() {
        std::unique_lock<std::mutex> lock1(m_msgQueueMutex);
        std::unique_lock<std::mutex> lock2(m_streamMutex);

        writeMsgs(m_stream, m_serializationBuffer, m_serializationMsgIdx);
        m_serializationMsgIdx = 0;

        writeMsgs(m_stream, m_msgQueue, m_msgIdx);
        m_msgIdx = 0;
    }

    void disableBuffering() {
        flush();
        m_buffering = false;
    }

    void initSerializationThread() {
        m_serializationThread = std::thread([this] () {
            for (;;) {
                {
                    std::unique_lock<std::mutex> lock(m_msgQueueMutex);

                    while (m_msgIdx == 0)
                        m_msgQueueCv.wait(lock);

                    std::swap(m_serializationBuffer, m_msgQueue);
                    std::swap(m_serializationMsgIdx, m_msgIdx);
                }
                m_msgQueueCv.notify_one();

                std::unique_lock<std::mutex> lock(m_streamMutex);
                writeMsgs(m_stream, m_serializationBuffer, m_serializationMsgIdx);
                m_serializationMsgIdx = 0;
            }
        });
    }

    static void writeMsgs(FILE* stream, Msg* msgs, int numMsgs) {
        for (int i = 0; i < numMsgs; ++i) {
            auto& msg = msgs[i];
            switch (msg.m_type) {
                case MsgType::Alloc: {
                    ::fprintf(stream, "+ %zx %x %" PRIxPTR "\n",
                              msg.m_data.allocMsg.size,
                              msg.m_data.allocMsg.index,
                              reinterpret_cast<uintptr_t>(msg.m_data.allocMsg.ptr));
                    break;
                }
                case MsgType::Dealloc: {
                    ::fprintf(stream, "- %" PRIxPTR "\n", reinterpret_cast<uintptr_t>(msg.m_data.deallocMsg.ptr));
                    break;
                }
                case MsgType::Empty: {
                    break;
                }
            }
        }
    }

    FILE* m_stream;
    bool  m_buffering = true;

    std::mutex              m_msgQueueMutex;
    std::condition_variable m_msgQueueCv;
    Msg* m_msgQueue;
    int  m_msgIdx = 0;

    std::mutex              m_streamMutex;
    std::thread             m_serializationThread;
    Msg*                    m_serializationBuffer;
    int                     m_serializationMsgIdx = 0;

};

#endif
