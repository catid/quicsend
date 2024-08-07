#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <string>
#include <functional>
#include <sstream>

#include <boost/asio.hpp>


//------------------------------------------------------------------------------
// Logger

class Logger {
public:
    enum LogLevel { DEBUG, INFO, WARN, ERROR };

    static Logger& getInstance();

    Logger();
    ~Logger();

    void SetLogLevel(LogLevel level);
    void SetCallback(std::function<void(LogLevel, const std::string&)> callback);

    class LogStream {
    public:
        LogStream(Logger& logger, LogLevel level, bool log_enabled = true)
            : logger_(logger)
            , level_(level)
            , log_enabled_(log_enabled)
        {
        }
        LogStream(const LogStream&& other)
            : logger_(other.logger_)
            , level_(other.level_)
        {
        }
        ~LogStream() {
            if (log_enabled_) {
                logger_.Log(level_, std::move(ss_));
            }
        }

        template<typename T>
        LogStream& operator<<(const T& value) {
            if (log_enabled_) {
                ss_ << value;
            }
            return *this;
        }

    private:
        Logger& logger_;
        LogLevel level_;
        bool log_enabled_ = false;

        std::ostringstream ss_;
    };

    LogStream Debug() { return LogStream(*this, LogLevel::DEBUG, CurrentLogLevel <= LogLevel::DEBUG); }
    LogStream Info() { return LogStream(*this, LogLevel::INFO, CurrentLogLevel <= LogLevel::INFO); }
    LogStream Warn() { return LogStream(*this, LogLevel::WARN, CurrentLogLevel <= LogLevel::WARN); }
    LogStream Error() { return LogStream(*this, LogLevel::ERROR, CurrentLogLevel <= LogLevel::ERROR); }

    void Terminate();

private:
    static std::unique_ptr<Logger> Instance;
    static std::once_flag InitInstanceFlag;

    struct LogEntry {
        LogLevel Level;
        std::string Message;
    };

    std::vector<LogEntry> LogQueue;
    std::mutex LogQueueMutex;
    std::condition_variable LogQueueCV;

    std::thread LoggerThread;
    std::atomic<LogLevel> CurrentLogLevel = ATOMIC_VAR_INIT(LogLevel::INFO);
    std::function<void(LogLevel, const std::string&)> Callback;

    std::atomic<bool> Terminated = ATOMIC_VAR_INIT(false);

    std::vector<LogEntry> LogsToProcess;

    void Log(LogLevel level, std::ostringstream&& message);
    void RunLogger();
    void ProcessLogQueue();
};

// Macros for logging
#define LOG_DEBUG() Logger::getInstance().Debug()
#define LOG_INFO() Logger::getInstance().Info()
#define LOG_WARN() Logger::getInstance().Warn()
#define LOG_ERROR() Logger::getInstance().Error()
#define LOG_TERMINATE() Logger::getInstance().Terminate();

void EnableQuicheDebugLogging();


//------------------------------------------------------------------------------
// Tools

void JoinThread(std::shared_ptr<std::thread> th);

int64_t GetNsec();

struct CallbackScope {
    CallbackScope(std::function<void()> func) : func(func) {}
    ~CallbackScope() { func(); }
    std::function<void()> func;
};

std::vector<uint8_t> LoadPEMCertAsDER(const std::string& pem_file_path);

std::string EndpointToString(const boost::asio::ip::udp::endpoint& endpoint);

std::string DumpHex(const void* data, size_t size = 32, const char* label = nullptr);


//------------------------------------------------------------------------------
// Serialization

inline void write_uint16_le(void* buffer, uint16_t value) {
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    ptr[0] = static_cast<uint8_t>(value);
    ptr[1] = static_cast<uint8_t>(value >> 8);
}

inline uint16_t read_uint16_le(const void* buffer) {
    const uint8_t* ptr = static_cast<const uint8_t*>(buffer);
    return static_cast<uint16_t>(ptr[0]) |
           (static_cast<uint16_t>(ptr[1]) << 8);
}

inline void write_uint32_le(void* buffer, uint32_t value) {
    uint8_t* ptr = static_cast<uint8_t*>(buffer);
    ptr[0] = static_cast<uint8_t>(value);
    ptr[1] = static_cast<uint8_t>(value >> 8);
    ptr[2] = static_cast<uint8_t>(value >> 16);
    ptr[3] = static_cast<uint8_t>(value >> 24);
}

inline uint32_t read_uint32_le(const void* buffer) {
    const uint8_t* ptr = static_cast<const uint8_t*>(buffer);
    return static_cast<uint32_t>(ptr[0]) |
           (static_cast<uint32_t>(ptr[1]) << 8) |
           (static_cast<uint32_t>(ptr[2]) << 16) |
           (static_cast<uint32_t>(ptr[3]) << 24);
}
