#ifndef STAR_LOG_HPP
#define STAR_LOG_HPP

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
#else
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#endif


namespace star_log{

/* 等级 */
enum LogSeverity {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4
};

inline const char* SeverityName (int s)
{
    switch (s) 
    {
        case DEBUG: return "DEBUG";
        case INFO : return "INFO ";
        case WARN : return "WARN ";
        case ERROR: return "ERROR";
        case FATAL: return "FATAL";
        default:    return "UNKNOWN";
    }
}

inline const char* SeverityColor(int s) {
    switch (s) {
        case DEBUG: return "\033[36m";   // 青色
        case INFO : return "\033[32m";   // 绿色
        case WARN : return "\033[33m";   // 绿色
        case ERROR: return "\033[31m";   // 绿色
        case FATAL: return "\033[1;31m";   // 绿色
        default:    return "\033[0m"; 
    }
}

namespace uitls
{


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

inline std::string Basename(const char* file)
{
    if (!file || !*file) return "(unknown)";
    const char* p1 = std::strrchr(file, '/');
    const char* p2 = std::strrchr(file, '\\');
    const char* base = file;
    if (p1) base = p1 + 1;
    if (p2 && p2 + 1 > base) base = p2 + 1;
    return std::string(base);
}

inline void FormatTime(char* buf, std::size_t n)
{
    using namespace std::chrono;
    const auto now  = system_clock::now();
    const std::time_t secs = system_clock::to_time_t(now);
    const long micros = static_cast<long>(
        duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);

    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &secs);
#else
    localtime_r(&secs, &tm_buf);
#endif
    std::snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, micros);
}

}

class Logger
{
public:
    static Logger& Instance()
    {
        static Logger inst;
        return inst;
    }

    /* -----> 级别控制 <----- */
    void SetMinLogLevel(int level)
    {
        min_level_.store(level, std::memory_order_relaxed);
    }

    int MinLogLevel() const {
        return min_level_.load(std::memory_order_relaxed);
    }

    bool ShouldLog(int severity) const 
    {
        return severity >= MinLogLevel();
    }

    /* -----> 输出目标 <----- */
    void SetLogToStderr(bool enable) 
    {
        std::lock_guard<std::mutex> lk(mutex_);
        log_to_stderr_ = enable;
    }

    bool LogToStderr() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return log_to_stderr_;
    }

    void SetLogToFile(const std::string& path)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (file_stream_.is_open()) file_stream_.close();
        if(!path.empty())
        {
            file_stream_.open(path.c_str(), std::ios::out | std::ios::app);
        }
    }

    /* -----> 彩色输出 <----- */
    void SetColor(bool enable) 
    {
        std::lock_guard<std::mutex> lk(mutex_);
        use_color_ = enable;
    }

    bool Color() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return use_color_;
    }

    /* -----> 核心日志 <----- */
    void Write(
        int severity,
        const char* file,
        int line,
        const std::string& msg)
    {
        std::lock_guard<std::mutex> lk(mutex_);

        char timebuf[64];
        uitls::FormatTime(timebuf, sizeof(timebuf));
        std::string record;
        record.reserve(128 + msg.size());
        record += '[' ;
        record += SeverityName(severity);
        record += ']';
        record += '[';
        record += timebuf;
        record += ']';
        record += '[' + std::to_string(uitls::ThreadId()) + ']';
        record += '[';
        record += uitls::Basename(file);
        record += ':';
        record += std::to_string(line);
        record += ']';
        record += msg;
        record += '\n';

        if (log_to_stderr_)
        {
            if (use_color_ && uitls::StderrIsTty())
            {
                std::fputs(SeverityColor(severity), stderr);
                std::fwrite(record.data(), 1, record.size(), stderr);
                std::fputs("\033[0m", stderr);
            }
            else 
            {
                std::fwrite(record.data(), 1, record.size(), stderr);
            }
            std::fflush(stderr);
        }

        if (file_stream_.is_open())
        {
            file_stream_.write(record.data(),static_cast<std::streamsize>(record.size()));
            file_stream_.flush();
        }

        if (severity == FATAL)
        {
            if (file_stream_.is_open()) file_stream_.flush();
            std::fflush(stderr);
            std::abort();
        }
    }

    void Flush() 
    {
        std::lock_guard<std::mutex> lk(mutex_);
        std::fflush(stderr);
        if (file_stream_.is_open()) file_stream_.flush();
    }

private:
    Logger() : min_level_(DEBUG), log_to_stderr_(true), use_color_(true) {}
    ~Logger()
    {
        if (file_stream_.is_open()) file_stream_.close();
    }


    std::atomic<int> min_level_;
    bool             log_to_stderr_;
    bool             use_color_;
    std::ofstream    file_stream_;
    mutable std::mutex mutex_;

};

class LogMessage
{
public:
    LogMessage(int severity, const char* file, int line, bool force = false)
        : severity_(severity),
          file_(file),
          line_(line),
          enabled_(force || Logger::Instance().ShouldLog(severity)) 
    {}

    ~LogMessage() 
    {
        if(enabled_) 
        {
            Logger::Instance().Write(severity_, file_, line_, stream_.str());
        }
    }

    std::ostream& stream()
    {
        return enabled_? static_cast<std::ostream&>(stream_) : uitls::NullOstream();
    }



private:
    int                 severity_;
    const char*         file_;
    int                 line_;
    bool                enabled_;
    std::ostringstream  stream_;

};

class LogMessageVoidify
{
public:
    LogMessageVoidify() = default;
    void operator&(std::ostream&) const {}
};

/* -----> 接口函数 <----- */
inline void SetMinLogLevel(int level)  { Logger::Instance().SetMinLogLevel(level); }
inline int  MinLogLevel()              { return Logger::Instance().MinLogLevel(); }
inline void SetLogToStderr(bool on)    { Logger::Instance().SetLogToStderr(on); }
inline void SetLogToFile(const std::string& path) {
    Logger::Instance().SetLogToFile(path);
}
inline void SetLogColor(bool on)       { Logger::Instance().SetColor(on); }
inline void FlushLog()                 { Logger::Instance().Flush(); }

}

// ============================================================================
//  宏定义
// ============================================================================

// ---- 普通日志 ----
#define LOG(severity) \
    ::star_log::LogMessage(::star_log::severity, __FILE__, __LINE__).stream()

// ---- 条件日志：condition 为真时才输出 ----
#define LOG_IF(severity, condition) \
    !(condition) ? (void)0 : ::star_log::LogMessageVoidify() & \
        ::star_log::LogMessage(::star_log::severity, __FILE__, __LINE__).stream()




#endif