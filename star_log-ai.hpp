// ============================================================================
//  glog_lite.hpp —— 仿 Google glog 的 header-only C++ 日志库
//
//  用法：
//      #include "glog_lite.hpp"
//      glog::SetMinLogLevel(glog::DEBUG);
//      LOG(INFO) << "hello " << 42;
//      LOG_IF(WARNING, x > 100) << "too big";
//      DLOG(INFO) << "only in debug build";
//      CHECK(ptr != nullptr) << "ptr must not be null";
//
//  输出格式（与 glog 一致）：
//      D20240101 12:00:00.123456 12345 main.cc:42] message
// ============================================================================
#ifndef GLOG_LITE_LOGGING_HPP
#define GLOG_LITE_LOGGING_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#if defined(ERROR)
#undef ERROR          // windows.h 里的 ERROR 宏会污染 LOG(ERROR)
#endif
#else
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace glog {

// ============================================================================
//  日志级别
// ============================================================================
enum LogSeverity {
    DEBUG   = 0,
    INFO    = 1,
    WARNING = 2,
    ERROR   = 3,
    FATAL   = 4
};

inline char SeverityChar(int s) {
    static const char kChars[] = {'D', 'I', 'W', 'E', 'F'};
    return (s >= 0 && s <= 4) ? kChars[s] : '?';
}

inline const char* SeverityName(int s) {
    switch (s) {
        case DEBUG:   return "DEBUG";
        case INFO:    return "INFO";
        case WARNING: return "WARNING";
        case ERROR:   return "ERROR";
        case FATAL:   return "FATAL";
        default:      return "UNKNOWN";
    }
}

inline const char* SeverityColor(int s) {
    switch (s) {
        case DEBUG:   return "\033[36m";    // 青色
        case INFO:    return "\033[32m";    // 绿色
        case WARNING: return "\033[33m";    // 黄色
        case ERROR:   return "\033[31m";    // 红色
        case FATAL:   return "\033[1;31m";  // 高亮红
        default:      return "\033[0m";
    }
}

// ============================================================================
//  内部工具
// ============================================================================
namespace internal {

// 用于丢弃日志的 streambuf（不做任何事，避免内存增长）
class NullStreamBuf : public std::streambuf {
protected:
    int overflow(int c) override { return c; }
};

inline std::ostream& NullOstream() {
    static NullStreamBuf buf;
    static std::ostream os(&buf);
    return os;
}

inline bool StderrIsTty() {
#if defined(_WIN32)
    return _isatty(_fileno(stderr)) != 0;
#else
    return ::isatty(::fileno(stderr)) != 0;
#endif
}

inline uint64_t ThreadId() {
#if defined(_WIN32)
    return static_cast<uint64_t>(::GetCurrentThreadId());
#elif defined(__linux__)
    return static_cast<uint64_t>(::syscall(SYS_gettid));
#else
    std::hash<std::thread::id> h;
    return static_cast<uint64_t>(h(std::this_thread::get_id()) % 1000000);
#endif
}

inline std::string Basename(const char* file) {
    if (!file || !*file) return "(unknown)";
    const char* p1 = std::strrchr(file, '/');
    const char* p2 = std::strrchr(file, '\\');
    const char* base = file;
    if (p1) base = p1 + 1;
    if (p2 && p2 + 1 > base) base = p2 + 1;
    return std::string(base);
}

// 输出 "YYYYMMDD HH:MM:SS.uuuuuu"
inline void FormatTime(char* buf, std::size_t n) {
    using namespace std::chrono;
    const auto now  = system_clock::now();
    const std::time_t secs = system_clock::to_time_t(now);
    const long micros = static_cast<long>(
        duration_cast<microseconds>(now.time_since_epoch()).count() % 1000000);

    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &secs);
#else
    localtime_r(&secs, &tm_buf);
#endif
    std::snprintf(buf, n, "%04d%02d%02d %02d:%02d:%02d.%06ld",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, micros);
}

}  // namespace internal

// ============================================================================
//  Logger：全局单例，负责真正的输出
// ============================================================================
class Logger {
public:
    static Logger& Instance() {
        static Logger inst;
        return inst;
    }

    // ---- 级别控制 ----
    void SetMinLogLevel(int level) {
        min_level_.store(level, std::memory_order_relaxed);
    }
    int MinLogLevel() const {
        return min_level_.load(std::memory_order_relaxed);
    }
    bool ShouldLog(int severity) const {
        return severity >= MinLogLevel();
    }

    // ---- 输出目标 ----
    void SetLogToStderr(bool enable) {
        std::lock_guard<std::mutex> lk(mutex_);
        log_to_stderr_ = enable;
    }
    bool LogToStderr() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return log_to_stderr_;
    }

    void SetLogToFile(const std::string& path) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (file_stream_.is_open()) file_stream_.close();
        if (!path.empty()) {
            file_stream_.open(path.c_str(), std::ios::out | std::ios::app);
        }
    }

    // ---- 彩色输出（仅在 stderr 为终端时生效）----
    void SetColor(bool enable) {
        std::lock_guard<std::mutex> lk(mutex_);
        use_color_ = enable;
    }
    bool Color() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return use_color_;
    }

    // ---- 核心写日志 ----
    void Write(int severity, const char* file, int line,
               const std::string& msg) {
        std::lock_guard<std::mutex> lk(mutex_);

        char timebuf[64];
        internal::FormatTime(timebuf, sizeof(timebuf));

        std::string record;
        record.reserve(128 + msg.size());
        record += SeverityChar(severity);
        record += timebuf;
        record += ' ';
        record += std::to_string(internal::ThreadId());
        record += ' ';
        record += internal::Basename(file);
        record += ':';
        record += std::to_string(line);
        record += "] ";
        record += msg;
        record += '\n';

        if (log_to_stderr_) {
            if (use_color_ && internal::StderrIsTty()) {
                std::fputs(SeverityColor(severity), stderr);
                std::fwrite(record.data(), 1, record.size(), stderr);
                std::fputs("\033[0m", stderr);
            } else {
                std::fwrite(record.data(), 1, record.size(), stderr);
            }
            std::fflush(stderr);
        }

        if (file_stream_.is_open()) {
            file_stream_.write(record.data(),
                               static_cast<std::streamsize>(record.size()));
            file_stream_.flush();
        }

        if (severity == FATAL) {
            if (file_stream_.is_open()) file_stream_.flush();
            std::fflush(stderr);
            std::abort();
        }
    }

    void Flush() {
        std::lock_guard<std::mutex> lk(mutex_);
        std::fflush(stderr);
        if (file_stream_.is_open()) file_stream_.flush();
    }

private:
    Logger() : min_level_(INFO), log_to_stderr_(true), use_color_(true) {}
    ~Logger() {
        if (file_stream_.is_open()) file_stream_.close();
    }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::atomic<int>  min_level_;
    bool              log_to_stderr_;
    bool              use_color_;
    std::ofstream     file_stream_;
    mutable std::mutex mutex_;
};

// ============================================================================
//  LogMessage：一条日志消息（临时对象，析构时输出）
// ============================================================================
class LogMessage {
public:
    LogMessage(int severity, const char* file, int line, bool force = false)
        : severity_(severity),
          file_(file),
          line_(line),
          enabled_(force || Logger::Instance().ShouldLog(severity)) {}

    ~LogMessage() {
        if (enabled_) {
            Logger::Instance().Write(severity_, file_, line_, stream_.str());
        }
    }

    std::ostream& stream() {
        return enabled_ ? static_cast<std::ostream&>(stream_)
                        : internal::NullOstream();
    }

private:
    int                 severity_;
    const char*         file_;
    int                 line_;
    bool                enabled_;
    std::ostringstream  stream_;
};

// 用于让 "cond ? void : stream" 这种三元表达式合法化
class LogMessageVoidify {
public:
    LogMessageVoidify() = default;
    void operator&(std::ostream&) const {}
};

// ============================================================================
//  便捷接口函数
// ============================================================================
inline void SetMinLogLevel(int level)  { Logger::Instance().SetMinLogLevel(level); }
inline int  MinLogLevel()              { return Logger::Instance().MinLogLevel(); }
inline void SetLogToStderr(bool on)    { Logger::Instance().SetLogToStderr(on); }
inline void SetLogToFile(const std::string& path) {
    Logger::Instance().SetLogToFile(path);
}
inline void SetLogColor(bool on)       { Logger::Instance().SetColor(on); }
inline void FlushLog()                 { Logger::Instance().Flush(); }

// 兼容 glog 命名习惯的空实现
inline void InitGoogleLogging(const char* /*argv0*/ = nullptr) {}
inline void ShutdownGoogleLogging()    { FlushLog(); }

}  // namespace glog

// ============================================================================
//  宏定义
// ============================================================================

// ---- 普通日志 ----
#define LOG(severity) \
    ::glog::LogMessage(::glog::severity, __FILE__, __LINE__).stream()

// ---- 条件日志：condition 为真时才输出 ----
#define LOG_IF(severity, condition) \
    !(condition) ? (void)0 : ::glog::LogMessageVoidify() & \
        ::glog::LogMessage(::glog::severity, __FILE__, __LINE__).stream()

// ---- Debug 日志：NDEBUG 下完全消除，否则无条件输出 ----
#ifdef NDEBUG
#  define DLOG(severity) \
       true ? (void)0 : ::glog::LogMessageVoidify() & \
           ::glog::LogMessage(::glog::severity, __FILE__, __LINE__).stream()
#  define DLOG_IF(severity, condition) \
       true ? (void)0 : ::glog::LogMessageVoidify() & \
           ::glog::LogMessage(::glog::severity, __FILE__, __LINE__).stream()
#else
#  define DLOG(severity) \
       ::glog::LogMessage(::glog::severity, __FILE__, __LINE__, true).stream()
#  define DLOG_IF(severity, condition) \
       !(condition) ? (void)0 : ::glog::LogMessageVoidify() & \
           ::glog::LogMessage(::glog::severity, __FILE__, __LINE__, true).stream()
#endif

// ---- CHECK：条件不满足则打印 FATAL 并 abort ----
#define CHECK(condition) \
    (condition) ? (void)0 : ::glog::LogMessageVoidify() & \
        ::glog::LogMessage(::glog::FATAL, __FILE__, __LINE__).stream() \
            << "Check failed: " #condition " "

#define GLOG_LITE_CHECK_OP(op, val1, val2)                                  \
    do {                                                                    \
        const auto& _gl_v1 = (val1);                                        \
        const auto& _gl_v2 = (val2);                                        \
        if (!(_gl_v1 op _gl_v2)) {                                          \
            std::ostringstream _gl_oss;                                     \
            _gl_oss << "Check failed: " #val1 " " #op " " #val2 " ("        \
                    << _gl_v1 << " vs " << _gl_v2 << ")";                   \
            ::glog::Logger::Instance().Write(                               \
                ::glog::FATAL, __FILE__, __LINE__, _gl_oss.str());          \
        }                                                                   \
    } while (0)

#define CHECK_EQ(v1, v2) GLOG_LITE_CHECK_OP(==, v1, v2)
#define CHECK_NE(v1, v2) GLOG_LITE_CHECK_OP(!=, v1, v2)
#define CHECK_LT(v1, v2) GLOG_LITE_CHECK_OP(<,  v1, v2)
#define CHECK_LE(v1, v2) GLOG_LITE_CHECK_OP(<=, v1, v2)
#define CHECK_GT(v1, v2) GLOG_LITE_CHECK_OP(>,  v1, v2)
#define CHECK_GE(v1, v2) GLOG_LITE_CHECK_OP(>=, v1, v2)

#define CHECK_NOTNULL(val) \
    ::glog::LogMessage(::glog::FATAL, __FILE__, __LINE__).stream() \
        << "Check failed: " #val " != nullptr "

#endif  // GLOG_LITE_LOGGING_HPP