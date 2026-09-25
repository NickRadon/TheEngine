#pragma once

#include <cstdarg>
#include <string>
#include <vector>

// Engine log; the Console panel reads from this.
enum class LogLevel { Info, Warning, Error };

struct LogEntry
{
    LogLevel level;
    std::string message;
    double time;
    int count = 1; // collapsed duplicates
};

namespace Log
{
    void Write(LogLevel level, const char* fmt, ...);
    void Clear();
    const std::vector<LogEntry>& Entries();
    int CountOf(LogLevel level);
}

#define LOG_INFO(...)  ::Log::Write(LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...)  ::Log::Write(LogLevel::Warning, __VA_ARGS__)
#define LOG_ERROR(...) ::Log::Write(LogLevel::Error, __VA_ARGS__)
