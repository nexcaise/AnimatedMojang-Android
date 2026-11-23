#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nmdk/NMDK.hpp>

namespace animated_mojang::client::signatures {

inline constexpr std::string_view MinecraftLibrary = "libminecraftpe.so";
inline constexpr std::string_view GlClear = "glClear";
inline constexpr std::string_view GlColorMask = "glColorMask";
inline constexpr std::string_view EglSwapBuffers = "eglSwapBuffers";

// ---------------------------------------------------------------------------
// FIXED targets for Minecraft Bedrock Android arm64-v8a 1.26.62 (build 2662).
// Every function has exactly ONE signature or ONE (vtable symbol, slot index);
// there are no fallback candidates. When a game update moves something, the
// hook is skipped and the reason is written to the log; edit the value here.
// ---------------------------------------------------------------------------

// Non-virtual functions (std::function lambda bodies etc.): byte signatures.
inline constexpr std::string_view GetTechStack =
    R"sig(FD 7B BD A9 F5 0B 00 F9 F4 4F 02 A9 FD 03 00 91 F4 03 01 AA F5 03 00 AA ?? ?? ?? 94 20 01 00 B4 F3 03 00 AA 00 1C 40 F9 40 01 00 B4)sig";

inline constexpr std::string_view PlayButton =
    R"sig(FF C3 02 D1 FD 7B 07 A9 F7 43 00 F9 F6 57 09 A9 F4 4F 0A A9 FD C3 01 91 56 D0 3B D5 ?? ?? ?? ?? 21 ?? ?? 91 C8 16 40 F9)sig";

inline constexpr std::string_view SettingsButton =
    R"sig(FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 82 04 80 52 A8 83 1F F8 13 04 40 F9)sig";

inline constexpr std::string_view RealmsButton =
    R"sig(FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 C8 16 40 F9 A8 83 1F F8 13 04 40 F9 E0 03 13 AA)sig";

inline constexpr std::string_view DressingRoomButton =
    R"sig(FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 05 80 52 A8 83 1F F8 13 04 40 F9 E0 23 00 91 74 6E 44 F9 ?? ?? ?? ?? 00 E4 00 6F 08 F0 A7 52 E8 43 00 B9 E0 03 01 AD E1 23 00 91 E2 83 00 91 E0 03 14 AA ?? ?? ?? ?? F4 1B 40 F9 94 02 00 B5)sig";

inline constexpr std::string_view SkinsMenuButton =
    R"sig(FF 83 03 D1 FD 7B 0B A9 F6 57 0C A9 F4 4F 0D A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 A2 04 80 52 A8 83 1F F8 13 04 40 F9)sig";

inline constexpr std::string_view MarketplaceButton =
    R"sig(FF C3 03 D1 FD 7B 0B A9 F7 63 00 F9 F6 57 0D A9 F4 4F 0E A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? 21 ?? ?? 91 C8 16 40 F9)sig";

inline constexpr std::string_view AchievementsButton =
    R"sig(FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? 21 ?? ?? 91 C8 16 40 F9 02 05 80 52 A8 83 1F F8 13 04 40 F9 E0 23 00 91 74 6E 44 F9 ?? ?? ?? ?? 00 E4 00 6F 08 F0 A7 52 E8 43 00 B9 E0 03 01 AD E1 23 00 91 E2 83 00 91 E0 03 14 AA ?? ?? ?? ?? F4 1B 40 F9 ?? ?? ?? ?? E0 13 40 F9 FF 13 00 F9 ?? ?? ?? ?? ?? ?? ?? ?? E8 23 40 39 ?? ?? ?? ?? E0 0F 40 F9 ?? ?? ?? ?? 60 F2 44 F9)sig";

inline constexpr std::string_view FeedbackButton =
    R"sig(FF 43 07 D1 FD 7B 17 A9 FC 6F 18 A9 FA 67 19 A9 F8 5F 1A A9 F6 57 1B A9 F4 4F 1C A9 FD C3 05 91 57 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? E8 16 40 F9 82 04 80 52)sig";

inline constexpr std::string_view SignInButton =
    R"sig(FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 55 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? A8 16 40 F9 42 04 80 52 A8 83 1F F8 13 04 40 F9)sig";

inline constexpr std::string_view MenuExit =
    R"sig(FD 7B BE A9 F3 0B 00 F9 FD 03 00 91 09 04 40 F9 F3 03 08 AA 20 6D 44 F9 ?? ?? ?? 97 ?? ?? ?? ?? ?? ?? ?? 91 ?? ?? ?? ?? ?? ?? ?? 91 1F 00 00 72 0A 02 80 52 4B 03 80 52)sig";

// OreUI router: navigate(Router* self, std::string const& route, RouteHistoryAction action).
// Every OreUI page push funnels through here (Profile, Inbox, Social drawer, Achievements, ...),
// including pages that have no dedicated StartMenu button handler. The first argument after
// `this` is the route string ("/inbox", "/profile", "/friends-drawer", ...).
inline constexpr std::string_view RouteNavigate =
    R"sig(FF 83 06 D1 FD 7B 16 A9 FC 5F 17 A9 F6 57 18 A9 F4 4F 19 A9 FD 83 05 91 57 D0 3B D5 F4 03 00 AA F5 03 02 2A E8 16 40 F9 A8 83 1F F8 00 38 40 F9 08 00 40 F9 09 0D 40 F9 E8 63 00 91 20 01 3F D6 80 3A 40 F9 08 00 40 F9 08 11 40 F9 E1 63 00 91)sig";

// Virtual functions: NMDK virtual functions Resolver (nmdk::vtable::resolveFunction).
// symbol = Itanium vtable symbol ("_ZTV" + mangled class name), located through RTTI.
// index  = 0-based slot after the typeinfo pointer; a virtual destructor uses 2 slots
//          (slot order follows LeviLamina's declaration order of the class chain).
struct VirtualHook {
    std::string_view symbol;
    std::size_t index;
};

inline constexpr VirtualHook StartMenuOnOpen{"_ZTV25StartMenuScreenController", 7};
inline constexpr VirtualHook StartMenuTick{"_ZTV25StartMenuScreenController", 3};
inline constexpr VirtualHook ProgressBar{"_ZTV19ProgressBarRenderer", 17};
inline constexpr VirtualHook InGameHud{"_ZTV16InGamePlayScreen", 20};
inline constexpr VirtualHook LeaveGame{"_ZTV14ClientInstance", 15};
// ClientInstance::update(bool isInitFinished, bool allowGameUpdate) -> bool (hooked: saves the ClientInstance).
inline constexpr VirtualHook ClientUpdate{"_ZTV14ClientInstance", 24};
// ClientInstance::getScreenName() const -> std::string (only resolved, called through the saved instance).
inline constexpr VirtualHook ClientGetScreenName{"_ZTV14ClientInstance", 244};
inline constexpr VirtualHook Panorama{"_ZTV16PanoramaRenderer", 17};
inline constexpr VirtualHook Cubemap{"_ZTV23CubemapBackgroundScreen", 20};
inline constexpr VirtualHook DirectoryPack{"_ZTV27DirectoryPackAccessStrategy", 9};
inline constexpr VirtualHook ZipPack{"_ZTV21ZipPackAccessStrategy", 9};

// Result of resolving one hook target. On failure `address` is 0 and `failure`
// holds a human readable reason that Client.cpp writes to the log.
struct Resolution {
    nmdk::memory::Address address = 0;
    std::string source;   // how it was found (for the success log)
    std::string failure;  // why it was not found (for the failure log)
    explicit operator bool() const noexcept { return address != 0; }
};

struct Resolved {
    Resolution getTechStack;
    Resolution playButton;
    Resolution settingsButton;
    Resolution realmsButton;
    Resolution dressingRoomButton;
    Resolution skinsMenuButton;
    Resolution marketplaceButton;
    Resolution achievementsButton;
    Resolution feedbackButton;
    Resolution signInButton;
    Resolution menuExit;
    Resolution routeNavigate;
    Resolution startMenuOnOpen;
    Resolution startMenuTick;
    Resolution progressBar;
    Resolution inGameHud;
    Resolution leaveGame;
    Resolution clientUpdate;
    Resolution clientGetScreenName;
    Resolution panorama;
    Resolution cubemap;
    Resolution directoryPack;
    Resolution zipPack;

    std::size_t matchedCount() const noexcept {
        std::size_t count = 0;
        for (const auto* r : {
            &getTechStack,
            &playButton,
            &settingsButton,
            &realmsButton,
            &dressingRoomButton,
            &skinsMenuButton,
            &marketplaceButton,
            &achievementsButton,
            &feedbackButton,
            &signInButton,
            &menuExit,
            &routeNavigate,
            &startMenuOnOpen,
            &startMenuTick,
            &progressBar,
            &inGameHud,
            &leaveGame,
            &clientUpdate,
            &clientGetScreenName,
            &panorama,
            &cubemap,
            &directoryPack,
            &zipPack}) {
            if (r->address != 0) ++count;
        }
        return count;
    }
};

template <typename AddressMap>
inline Resolution resolveSignature(AddressMap const& addresses, std::string_view pattern) {
    Resolution r;
    const auto it = addresses.find(std::string(pattern));
    if (it != addresses.end() && it->second) {
        r.address = it->second;
        r.source = "signature";
        return r;
    }
    r.failure = "signature not found in the executable region of " + std::string(MinecraftLibrary)
        + " (game build differs from 1.26.62 or the code changed). Pattern: " + std::string(pattern);
    return r;
}

inline Resolution resolveVirtual(std::string_view library, VirtualHook const& hook) {
    Resolution r;
    const auto slot = std::to_string(hook.index);
    void** vtable = nmdk::vtable::findVtable(library, hook.symbol);
    if (!vtable) {
        r.failure = "vtable not found: no exported symbol or RTTI typeinfo for " + std::string(hook.symbol)
            + " in " + std::string(library) + " (class renamed/removed or RTTI stripped)";
        return r;
    }
    const auto address = nmdk::vtable::resolveFunction(hook.symbol, hook.index, library);
    if (!address) {
        r.failure = "vtable " + std::string(hook.symbol) + " found but slot " + slot
            + " is null or unreadable (slot index moved or vtable is shorter)";
        return r;
    }
    r.address = address;
    r.source = "vtable " + std::string(hook.symbol) + " slot " + slot;
    return r;
}

inline Resolved resolveAll(std::string_view library) {
    const std::vector<std::string_view> patterns{
        GetTechStack,
        PlayButton,
        SettingsButton,
        RealmsButton,
        DressingRoomButton,
        SkinsMenuButton,
        MarketplaceButton,
        AchievementsButton,
        FeedbackButton,
        SignInButton,
        MenuExit,
        RouteNavigate
    };
    const auto addresses = nmdk::signature::resolveSignatures(
        library,
        std::span<const std::string_view>{patterns.data(), patterns.size()},
        nmdk::memory::Scan::Executable);

    Resolved result;
    result.getTechStack = resolveSignature(addresses, GetTechStack);
    result.playButton = resolveSignature(addresses, PlayButton);
    result.settingsButton = resolveSignature(addresses, SettingsButton);
    result.realmsButton = resolveSignature(addresses, RealmsButton);
    result.dressingRoomButton = resolveSignature(addresses, DressingRoomButton);
    result.skinsMenuButton = resolveSignature(addresses, SkinsMenuButton);
    result.marketplaceButton = resolveSignature(addresses, MarketplaceButton);
    result.achievementsButton = resolveSignature(addresses, AchievementsButton);
    result.feedbackButton = resolveSignature(addresses, FeedbackButton);
    result.signInButton = resolveSignature(addresses, SignInButton);
    result.menuExit = resolveSignature(addresses, MenuExit);
    result.routeNavigate = resolveSignature(addresses, RouteNavigate);
    result.startMenuOnOpen = resolveVirtual(library, StartMenuOnOpen);
    result.startMenuTick = resolveVirtual(library, StartMenuTick);
    result.progressBar = resolveVirtual(library, ProgressBar);
    result.inGameHud = resolveVirtual(library, InGameHud);
    result.leaveGame = resolveVirtual(library, LeaveGame);
    result.clientUpdate = resolveVirtual(library, ClientUpdate);
    result.clientGetScreenName = resolveVirtual(library, ClientGetScreenName);
    result.panorama = resolveVirtual(library, Panorama);
    result.cubemap = resolveVirtual(library, Cubemap);
    result.directoryPack = resolveVirtual(library, DirectoryPack);
    result.zipPack = resolveVirtual(library, ZipPack);
    return result;
}

} // namespace animated_mojang::client::signatures
