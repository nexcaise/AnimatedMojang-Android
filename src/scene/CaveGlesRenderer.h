#pragma once

#include "scene/CaveMeshBuilder.h"
#include "scene/ImageDecode.h"
#include "scene/TitleAtlas.h"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <vector>

namespace animated_mojang::scene {

// OpenGL ES 3.0 renderer for the cave background, the animated title and the UI fade-in.
//
// Threading model
//   * beginLoad / configure / requestTitleReplay / requestGlRelease / failed / ready: any thread
//   * everything else: the render thread that owns the current EGL context (i.e. inside the glClear hook)
// All file IO, PNG decoding, mesh de-duplication and alpha classification run on background threads;
// the render thread only issues GL calls and never decodes an image.
// Where the visible (outlined) logo sits on screen, as fractions of the screen. The values were measured
// from screenshots of the original logo on each screen (2400x1080), so the replacement lands on top of it.
struct TitleLayout {
    float widthFrac; // visible logo width / screen width
    float centerX;   // visible logo centre X / screen width
    float centerY;   // visible logo centre Y / screen height
};
inline constexpr TitleLayout kTitleLayoutStart{0.5475f, 0.4992f, 0.2458f};    // start_screen
inline constexpr TitleLayout kTitleLayoutProgress{0.5483f, 0.4994f, 0.1644f}; // world loading / saving progress
inline constexpr TitleLayout kTitleLayoutPause{0.3504f, 0.2940f, 0.1338f};    // pause_screen (above the buttons)

class CaveGlesRenderer {
public:
    struct FrameParams {
        float width = 0, height = 0;
        glm::mat4 view{1.f};
        double timeMs = 0;
        int titleFrame = -1;     // < 0: no title
        float titleAlpha = 1.f;
        TitleLayout titleLayout = kTitleLayoutStart;
        bool captureBackground = false; // copy the finished cave+title into the fade texture
    };

    CaveGlesRenderer();
    ~CaveGlesRenderer();
    CaveGlesRenderer(CaveGlesRenderer const&) = delete;
    CaveGlesRenderer& operator=(CaveGlesRenderer const&) = delete;

    // ---- any thread -----------------------------------------------------------------------
    void beginLoad(std::filesystem::path const& modDirectory); // idempotent
    void configure(bool titleEnabled, bool keepTitleResident);
    void requestTitleReplay();
    void requestGlRelease() { mReleaseRequested.store(true); }
    // Render thread: frees GL objects if a release was requested from another thread (mod disable).
    void serviceRelease();
    bool failed() const { return mStatus.load() == Status::Failed; }
    bool ready() const { return mGlReady.load(); }

    // ---- render thread ----------------------------------------------------------------------
    // Creates GL resources once the background load finished and streams title pages.
    // Returns true when the presentation may start (renderer is drawable, or has permanently failed).
    bool prepare();
    bool drawFrame(FrameParams const& params);
    bool renderFadeOverlay(float screenWidth, float screenHeight, float overlayAlpha);
    // Draws only the resting (final) title frame on top of the current frame, without the cave.
    // Used for screens outside the Start Menu that must still show the replacement logo (pause, modal progress).
    bool renderTitleOverlay(float screenWidth, float screenHeight, float alpha, TitleLayout const& layout);
    void destroy(bool contextLost);

private:
    enum class Status : int { Idle, Loading, Ready, Failed };

    struct LoadResult {
        bool ok = false;
        std::string error;
        CaveMeshCpu mesh;
        Rgba8Image atlas;
        std::optional<TitleAtlasInfo> title;
        std::string titleError;
    };
    static LoadResult loadAll(std::filesystem::path modDirectory);

    struct TitlePage {
        enum class State { Absent, Decoding, Decoded, Resident, Failed };
        State state = State::Absent;
        GLuint tex = 0;
        std::future<Rgba8Image> pending;
        Rgba8Image image;
        int rowsUploaded = 0;
    };

    // --- GL setup
    bool pollCpuLoad();
    bool initGl();
    GLuint buildProgram(const char* vs, const char* fs, const char* defines, const char* name);
    void releaseGl(bool contextLost);

    // --- drawing
    void drawCave(FrameParams const& p);
    void drawTitle(FrameParams const& p);
    void captureFade(float w, float h);
    void drawFullscreenFade(float alpha);

    // --- title streaming
    void pumpTitleStreaming();
    void requestTitlePage(int page);
    void evictTitlePagesExcept(int keepPage);
    void releaseTitlePage(TitlePage& page, bool contextLost);
    bool titlePagesWanted(int page) const;

    // lifecycle
    std::atomic<Status> mStatus{Status::Idle};
    std::atomic<bool> mGlReady{false};
    std::atomic<bool> mReleaseRequested{false};
    std::atomic<bool> mReplayRequested{false};
    std::atomic<bool> mTitleEnabled{true};
    std::atomic<bool> mKeepTitleResident{false};
    std::mutex mLoadMutex;
    std::future<LoadResult> mLoadFuture;
    std::filesystem::path mModDirectory;
    bool mLoadStarted = false;

    // CPU copies (kept so a lost EGL context can be rebuilt without touching the disk)
    CaveMeshCpu mMesh;
    Rgba8Image mAtlasImage;
    std::optional<TitleAtlasInfo> mTitleInfo;
    bool mCpuReady = false;

    // GL objects: cave
    EGLContext mContext = EGL_NO_CONTEXT;
    GLuint mProgOpaque = 0, mProgCutout = 0;
    GLint mLocMvpOpaque = -1, mLocLavaOpaque = -1, mLocMvpCutout = -1, mLocLavaCutout = -1;
    GLuint mCaveVao = 0, mCaveVbo = 0, mCaveIbo = 0, mAtlasTex = 0;
    GLenum mIndexType = GL_UNSIGNED_SHORT;
    // GL objects: title + fade (shared quad program)
    GLuint mQuadProg = 0, mQuadVao = 0, mQuadVbo = 0;
    GLint mLocRect = -1, mLocUv = -1, mLocAlpha = -1, mLocOpaque = -1;
    GLuint mFadeTex = 0;
    int mFadeW = 0, mFadeH = 0;

    // title streaming state
    std::vector<TitlePage> mPages;
    bool mTitleSettled = false;
    bool mOverlayMode = false; // overlay draw in progress: stream only the resting page
    int mPriorityPage = -1;
    bool mWarnedTitle = false;

    // GL init retry policy
    int mInitAttempts = 0;
    std::chrono::steady_clock::time_point mNextInitAttempt{};
};

} // namespace animated_mojang::scene
