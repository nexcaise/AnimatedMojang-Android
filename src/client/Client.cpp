#include "client/Client.h"
#include "client/Signatures.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <nmdk/NMDK.hpp>

#include "PathView.h"
#include "animation/Timeline.h"
#include "mod/MyMod.h"
#include "scene/CaveCamera.h"
#include "scene/CaveGlesRenderer.h"

// Opaque engine types: only ever passed through, never dereferenced.
class MinecraftUIRenderContext;
class ScreenContext;

namespace animated_mojang::client {
namespace {

using Clock = std::chrono::steady_clock;

// Presentation timeline (ms since the intro started)
constexpr double kTitleStartMs = 4000.0;     // title animation begins
constexpr double kUiRevealMs = 6000.0;       // buttons start fading in
constexpr double kUiFadeEndMs = 7500.0;      // ...and are fully visible
constexpr double kAssetWaitTimeoutMs = 4000.0;

struct Runtime {
    Config config;
    Timeline timeline;
    std::filesystem::path modDirectory;
    Clock::time_point epoch = Clock::now();
    std::recursive_mutex mutex; // guards timeline / camera / config-derived state below
    scene::CaveGlesRenderer caveRenderer;
    scene::CaveCameraController camera;
    scene::CaveCameraFrame cameraFrame;
    bool enteredMenu = false;
    bool pendingReplay = false;
    bool warnedAssetTimeout = false;
    double firstMenuRenderMs = -1;
    Frame frame;
};

Runtime runtime;
std::atomic_bool active = false;
std::atomic_bool skipRequested = false;
std::atomic_bool skipAllowed = false;

// Lifecycle flags written by engine hooks (main thread) and read on the render thread.
std::atomic_bool sIsMainMenu = false;
std::atomic_bool sIsInWorld = false;
std::atomic_bool sIsLoading = true;
std::atomic_bool sInsideCaveRender = false;
std::atomic_bool sNormalLoadingComplete = false;
std::atomic_bool sIsStartScreen = true;
std::atomic_bool sModalDialogueActive = false;
std::atomic<double> sModalSinceMs = 0.0;      // when the modal flag was raised (render-thread expiry)
std::atomic<double> sLastStartMenuTickMs = 0.0;
// The vanilla title logo is never shown again: it is always blanked while the mod is active.
// The replacement title itself is drawn only on the Start Menu screen (see renderMenuFrame).
std::atomic_bool sInterceptTitleTexture = true;
// Safety net: a "modal" raised by a heuristic (screen name / non-navigating button) can miss its
// close event. Never keep the title hidden for longer than this because of the flag alone.
constexpr double kModalMaxHideMs = 10000.0;
std::atomic_bool sSuppressUiDrawing = false;
// ClientInstance saved by the update hook once isInitFinished is true; used to ask for the current screen name.
std::atomic<void*> sClientInstance{nullptr};
// Current screen as reported by ClientInstance::getScreenName (updated every ClientInstance::update).
//   Start    : "start_screen"                      -> replacement logo in the cave pass
//   Pause    : "pause_screen"                      -> overlay logo above the pause buttons
//   Progress : "progress.world_saving_progress_screen" / "world_loading_progress_screen" -> overlay logo
enum class ScreenKind : int { Unknown = -1, Other = 0, Start = 1, Pause = 2, Progress = 3 };
std::atomic<int> sScreenKind{static_cast<int>(ScreenKind::Unknown)};
std::atomic<int> sViewportW{0}, sViewportH{0}; // last menu viewport (avoids a glGetIntegerv per swap)

// Lock-free snapshot of presentation state for hooks that must not take the mutex.
std::atomic<scene::CavePage> sCurrentPage{scene::CavePage::Main};
std::atomic<Phase> sPhase{Phase::Waiting};
std::atomic<double> sElapsedMs{0.0};
std::atomic<float> sMenuAlpha{0.f};

bool isMainMenuActive() {
    return active.load() && sNormalLoadingComplete.load() && sIsMainMenu.load() && !sIsInWorld.load()
        && !sIsLoading.load() && !runtime.caveRenderer.failed();
}

double nowMs() {
    return std::chrono::duration<double, std::milli>(Clock::now() - runtime.epoch).count();
}

auto& logger() {
    return my_mod::MyMod::getInstance().getLogger();
}

// Raises/clears the modal flag and remembers when it was raised.
void setModalActive(bool on) {
    if (!on) { sModalDialogueActive.store(false); return; }
    if (!sModalDialogueActive.exchange(true)) sModalSinceMs.store(nowMs());
}

bool matches(std::vector<std::string> const& names, std::string const& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

// Caller holds runtime.mutex.
void updatePresentation(double time, bool foregroundMenu, bool ready) {
    if (foregroundMenu && runtime.pendingReplay) {
        runtime.pendingReplay = false;
        runtime.timeline = Timeline{Timing{.maxPresentationMs = runtime.config.maxPresentationSeconds * 1000.0}};
        runtime.camera.replay();
        runtime.firstMenuRenderMs = -1;
        runtime.warnedAssetTimeout = false;
    }
    if (foregroundMenu) {
        runtime.enteredMenu = true;
        if (runtime.firstMenuRenderMs < 0) runtime.firstMenuRenderMs = time;
    }
    if (skipRequested.exchange(false)) runtime.timeline.skip();
    runtime.frame = runtime.timeline.update(time, foregroundMenu, ready, runtime.config.minecraftTitleAnimation);
    skipAllowed.store(foregroundMenu && runtime.frame.phase != Phase::MenuIdle);
    sPhase.store(runtime.frame.phase);
    sElapsedMs.store(runtime.frame.elapsedMs);
    sMenuAlpha.store(runtime.frame.menuAlpha);
}

// -----------------------------------------------------------------------------
// Signature-resolved Native Hooks (version-independent where patterns match)
// -----------------------------------------------------------------------------

// 1. ui::ScreenTechStackSelector::getTechStackForScreen
using GetTechStackFn = int (*)(void*, std::string const&);
static GetTechStackFn sOriginalGetTechStack = nullptr;

static int Hooked_ScreenTechStackSelector_getTechStackForScreen(void* self, std::string const& name) {
    if (active.load()) {
        std::string lowerName = name;
        for (char& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // Internal engine transition route between menus: ignore and do not reset start screen state
        if (lowerName.find("pre-start") != std::string::npos) {
            return sOriginalGetTechStack ? sOriginalGetTechStack(self, name) : 0;
        }

        // Social drawer: an overlay on the Start Menu. The replacement title must stay, so no state changes.
        if (scene::isSocialDrawerScreen(name) || scene::isSocialDrawerRoute(name)) {
            return sOriginalGetTechStack ? sOriginalGetTechStack(self, name) : 0;
        }

        const bool isStart = (lowerName == "start_screen" || lowerName == "/main-menu"
            || lowerName == "main_menu_screen"
            || matches(runtime.config.menuScreenNames, name) || matches(runtime.config.menuScreenNames, lowerName));

        static constexpr const char* kModalWords[] = {"dialog", "modal", "popup", "quit", "leave",
                                                      "exit", "alert", "prompt", "confirm"};
        bool isModal = false;
        for (const char* word : kModalWords) isModal = isModal || lowerName.find(word) != std::string::npos;

        const auto mappedPage = [&]() {
            auto p = scene::cavePageForRoute(name);
            return p ? p : scene::cavePageForScreen(name);
        }();

        if (isModal && !mappedPage.has_value()) {
            // Only a pure dialogue (no page of its own) hides the title through the modal flag.
            setModalActive(true);
            sIsStartScreen.store(false);
            logger().info("[AnimatedMojang] Modal/dialogue screen detected: ", name, " -> title suppressed");
        } else if (isStart) {
            sIsStartScreen.store(true);
            sModalDialogueActive.store(false);
        } else {
            sIsStartScreen.store(false);
            sModalDialogueActive.store(false);
        }

        const auto& page = mappedPage;
        if (page.has_value() && sCurrentPage.load() != *page) {
            logger().info("[AnimatedMojang] Screen change detected: ", name, " -> Page ", static_cast<int>(*page));
            sCurrentPage.store(*page);
            sIsMainMenu.store(true);
            sIsInWorld.store(false);
            sIsLoading.store(false);
            if (*page == scene::CavePage::Main) {
                sIsStartScreen.store(true);
                sModalDialogueActive.store(false);
            } else {
                sIsStartScreen.store(false);
                // A modal that belongs to a sub-page must not outlive it and hide the title on Main.
                sModalDialogueActive.store(false);
            }
        }
        if (isStart || name == "/start-from-template") {
            return 0; // TechStack::JsonUI (0)
        }
    }
    return sOriginalGetTechStack ? sOriginalGetTechStack(self, name) : 0;
}

// ----------------------------------------------------------------------------
// Asset loading: all file IO / decoding happens on a background thread (see CaveGlesRenderer).
// ----------------------------------------------------------------------------
static void onNormalLoadingComplete() {
    if (sNormalLoadingComplete.exchange(true)) return;
    logger().info("Normal loading complete; cave assets are prepared in the background.");
    prepareCave(runtime.modDirectory); // idempotent: usually already running since mod start
}

// ----------------------------------------------------------------------------
// Deferred Start Menu Construction
// 0.0s - 4.0s: pure camera flight; 4.0s - 6.0s: title animation; 6.0s+: start menu controls constructed
// ----------------------------------------------------------------------------
static void* sPendingMenuOpenSelf = nullptr;
static std::atomic<bool> sMenuOpenConstructed = false;

// 2. StartMenuScreenController::$onOpen
using MenuOpenFn = void (*)(void*);
static MenuOpenFn sOriginalMenuOpen = nullptr;

static void triggerPendingStartMenuConstruction() {
    if (sPendingMenuOpenSelf) {
        void* self = sPendingMenuOpenSelf;
        sPendingMenuOpenSelf = nullptr;
        sMenuOpenConstructed.store(true);
        logger().info("[AnimatedMojang] Constructing deferred Start Menu screen and displaying options.");
        if (sOriginalMenuOpen) sOriginalMenuOpen(self);
    }
}

static bool introFinishedForMenu() {
    return sPhase.load() == Phase::MenuIdle || sElapsedMs.load() >= kUiRevealMs;
}

static void Hooked_StartMenuScreenController_onOpen(void* self) {
    if (active.load()) {
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false); // a re-opened start menu never sits under a stale modal flag
        onNormalLoadingComplete();

        std::lock_guard guard(runtime.mutex);
        if (runtime.enteredMenu && runtime.config.replayOnReturnToMenu) {
            runtime.pendingReplay = true;
            runtime.caveRenderer.requestTitleReplay();
        }
        runtime.enteredMenu = true;

        if (sPhase.load() == Phase::MenuOpening && sElapsedMs.load() < kUiRevealMs) {
            sPendingMenuOpenSelf = self;
            sMenuOpenConstructed.store(false);
            logger().info("[AnimatedMojang] Deferring Start Menu construction until the intro reaches 6.0s...");
            return;
        }

        sMenuOpenConstructed.store(true);
        if (sCurrentPage.load() != scene::CavePage::Main) {
            logger().info("[AnimatedMojang] StartMenu onOpen: camera returning to the Main vista.");
            sCurrentPage.store(scene::CavePage::Main);
            sIsStartScreen.store(true);
            sModalDialogueActive.store(false);
        }
    }
    if (sOriginalMenuOpen) sOriginalMenuOpen(self);
}

// 2b. StartMenuScreenController::$tick
using MenuTickFn = void (*)(void*);
static MenuTickFn sOriginalMenuTick = nullptr;
static std::atomic<bool> sFirstMenuTick = false;

static void Hooked_StartMenuScreenController_tick(void* self) {
    if (active.load()) {
        const double now = nowMs();
        const double previousTick = sLastStartMenuTickMs.exchange(now);
        // The tick stream paused while another screen was on top and resumed: that screen is gone,
        // so a modal raised for it must not keep hiding the title.
        if (sModalDialogueActive.load() && previousTick > 0.0 && now - previousTick > 250.0
            && sCurrentPage.load() == scene::CavePage::Main) {
            logger().info("[AnimatedMojang] Start menu is on top again -> stale modal cleared.");
            sModalDialogueActive.store(false);
        }
        sIsStartScreen.store(sCurrentPage.load() == scene::CavePage::Main && !sModalDialogueActive.load());

        if (!sFirstMenuTick.exchange(true) || sIsLoading.load()) {
            sIsMainMenu.store(true);
            sIsInWorld.store(false);
            sIsLoading.store(false);
            onNormalLoadingComplete();
        }

        if (sPendingMenuOpenSelf != nullptr) {
            if (introFinishedForMenu()) triggerPendingStartMenuConstruction();
            else return; // do not tick unconstructed menu controls before 6.0s
        }
    }
    if (sOriginalMenuTick) sOriginalMenuTick(self);
}

// 2c-2k. StartMenu button hooks. One template instance per button instead of nine copies.
using GenericButtonFn = void (*)(void*, void*, void*);

struct ButtonSpec {
    const char* label;
    bool navigates;           // true: glide the camera to `page`; false: opens a modal dialogue
    scene::CavePage page;
};
static constexpr ButtonSpec kButtons[] = {
    {"Play -> Worlds vista", true, scene::CavePage::Worlds},
    {"Settings -> Settings vista", true, scene::CavePage::Settings},
    {"Realms -> Realms vista", true, scene::CavePage::Realms},
    {"Dressing Room -> Dressing Room vista", true, scene::CavePage::DressingRoom},
    {"Skins -> Dressing Room vista", true, scene::CavePage::DressingRoom},
    {"Marketplace -> Marketplace vista", true, scene::CavePage::Marketplace},
    {"Achievements -> modal dialogue", false, scene::CavePage::Main},
    {"Feedback -> modal dialogue", false, scene::CavePage::Main},
    {"Sign In -> modal dialogue", false, scene::CavePage::Main},
};

// Moves the presentation to a sub-page vista: camera glides there and the title is hidden.
// Shared by the button hooks, the screen hook and the OreUI route hook.
static void enterSubPage(scene::CavePage page) {
    std::lock_guard guard(runtime.mutex);
    sCurrentPage.store(page);
    sIsStartScreen.store(false);
    sModalDialogueActive.store(false);
    sIsMainMenu.store(true);
    sIsInWorld.store(false);
    sIsLoading.store(false);
    if (runtime.enteredMenu) runtime.timeline.skip();
}

// Camera returns to the Main vista and the title comes back.
static void returnToMainPage() {
    std::lock_guard guard(runtime.mutex);
    sCurrentPage.store(scene::CavePage::Main);
    sIsStartScreen.store(true);
    sModalDialogueActive.store(false);
    sIsMainMenu.store(true);
}

// Returns false when the click must be swallowed (input is blocked during the intro).
static bool onStartMenuButton(ButtonSpec const& spec) {
    if (!active.load()) return true;
    if (spec.navigates && runtime.config.blockMenuInputDuringIntro && sPhase.load() == Phase::MenuOpening
        && sMenuAlpha.load() < 0.95f) {
        return false;
    }
    logger().info("[AnimatedMojang] Button clicked: ", spec.label);
    if (spec.navigates) {
        enterSubPage(spec.page);
    } else {
        std::lock_guard guard(runtime.mutex);
        setModalActive(true);
    }
    return true;
}

template <int Id>
struct ButtonHook {
    static inline GenericButtonFn original = nullptr;
    static void hooked(void* a0, void* a1, void* a2) {
        if (!onStartMenuButton(kButtons[Id])) return;
        if (original) original(a0, a1, a2);
    }
};

static GenericButtonFn sOriginalMenuExit = nullptr;
static void Hooked_Menu_Exit(void* a0, void* a1, void* a2) {
    if (active.load() && !sIsInWorld.load()) {
        logger().info("[AnimatedMojang] Back / Exit pressed -> camera returns to the Main vista");
        returnToMainPage();
    }
    if (sOriginalMenuExit) sOriginalMenuExit(a0, a1, a2);
}

// 2l. OreUI Router::navigate(route, action)
// Profile and Inbox have no dedicated button hook: they are OreUI pages opened by pushing a route
// ("/profile", "/inbox"). Every push funnels through this function, so one hook gives them the same
// camera glide and title hiding as the hooked StartMenu buttons.
// The Social drawer ("/friends-drawer", ...) is deliberately ignored: it is an overlay, the title stays.
using RouteNavigateFn = std::uintptr_t (*)(void*, std::string const*, int);
static RouteNavigateFn sOriginalRouteNavigate = nullptr;

static bool readRouteString(std::string const* routePtr, std::string& out) noexcept {
    if (!routePtr) return false;
    try {
        const std::size_t size = routePtr->size();
        if (size == 0 || size > 512) return false;
        const char* data = routePtr->data();
        if (!data) return false;
        for (std::size_t i = 0; i < size; ++i) {
            const auto c = static_cast<unsigned char>(data[i]);
            if (c < 0x20 || c > 0x7e) return false; // not a plausible route: leave it alone
        }
        out.assign(data, size);
        return true;
    } catch (...) {}
    return false;
}

static void onRouteNavigate(std::string const& route) {
    // Only act while the start menu owns the cave: never in a world, never during cold boot.
    if (!sNormalLoadingComplete.load() || sIsInWorld.load() || sIsLoading.load()) return;

    std::string lower = route;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("pre-start") != std::string::npos) return;

    // Social drawer: overlay on the Start Menu -> camera, page and replacement title stay untouched.
    if (scene::isSocialDrawerRoute(route)) return;

    const auto page = scene::cavePageForRoute(route);
    if (page.has_value()) {
        if (*page == scene::CavePage::Main) {
            if (sCurrentPage.load() != scene::CavePage::Main || sModalDialogueActive.load()) {
                logger().info("[AnimatedMojang] Route ", route, " -> Main vista");
                returnToMainPage();
            }
            return;
        }
        if (sCurrentPage.load() != *page || sIsStartScreen.load() || sModalDialogueActive.load()) {
            logger().info("[AnimatedMojang] Route ", route, " -> Page ", static_cast<int>(*page));
            enterSubPage(*page);
        }
        return;
    }

    // A route without a vista of its own that is still a modal popup: hide the title while it is up.
    static constexpr const char* kModalWords[] = {"modal", "popup", "dialog"};
    for (const char* word : kModalWords) {
        if (lower.find(word) != std::string::npos) {
            if (sCurrentPage.load() == scene::CavePage::Main) {
                logger().info("[AnimatedMojang] Modal route ", route, " -> title suppressed");
                std::lock_guard guard(runtime.mutex);
                setModalActive(true);
                sIsStartScreen.store(false);
            }
            return;
        }
    }
}

static std::uintptr_t Hooked_Router_navigate(void* self, std::string const* route, int action) {
    if (active.load()) {
        std::string name;
        if (readRouteString(route, name)) onRouteNavigate(name);
    }
    return sOriginalRouteNavigate ? sOriginalRouteNavigate(self, route, action) : 0;
}

// 2m. ClientInstance::update(isInitFinished, allowGameUpdate) -> bool
// Saves the ClientInstance (only once the game finished initializing) and, after the real update, asks
// ClientInstance::getScreenName() (vtable slot) for the current screen so pause_screen and
// modal_progress_screen can show the replacement logo.
using ClientUpdateFn = bool (*)(void*, bool, bool);
static ClientUpdateFn sOriginalClientUpdate = nullptr;
using GetScreenNameFn = std::string (*)(void*);
static GetScreenNameFn sGetScreenName = nullptr;

static ScreenKind classifyScreen(std::string_view name, ScreenKind previous) {
    const std::string_view base = name.substr(0, name.find(" - "));
    if (base == "start_screen") return ScreenKind::Start;
    if (name.find("pause_screen") != std::string_view::npos) return ScreenKind::Pause;
    if (name.find("world_saving_progress_screen") != std::string_view::npos
        || name.find("world_loading_progress_screen") != std::string_view::npos) return ScreenKind::Progress;
    // The Social drawer sits on top of the Start Menu: it keeps the replacement title.
    if (previous == ScreenKind::Start && scene::isSocialDrawerScreen(name)) return ScreenKind::Start;
    return ScreenKind::Other;
}

static bool Hooked_ClientInstance_update(void* self, bool isInitFinished, bool allowGameUpdate) {
    if (active.load() && isInitFinished && self && sClientInstance.load(std::memory_order_relaxed) != self) {
        sClientInstance.store(self);
        logger().info("[AnimatedMojang] ClientInstance saved: ", self);
    }
    const bool result = sOriginalClientUpdate ? sOriginalClientUpdate(self, isInitFinished, allowGameUpdate) : false;

    if (active.load(std::memory_order_relaxed) && sGetScreenName) {
        void* instance = sClientInstance.load(std::memory_order_relaxed);
        if (instance) {
            const std::string name = sGetScreenName(instance);
            const auto previous = static_cast<ScreenKind>(sScreenKind.load(std::memory_order_relaxed));
            const ScreenKind kind = classifyScreen(name, previous);
            if (kind != previous) {
                sScreenKind.store(static_cast<int>(kind));
                logger().info("[AnimatedMojang] Screen '", name, "' -> kind ", static_cast<int>(kind));
            }
        }
    }
    return result;
}

// 3. ProgressBarRenderer::$render
using ProgressRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static ProgressRenderFn sOriginalProgressRender = nullptr;

static void Hooked_ProgressBarRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    // Only the initial cold-boot loading bar counts. In-menu bars (Marketplace, Realms, skin sync)
    // must not disturb the cave or the camera interpolation.
    if (active.load() && !sNormalLoadingComplete.load()) {
        sIsLoading.store(true);
        sIsMainMenu.store(false);
        sIsInWorld.store(false);
    }
    if (sOriginalProgressRender) sOriginalProgressRender(self, ctx, client, owner, layer);
}

// 4. InGamePlayScreen::render
using InGamePlayScreenRenderFn = void (*)(void*, void*, const void*);
static InGamePlayScreenRenderFn sOriginalInGamePlayScreenRender = nullptr;

static void Hooked_inGamePlayScreenRender(void* self, void* screenContext, const void* frameObj) {
    if (active.load()) {
        sIsInWorld.store(true);
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(false);
        sIsLoading.store(false);
    }
    if (sOriginalInGamePlayScreenRender) sOriginalInGamePlayScreenRender(self, screenContext, frameObj);
}

// 6. ClientInstance::requestLeaveGame
using LeaveGameFn = void (*)(void*, bool, bool);
static LeaveGameFn sOriginalClientInstanceRequestLeaveGame = nullptr;

static void Hooked_clientInstanceRequestLeaveGame(void* self, bool b1, bool b2) {
    if (active.load()) {
        sIsInWorld.store(false);
        sIsMainMenu.store(true);
        sIsLoading.store(false);
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false);
        sCurrentPage.store(scene::CavePage::Main);
        logger().info("[AnimatedMojang] Leave game detected -> Main menu restored.");
    }
    if (sOriginalClientInstanceRequestLeaveGame) sOriginalClientInstanceRequestLeaveGame(self, b1, b2);
}

// 7. PanoramaRenderer::$render
using PanoramaRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static PanoramaRenderFn sOriginalPanoramaRender = nullptr;

static void Hooked_PanoramaRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (isMainMenuActive() && runtime.config.caveBackground) return; // suppress vanilla panorama
    if (sOriginalPanoramaRender) sOriginalPanoramaRender(self, ctx, client, owner, layer);
}

// 8. CubemapBackgroundScreen::$render
using CubemapRenderFn = void (*)(void*, ScreenContext&, const void*);
static CubemapRenderFn sOriginalCubemapRender = nullptr;

static void Hooked_CubemapBackgroundScreen_render(void* self, ScreenContext& ctx, const void* frameObject) {
    if (active.load()) {
        if (sCurrentPage.load() == scene::CavePage::Main && !sModalDialogueActive.load()) sIsStartScreen.store(true);
        if (sPendingMenuOpenSelf != nullptr && introFinishedForMenu()) triggerPendingStartMenuConstruction();
    }
    if (isMainMenuActive() && runtime.config.caveBackground) return; // suppress vanilla cubemap background
    if (sOriginalCubemapRender) sOriginalCubemapRender(self, ctx, frameObject);
}

// 8b. Resource Pack Asset Interception (Zero RP Requirement)
// Transparent 16x16 PNG to suppress vanilla static title logo and cubemap/panorama textures
static const uint8_t kTransparentPng[75] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0xf3, 0xff, 0x61, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x18, 0x05, 0xa3,
    0x60, 0x14, 0x8c, 0x02, 0x08, 0x00, 0x00, 0x04, 0x10, 0x00, 0x01, 0x55,
    0x37, 0x5a, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82
};

static bool safeExtractPath(const void* pathPtr, std::string& outPath) noexcept {
    if (!pathPtr) return false;
    try {
        const auto* s = reinterpret_cast<const std::string*>(pathPtr);
        size_t sz = s->size();
        if (sz > 0 && sz < 1024) {
            const char* data = s->data();
            if (data) {
                outPath.assign(data, sz);
                return true;
            }
        }
    } catch (...) {}
    return false;
}

using PackGetAssetFn = bool (*)(void*, const ::Core::PathView&, std::string&, bool);
static PackGetAssetFn sOriginalDirectoryPackGetAsset = nullptr;
static PackGetAssetFn sOriginalZipPackGetAsset = nullptr;

static bool shouldInterceptAsset(std::string_view p) {
    // Hot path (every pack asset lookup): no allocation, one cheap prefilter first.
    constexpr std::string_view kUi = "textures/ui/";
    const auto ui = p.find(kUi);
    if (ui == std::string_view::npos) return false;
    const std::string_view rest = p.substr(ui + kUi.size());
    if (rest.starts_with("title")) return sInterceptTitleTexture.load(std::memory_order_relaxed);
    if (rest.starts_with("panorama")) return true;
    return false;
}

static bool handlePackAsset(void* self, const ::Core::PathView& packRelativePath, std::string& outBuffer, bool loadMode, PackGetAssetFn orig) noexcept {
    // Suppress vanilla static title and cubemap/panorama textures with transparent 16x16 PNG
    // Zero Resource Pack requirement - purely intercepts in-memory asset queries
    if (!packRelativePath.empty() && shouldInterceptAsset(packRelativePath.getUtf8StringView())) {
        outBuffer.assign(reinterpret_cast<const char*>(kTransparentPng), sizeof(kTransparentPng));
        return true;
    }

    if (orig) {
        return orig(self, packRelativePath, outBuffer, loadMode);
    }
    return false;
}

static bool Hooked_DirectoryPack_getAsset(void* self, const ::Core::PathView& packRelativePath, std::string& outBuffer, bool loadMode) noexcept {
    return handlePackAsset(self, packRelativePath, outBuffer, loadMode, sOriginalDirectoryPackGetAsset);
}

static bool Hooked_ZipPack_getAsset(void* self, const ::Core::PathView& packRelativePath, std::string& outBuffer, bool loadMode) noexcept {
    return handlePackAsset(self, packRelativePath, outBuffer, loadMode, sOriginalZipPackGetAsset);
}

// 9. glColorMask via GOT Relocation Hook
// During the first 6.0s on the Start Menu, all UI color writes are masked off so the buttons stay hidden.
using GlColorMaskFn = void (*)(GLboolean, GLboolean, GLboolean, GLboolean);
static GlColorMaskFn sOriginalGlColorMask = nullptr;

static void Hooked_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (sSuppressUiDrawing.load()) {
        if (sOriginalGlColorMask) sOriginalGlColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        return;
    }
    if (sOriginalGlColorMask) sOriginalGlColorMask(r, g, b, a);
}

// One complete menu frame, executed on the GL thread right after the game cleared its target.
static void renderMenuFrame(float width, float height, double time) {
    sInsideCaveRender.store(true);
    auto& R = runtime;

    // GL object creation + bounded texture streaming; never blocks on disk or decoding.
    const bool rendererReady = R.caveRenderer.prepare();

    // A heuristic modal can miss its close event; never hide the title because of it for long.
    if (sModalDialogueActive.load() && time - sModalSinceMs.load() > kModalMaxHideMs) {
        logger().info("[AnimatedMojang] Modal flag expired -> title restored.");
        sModalDialogueActive.store(false);
    }

    Frame frame;
    scene::CaveCameraFrame cam;
    scene::CavePage page;
    bool entered, timedOut;
    {
        std::lock_guard guard(R.mutex);
        timedOut = R.firstMenuRenderMs >= 0 && time - R.firstMenuRenderMs > kAssetWaitTimeoutMs;
        entered = R.enteredMenu;
        if (!rendererReady && timedOut) {
            if (!R.warnedAssetTimeout) {
                R.warnedAssetTimeout = true;
                logger().warn("[AnimatedMojang] Cave assets did not become ready within 4s; showing the plain menu.");
            }
        } else {
            updatePresentation(time, R.enteredMenu, rendererReady);
            page = sCurrentPage.load();
            R.cameraFrame = R.camera.update(time, page, R.frame);
            frame = R.frame;
            cam = R.cameraFrame;
        }
    }
    if (!rendererReady && timedOut) { // give the user a usable menu instead of a black screen
        sSuppressUiDrawing.store(false);
        if (sOriginalGlColorMask) sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        sInsideCaveRender.store(false);
        return;
    }

    if (sOriginalGlColorMask) sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    scene::CaveGlesRenderer::FrameParams params;
    params.width = width;
    params.height = height;
    params.timeMs = time;
    const double motionTime = cam.motionTimeMs > 0.0 ? cam.motionTimeMs : time;
    params.view = scene::caveView(cam.pose, motionTime);

    // Title: only on the main start screen, never over dialogues or sub-screens.
    // The screen name from ClientInstance::getScreenName decides ("start_screen"); the flag heuristics are only
    // the fallback while no name is available (ClientInstance not saved yet or getScreenName unresolved).
    const auto screenKind = static_cast<ScreenKind>(sScreenKind.load(std::memory_order_relaxed));
    const bool startByState = screenKind == ScreenKind::Unknown
        ? (sIsStartScreen.load() && !sModalDialogueActive.load())
        : screenKind == ScreenKind::Start;
    const bool isStartScreenActive = startByState && page == scene::CavePage::Main;
    if (isStartScreenActive && frame.titleFrame >= 0 && R.config.minecraftTitleAnimation) {
        float alpha = cam.titleAlpha;
        if (frame.phase == Phase::MenuOpening && frame.elapsedMs < kUiRevealMs)
            alpha = frame.elapsedMs >= kTitleStartMs ? 1.0f : 0.0f;
        if (alpha > 0.01f) {
            params.titleFrame = frame.titleFrame;
            params.titleAlpha = alpha;
        }
    }
    // While buttons fade in (6.0s - 7.5s) keep a clean copy of cave + title for the overlay pass.
    params.captureBackground = page == scene::CavePage::Main && frame.phase == Phase::MenuOpening
        && frame.elapsedMs >= kUiRevealMs && frame.elapsedMs < kUiFadeEndMs;

    R.caveRenderer.drawFrame(params);

    // During the intro suppress all UI color writes (buttons hidden); afterwards let the UI through.
    const bool hideUi = page == scene::CavePage::Main
        && ((frame.phase == Phase::MenuOpening && frame.elapsedMs < kUiRevealMs) || (frame.phase == Phase::Waiting && entered));
    sSuppressUiDrawing.store(hideUi);
    if (sOriginalGlColorMask) sOriginalGlColorMask(hideUi ? GL_FALSE : GL_TRUE, hideUi ? GL_FALSE : GL_TRUE,
                                                   hideUi ? GL_FALSE : GL_TRUE, hideUi ? GL_FALSE : GL_TRUE);
    sInsideCaveRender.store(false);
}

// 10. glClear via GOT Relocation Hook (GL render thread, valid EGLContext)
using GlClearFn = void (*)(GLbitfield);
static GlClearFn sOriginalGlClear = nullptr;

static void Hooked_glClear(GLbitfield mask) {
    if (sOriginalGlClear) sOriginalGlClear(mask);

    // Cheapest checks first: in the world this hook runs several times per frame.
    if (sInsideCaveRender.load(std::memory_order_relaxed)) return;
    runtime.caveRenderer.serviceRelease(); // honours stop() requests on the thread that owns the context
    if (!(mask & GL_COLOR_BUFFER_BIT) || !active.load() || !runtime.config.caveBackground) return;

    if (!sNormalLoadingComplete.load() && (sIsMainMenu.load() || !sIsLoading.load())) onNormalLoadingComplete();

    if (!isMainMenuActive()) {
        // Never leave the UI masked after leaving the menu / when the cave is unusable.
        if (sSuppressUiDrawing.load(std::memory_order_relaxed)) sSuppressUiDrawing.store(false);
        return;
    }

    const double time = nowMs();
    static double sLastRenderMs = 0.0;
    if (time - sLastRenderMs < 1.0) return; // several clears per frame: draw once per burst (works at 120/144/165 Hz)

    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] < 400 || vp[3] < 300) return; // ignore small offscreen targets
    sLastRenderMs = time;
    sViewportW.store(vp[2], std::memory_order_relaxed);
    sViewportH.store(vp[3], std::memory_order_relaxed);

    renderMenuFrame(static_cast<float>(vp[2]), static_cast<float>(vp[3]), time);
}

// 11. eglSwapBuffers via GOT Relocation Hook
using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
static EglSwapBuffersFn sOriginalEglSwapBuffers = nullptr;

static EGLBoolean Hooked_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    if (sSuppressUiDrawing.load(std::memory_order_relaxed) && sOriginalGlColorMask)
        sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Slow fade-in of the start screen buttons (6.0s -> 7.5s): lock-free, uses the published snapshot.
    if (active.load() && !sIsInWorld.load() && sCurrentPage.load() == scene::CavePage::Main
        && sPhase.load() == Phase::MenuOpening) {
        const double elapsed = sElapsedMs.load();
        if (elapsed >= kUiRevealMs && elapsed < kUiFadeEndMs) {
            const float progress = std::clamp(static_cast<float>((elapsed - kUiRevealMs) / (kUiFadeEndMs - kUiRevealMs)), 0.0f, 1.0f);
            const float smooth = progress * progress * (3.0f - 2.0f * progress);
            const int w = sViewportW.load(std::memory_order_relaxed), h = sViewportH.load(std::memory_order_relaxed);
            if (w > 0 && h > 0)
                runtime.caveRenderer.renderFadeOverlay(static_cast<float>(w), static_cast<float>(h), 1.0f - smooth);
        }
    }
    // Pause screen / world saving+loading progress screens: replacement logo on top of the finished frame,
    // placed exactly where the original logo of that screen is.
    const auto overlayKind = static_cast<ScreenKind>(sScreenKind.load(std::memory_order_relaxed));
    if ((overlayKind == ScreenKind::Pause || overlayKind == ScreenKind::Progress) && active.load(std::memory_order_relaxed)) {
        EGLint w = 0, h = 0;
        if (eglQuerySurface(dpy, surf, EGL_WIDTH, &w) && eglQuerySurface(dpy, surf, EGL_HEIGHT, &h) && w >= 400 && h >= 300)
            runtime.caveRenderer.renderTitleOverlay(static_cast<float>(w), static_cast<float>(h), 1.0f,
                overlayKind == ScreenKind::Pause ? scene::kTitleLayoutPause : scene::kTitleLayoutProgress);
    }
    return sOriginalEglSwapBuffers ? sOriginalEglSwapBuffers(dpy, surf) : EGL_TRUE;
}

// Hook failures are always logged with the reason (unresolved target, nmdk::hook error, GOT error).
static std::vector<std::string> sFailedHooks;

template <typename Detour, typename Function>
bool installInlineHook(nmdk::memory::Address moduleBase, const char* name,
                       signatures::Resolution const& resolution, Detour detour, Function& original) {
    if (!resolution) {
        logger().error("Hook FAILED [", name, "]: target not resolved - ", resolution.failure);
        sFailedHooks.push_back(std::string(name) + ": target not resolved");
        return false;
    }
    const auto target = resolution.address;

    nmdk::memory::Address trampoline = 0;
    const auto result = nmdk::hook::hook(
        target,
        reinterpret_cast<nmdk::memory::Address>(detour),
        &trampoline
    );
    // nmdk::hook::hook: 0 = success, -1 = failure.
    if (result != 0) {
        logger().error("Hook FAILED [", name, "]: nmdk::hook::hook returned ", result, " (expected 0) for target ",
                       reinterpret_cast<void*>(target), " (", resolution.source,
                       "); address not hookable, already hooked incompatibly, or out of trampoline range");
        sFailedHooks.push_back(std::string(name) + ": nmdk::hook::hook returned " + std::to_string(result));
        return false;
    }
    if (trampoline == 0) {
        logger().error("Hook FAILED [", name, "]: nmdk::hook::hook succeeded (", result, ") but produced no trampoline for target ",
                       reinterpret_cast<void*>(target), " (", resolution.source, ")");
        sFailedHooks.push_back(std::string(name) + ": no trampoline");
        return false;
    }
    original = reinterpret_cast<Function>(trampoline);
    const auto rva = target >= moduleBase ? target - moduleBase : 0;
    logger().info("Hooked ", name, " at ", reinterpret_cast<void*>(target),
                  " (RVA 0x", std::hex, rva, std::dec, ", ", resolution.source, ")");
    return true;
}

template <typename Detour, typename Function>
bool installGotHook(const char* name, const char* symbol, Detour detour, Function& original) {
    nmdk::memory::Address trampoline = 0;
    const bool installed = nmdk::hook::hookGOT(
        signatures::MinecraftLibrary,
        symbol,
        reinterpret_cast<nmdk::memory::Address>(detour),
        &trampoline
    );
    if (!installed) {
        logger().error("GOT hook FAILED [", name, "]: nmdk::hook::hookGOT returned false for symbol '", symbol,
                       "' in ", signatures::MinecraftLibrary, " (symbol has no GOT entry in this library or the entry could not be patched)");
        sFailedHooks.push_back(std::string(name) + ": hookGOT returned false");
        return false;
    }
    if (trampoline == 0) {
        logger().error("GOT hook FAILED [", name, "]: hookGOT succeeded but returned no original function for symbol '", symbol, "'");
        sFailedHooks.push_back(std::string(name) + ": no original function");
        return false;
    }
    original = reinterpret_cast<Function>(trampoline);
    logger().info("Hooked GOT symbol: ", name, " (", symbol, ")");
    return true;
}

bool installHooks() {
    const auto moduleBase = nmdk::library::base(signatures::MinecraftLibrary);
    if (!moduleBase) {
        logger().error("Hooks NOT installed: ", signatures::MinecraftLibrary, " is not loaded (nmdk::library::base returned 0).");
        return false;
    }
    if (nmdk::architecture() != nmdk::Architecture::AArch64) {
        logger().error("Hooks NOT installed: unsupported process architecture, Animated Mojang requires AArch64.");
        return false;
    }

    const auto resolved = signatures::resolveAll(signatures::MinecraftLibrary);
    logger().info("Resolved ", resolved.matchedCount(), "/23 engine targets (11 via NMDK vtable resolver, 12 via signatures); installing NMDK hooks (",
                  nmdk::version(), ")...");
    sFailedHooks.clear();

    bool complete = true;
    complete &= installInlineHook(moduleBase, "ScreenTechStackSelector::getTechStackForScreen", resolved.getTechStack,
                                  &Hooked_ScreenTechStackSelector_getTechStackForScreen, sOriginalGetTechStack);
    complete &= installInlineHook(moduleBase, "StartMenuScreenController::onOpen", resolved.startMenuOnOpen,
                                  &Hooked_StartMenuScreenController_onOpen, sOriginalMenuOpen);
    complete &= installInlineHook(moduleBase, "StartMenuScreenController::tick", resolved.startMenuTick,
                                  &Hooked_StartMenuScreenController_tick, sOriginalMenuTick);
    complete &= installInlineHook(moduleBase, "StartMenu Play button", resolved.playButton,
                                  &ButtonHook<0>::hooked, ButtonHook<0>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Settings button", resolved.settingsButton,
                                  &ButtonHook<1>::hooked, ButtonHook<1>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Realms button", resolved.realmsButton,
                                  &ButtonHook<2>::hooked, ButtonHook<2>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Dressing Room button", resolved.dressingRoomButton,
                                  &ButtonHook<3>::hooked, ButtonHook<3>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Skins button", resolved.skinsMenuButton,
                                  &ButtonHook<4>::hooked, ButtonHook<4>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Marketplace button", resolved.marketplaceButton,
                                  &ButtonHook<5>::hooked, ButtonHook<5>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Achievements button", resolved.achievementsButton,
                                  &ButtonHook<6>::hooked, ButtonHook<6>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Feedback button", resolved.feedbackButton,
                                  &ButtonHook<7>::hooked, ButtonHook<7>::original);
    complete &= installInlineHook(moduleBase, "StartMenu Sign In button", resolved.signInButton,
                                  &ButtonHook<8>::hooked, ButtonHook<8>::original);
    complete &= installInlineHook(moduleBase, "Menu exit", resolved.menuExit,
                                  &Hooked_Menu_Exit, sOriginalMenuExit);
    complete &= installInlineHook(moduleBase, "OreUI Router::navigate", resolved.routeNavigate,
                                  &Hooked_Router_navigate, sOriginalRouteNavigate);
    complete &= installInlineHook(moduleBase, "ProgressBarRenderer::render", resolved.progressBar,
                                  &Hooked_ProgressBarRenderer_render, sOriginalProgressRender);
    complete &= installInlineHook(moduleBase, "inGamePlayScreenRender", resolved.inGameHud,
                                  &Hooked_inGamePlayScreenRender, sOriginalInGamePlayScreenRender);
    complete &= installInlineHook(moduleBase, "ClientInstance::requestLeaveGame", resolved.leaveGame,
                                  &Hooked_clientInstanceRequestLeaveGame, sOriginalClientInstanceRequestLeaveGame);
    complete &= installInlineHook(moduleBase, "ClientInstance::update", resolved.clientUpdate,
                                  &Hooked_ClientInstance_update, sOriginalClientUpdate);
    if (resolved.clientGetScreenName) {
        sGetScreenName = reinterpret_cast<GetScreenNameFn>(resolved.clientGetScreenName.address);
        logger().info("Resolved ClientInstance::getScreenName at ", reinterpret_cast<void*>(resolved.clientGetScreenName.address),
                      " (", resolved.clientGetScreenName.source, ")");
    } else {
        logger().error("Resolve FAILED [ClientInstance::getScreenName]: ", resolved.clientGetScreenName.failure);
        sFailedHooks.push_back("ClientInstance::getScreenName: target not resolved");
        complete = false;
    }
    complete &= installInlineHook(moduleBase, "PanoramaRenderer::render", resolved.panorama,
                                  &Hooked_PanoramaRenderer_render, sOriginalPanoramaRender);
    complete &= installInlineHook(moduleBase, "CubemapBackgroundScreen::render", resolved.cubemap,
                                  &Hooked_CubemapBackgroundScreen_render, sOriginalCubemapRender);
    complete &= installInlineHook(moduleBase, "DirectoryPackAccessStrategy::getAsset", resolved.directoryPack,
                                  &Hooked_DirectoryPack_getAsset, sOriginalDirectoryPackGetAsset);
    complete &= installInlineHook(moduleBase, "ZipPackAccessStrategy::getAsset", resolved.zipPack,
                                  &Hooked_ZipPack_getAsset, sOriginalZipPackGetAsset);
    complete &= installGotHook("glClear", signatures::GlClear.data(), &Hooked_glClear, sOriginalGlClear);
    complete &= installGotHook("glColorMask", signatures::GlColorMask.data(), &Hooked_glColorMask, sOriginalGlColorMask);
    complete &= installGotHook("eglSwapBuffers", signatures::EglSwapBuffers.data(), &Hooked_eglSwapBuffers, sOriginalEglSwapBuffers);

    if (complete) {
        logger().info("All NMDK hooks installed successfully.");
    } else {
        logger().warn(sFailedHooks.size(), " NMDK hook(s) failed; the mod runs with the hooks that were installed. Failed:");
        for (const auto& failed : sFailedHooks) logger().warn("  - ", failed);
    }
    return true;
}

static bool sHooksInstalled = false;

} // namespace

void prepareCave(std::filesystem::path const& modDirectory) {
    runtime.caveRenderer.beginLoad(modDirectory);
}

void start(Config const& config, std::filesystem::path const& modDirectory) {
    if (active.load() || !config.enabled) return;

    if (!sHooksInstalled) {
        if (!installHooks()) {
            logger().error("Startup aborted: required NMDK signatures or hooks could not be installed.");
            return;
        }
        sHooksInstalled = true;
    }

    {
        std::lock_guard guard(runtime.mutex);
        runtime.config = config;
        runtime.modDirectory = modDirectory;
        runtime.epoch = Clock::now();
        runtime.timeline = Timeline{Timing{.maxPresentationMs = config.maxPresentationSeconds * 1000.0}};
        runtime.frame = {};
        runtime.camera.reset();
        runtime.cameraFrame = {};
        runtime.enteredMenu = runtime.pendingReplay = runtime.warnedAssetTimeout = false;
        runtime.firstMenuRenderMs = -1;
        sCurrentPage.store(scene::CavePage::Main);
        sPhase.store(Phase::Waiting);
        sElapsedMs.store(0.0);
        sMenuAlpha.store(0.f);
    }
    runtime.caveRenderer.configure(config.minecraftTitleAnimation, config.replayOnReturnToMenu);
    sInterceptTitleTexture.store(config.minecraftTitleAnimation);

    active.store(true);
    sIsMainMenu.store(false);
    sIsInWorld.store(false);
    sIsLoading.store(true);
    sNormalLoadingComplete.store(false);

    // CPU-only work (mesh, atlas, title metadata) starts right away on a low-priority thread so the
    // assets are ready before the first menu frame; GL objects are created on the render thread.
    prepareCave(modDirectory);
}

void stop() {
    active.store(false);
    skipRequested.store(false);
    skipAllowed.store(false);
    sSuppressUiDrawing.store(false);
    sScreenKind.store(static_cast<int>(ScreenKind::Unknown));
    // The menu might still be waiting for the deferred construction; never leave it unbuilt.
    triggerPendingStartMenuConstruction();
    // GL objects can only be freed on the render thread: ask it to do so at its next glClear.
    runtime.caveRenderer.requestGlRelease();
}

} // namespace animated_mojang::client
