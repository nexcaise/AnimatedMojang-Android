#include "mod/MyMod.h"
#include "client/Client.h"

#include <dlfcn.h>
#include <cstdint>
#include <exception>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <vector>

namespace my_mod {
namespace {

constexpr const char* kLauncherProfiles =
    "/sdcard/Android/media/org.levimc.launcher/minecraft";

bool hasModResources(const std::filesystem::path& directory) {
    std::error_code error;
    return std::filesystem::is_regular_file(
        directory / "resources" / "data" / "normal_cave.mesh", error);
}

std::vector<std::filesystem::path> findModDirectories() {
    std::vector<std::filesystem::path> candidates;

    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&MyMod::getInstance), &info) && info.dli_fname) {
        const auto libraryDirectory = std::filesystem::path(info.dli_fname).parent_path();
        candidates.push_back(libraryDirectory);
        candidates.push_back(libraryDirectory.parent_path());
    }

    std::error_code error;
    const std::filesystem::path profiles(kLauncherProfiles);
    if (std::filesystem::is_directory(profiles, error)) {
        for (std::filesystem::directory_iterator it(profiles, error), end;
             !error && it != end; it.increment(error)) {
            std::error_code entryError;
            if (it->is_directory(entryError)) {
                candidates.push_back(it->path() / "mods" / "AnimatedMojang");
            }
        }
    }

    std::vector<std::filesystem::path> valid;
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && hasModResources(candidate)) {
            valid.push_back(candidate);
        }
    }
    return valid;
}

void loadConfig(const std::filesystem::path& file, animated_mojang::Config& config,
                Logger& logger) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error)) return;

    try {
        std::ifstream input(file);
        const auto json = nlohmann::json::parse(input);
        config.enabled = json.value("enabled", config.enabled);
        config.minecraftTitleAnimation =
            json.value("minecraftTitleAnimation", config.minecraftTitleAnimation);
        config.caveBackground = json.value("caveBackground", config.caveBackground);
        config.replayOnReturnToMenu =
            json.value("replayOnReturnToMenu", config.replayOnReturnToMenu);
        config.blockMenuInputDuringIntro =
            json.value("blockMenuInputDuringIntro", config.blockMenuInputDuringIntro);
        config.maxPresentationSeconds =
            json.value("maxPresentationSeconds", config.maxPresentationSeconds);
        if (json.contains("menuScreenNames")) {
            config.menuScreenNames = json.at("menuScreenNames").get<std::vector<std::string>>();
        }
    } catch (const std::exception& exception) {
        logger.warn("Cannot parse config.json: ", exception.what(), "; using defaults.");
    }
}

}

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    if (mLoaded) return true;
    mLoaded = true;

    mLogger.info("Loading Animated Mojang through NMDK.");

    const auto candidates = findModDirectories();
    if (!candidates.empty()) {
        setModDir(candidates.front());
        mLogger.info("Mod directory: ", mModDir.string());
    } else {
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&MyMod::getInstance), &info) && info.dli_fname) {
            setModDir(std::filesystem::path(info.dli_fname).parent_path());
        }
        mLogger.warn("Could not find the resources directory. Looked beside the native library and in launcher profiles.");
    }

    loadConfig(mConfigDir / "config.json", mConfig, mLogger);
    animated_mojang::client::start(mConfig, mModDir);
    mLogger.info("Initialization completed.");
    return true;
}

bool MyMod::enable() {
    animated_mojang::client::start(mConfig, mModDir);
    return true;
}

bool MyMod::disable() {
    animated_mojang::client::stop();
    return true;
}

bool MyMod::unload() {
    animated_mojang::client::stop();
    return true;
}

}

struct PLModRegistration {
    void* instance;
    bool (*load)(void* instance, void* context);
    bool (*enable)(void* instance, void* context);
    bool (*disable)(void* instance, void* context);
    bool (*unload)(void* instance, void* context);
};

static bool plLoad(void*, void*) { return my_mod::MyMod::getInstance().load(); }
static bool plEnable(void*, void*) { return my_mod::MyMod::getInstance().enable(); }
static bool plDisable(void*, void*) { return my_mod::MyMod::getInstance().disable(); }
static bool plUnload(void*, void*) { return my_mod::MyMod::getInstance().unload(); }

static PLModRegistration sRegistration = {
    &my_mod::MyMod::getInstance(), plLoad, plEnable, plDisable, plUnload
};

#if defined(__GNUC__) || defined(__clang__)
#define MOD_EXPORT __attribute__((visibility("default")))
#else
#define MOD_EXPORT
#endif

extern "C" {

MOD_EXPORT const void* PLGetModRegistration() {
    auto& mod = my_mod::MyMod::getInstance();
    mod.load();
    mod.enable();
    return &sRegistration;
}

using jint = int32_t;
MOD_EXPORT jint JNI_OnLoad(void*, void*) {
    auto& mod = my_mod::MyMod::getInstance();
    mod.load();
    mod.enable();
    return 0x00010006;
}

}
