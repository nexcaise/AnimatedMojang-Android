#pragma once

#include "mod/Config.h"
#include "runtime/Logger.h"

#include <filesystem>

namespace my_mod {

class MyMod {
public:
    static MyMod& getInstance();

    bool load();
    bool enable();
    bool disable();
    bool unload();

    const std::filesystem::path& getModDir() const { return mModDir; }
    const std::filesystem::path& getConfigDir() const { return mConfigDir; }
    void setModDir(const std::filesystem::path& dir) {
        mModDir = dir;
        mConfigDir = dir / "config";
    }

    Logger& getLogger() { return mLogger; }
    const Logger& getLogger() const { return mLogger; }

private:
    animated_mojang::Config mConfig;
    std::filesystem::path mModDir;
    std::filesystem::path mConfigDir;
    Logger mLogger{"AnimatedMojang"};
    bool mLoaded = false;
};

}
