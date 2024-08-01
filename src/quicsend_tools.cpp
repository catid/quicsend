#include <quicsend_tools.hpp>
#include <quiche.h>

#include <pthread.h>

#include <cstring>
#include <chrono>
#include <iostream>

#include <openssl/x509.h>
#include <openssl/pem.h>


//------------------------------------------------------------------------------
// Logger

// Initialize static members
std::unique_ptr<Logger> Logger::Instance;
std::once_flag Logger::InitInstanceFlag;

Logger& Logger::getInstance() {
    std::call_once(InitInstanceFlag, []() {
        Instance.reset(new Logger);
    });
    return *Instance;
}

Logger::Logger() {
    LoggerThread = std::thread(&Logger::RunLogger, this);
}

Logger::~Logger() {
    Terminate();
    if (LoggerThread.joinable()) {
        LoggerThread.join();
    }

    // Process any remaining logs
    LogsToProcess.swap(LogQueue);
    ProcessLogQueue();
}

void Logger::SetLogLevel(LogLevel level) {
    CurrentLogLevel = level;
}

void Logger::SetCallback(std::function<void(LogLevel, const std::string&)> callback) {
    Callback = callback;
}

void Logger::Log(LogLevel level, std::ostringstream&& message) {
    std::lock_guard<std::mutex> lock(LogQueueMutex);
    LogQueue.push_back({level, message.str()});
    LogQueueCV.notify_one();
}

void Logger::Terminate() {
    Terminated = true;
    LogQueueCV.notify_one();
}

void Logger::RunLogger() {
    while (!Terminated) {
        {
            std::unique_lock<std::mutex> lock(LogQueueMutex);
            LogQueueCV.wait(lock, [this] { return !LogQueue.empty() || Terminated; });
            LogsToProcess.swap(LogQueue);
        }

        ProcessLogQueue();

        if (Terminated) {
            break;
        }
    }
}

void Logger::ProcessLogQueue() {
    if (LogsToProcess.empty()) {
        return;
    }

    for (const auto& entry : LogsToProcess) {
        if (entry.Level >= CurrentLogLevel) {
            if (Callback) {
                Callback(entry.Level, entry.Message);
            } else {
                switch (entry.Level) {
                    case LogLevel::DEBUG:
                        std::cout << "[DEBUG] " << entry.Message << std::endl;
                        break;
                    case LogLevel::INFO:
                        std::cout << "[INFO] " << entry.Message << std::endl;
                        break;
                    case LogLevel::WARN:
                        std::cerr << "[WARN] " << entry.Message << std::endl;
                        break;
                    case LogLevel::ERROR:
                        std::cerr << "[ERROR] " << entry.Message << std::endl;
                        break;
                }
            }
        }
    }

    LogsToProcess.clear();
}

static void debug_log(const char* line, void* argp) {
    LOG_INFO() << "QuicheLog: " << line;
}

void EnableQuicheDebugLogging()
{
    quiche_enable_debug_logging(debug_log, nullptr);
}


//------------------------------------------------------------------------------
// Tools

void JoinThread(std::shared_ptr<std::thread> th)
{
    if (!th || !th->joinable()) {
        return;
    }

    try {
        th->join();
    } catch (...) {
        return;
    }
}

int64_t GetNsec()
{
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    return static_cast<int64_t>(tp.tv_sec) * 1000000000LL + tp.tv_nsec;
}

std::vector<uint8_t> LoadPEMCertAsDER(const std::string& pem_file_path) {
    std::vector<uint8_t> der_data;

    FILE* pem_file = fopen(pem_file_path.c_str(), "r");
    if (!pem_file) {
        throw std::runtime_error("Failed to open PEM file");
    }

    X509* cert = PEM_read_X509(pem_file, nullptr, nullptr, nullptr);
    fclose(pem_file);

    if (!cert) {
        throw std::runtime_error("Failed to read PEM certificate");
    }
    CallbackScope cert_scope([cert]() { X509_free(cert); });

    int der_length = i2d_X509(cert, nullptr);
    if (der_length < 0) {
        throw std::runtime_error("Failed to determine DER length");
    }

    der_data.resize(der_length);
    uint8_t* der_ptr = der_data.data();
    if (i2d_X509(cert, &der_ptr) != der_length) {
        throw std::runtime_error("Failed to convert PEM to DER");
    }

    return der_data;
}


//------------------------------------------------------------------------------
// QuicSendMailbox

void QuicSendMailbox::Poll(MailboxCallback callback)
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !events_.empty(); });
    if (events_.empty()) {
        return;
    }

    auto event = events_.back();
    events_.pop_back();

    lock.unlock();

    callback(event->Id, event->Type, event->Buffer->data(), event->Buffer->size());
}

void QuicSendMailbox::Post(
    int64_t stream_id,
    RequestDataType type,
    std::shared_ptr<std::vector<uint8_t>> buffer)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto event = std::make_shared<QueuedEvent>();
    event->Id = stream_id;
    event->Type = type;
    event->Buffer = buffer;

    events_.push_back(event);
    cv_.notify_one();
}
