#include "core/Log.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace
{
    std::vector<LogEntry> g_Entries;
    std::mutex g_Mutex;
    const auto g_Start = std::chrono::steady_clock::now();
}

namespace Log
{
    void Write(LogLevel level, const char* fmt, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        const char* prefix = level == LogLevel::Error ? "[error] " : level == LogLevel::Warning ? "[warn]  " : "[info]  ";
        std::fprintf(level == LogLevel::Error ? stderr : stdout, "%s%s\n", prefix, buffer);

        const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_Start).count();
        std::lock_guard lock(g_Mutex);
        if (!g_Entries.empty() && g_Entries.back().level == level && g_Entries.back().message == buffer)
        {
            g_Entries.back().count++;
            g_Entries.back().time = t;
            return;
        }
        g_Entries.push_back({ level, buffer, t });
        if (g_Entries.size() > 5000)
            g_Entries.erase(g_Entries.begin(), g_Entries.begin() + 1000);
    }

    void Clear()
    {
        std::lock_guard lock(g_Mutex);
        g_Entries.clear();
    }

    const std::vector<LogEntry>& Entries() { return g_Entries; }

    int CountOf(LogLevel level)
    {
        int n = 0;
        for (const auto& e : g_Entries)
            if (e.level == level) n += e.count;
        return n;
    }
}
