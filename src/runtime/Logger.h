#pragma once

#include <sstream>
#include <string>
#include <utility>

#if defined(__ANDROID__)
#include <android/log.h>
#else
#include <cstdio>
#endif

namespace my_mod {

class Logger {
public:
    explicit Logger(const char* tag = "AnimatedMojang") : mTag(tag) {}

    template <typename... Args>
    void info(Args&&... args) const { write(Level::Info, std::forward<Args>(args)...); }

    template <typename... Args>
    void warn(Args&&... args) const { write(Level::Warning, std::forward<Args>(args)...); }

    template <typename... Args>
    void error(Args&&... args) const { write(Level::Error, std::forward<Args>(args)...); }

private:
    enum class Level { Info, Warning, Error };

    template <typename... Args>
    void write(Level level, Args&&... args) const {
        std::ostringstream stream;
        (stream << ... << std::forward<Args>(args));
        const std::string message = stream.str();
#if defined(__ANDROID__)
        int priority = ANDROID_LOG_INFO;
        if (level == Level::Warning) priority = ANDROID_LOG_WARN;
        if (level == Level::Error) priority = ANDROID_LOG_ERROR;
        __android_log_write(priority, mTag, message.c_str());
#else
        const char* prefix = level == Level::Error ? "ERROR" : level == Level::Warning ? "WARN" : "INFO";
        std::fprintf(stderr, "[%s] %s: %s\n", prefix, mTag, message.c_str());
#endif
    }

    const char* mTag;
};

}
