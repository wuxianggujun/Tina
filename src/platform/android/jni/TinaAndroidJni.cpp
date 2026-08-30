// JNI entry points for the Android host.
//
// This file is the only place Java names appear. It deliberately does NOT contain engine logic:
// every call forwards into tina_platform_android, so the touch slot mapping, ring buffer and
// lifecycle rules stay in one testable place rather than being reimplemented in JNI glue.
//
// Registration goes through RegisterNatives in JNI_OnLoad rather than relying on name mangling.
// docs/platform-input.md records that cocos2d-x exported by mangling alone and resolved its
// reverse-call class by string name -- a class that only existed in an optional module, so the
// default project failed at the call site with no diagnostic. Explicit registration instead fails at
// library load, naming the method whose signature did not match, so a Java/C++ signature drift is
// impossible to ship silently.

#include <tina/platform/android/AndroidInputBridge.hpp>
#include <tina/platform/android/AndroidPlatformFactory.hpp>

#if defined(TINA_ANDROID_WITH_BGFX)
// Private backend header, reached the same way a desktop composition root reaches it: the concrete
// renderer never appears in a public header. Optional because enabling it requires a host-built
// shaderc, which not every checkout has -- see docs/building.md.
#include "render/bgfx/BgfxRenderDevice.hpp"

#include "TinaAndroidGame.hpp"

#include <tina/core/time/MonotonicClock.hpp>
#include <tina/render/RenderFrame.hpp>
#include <tina/runtime/EngineConfig.hpp>
#include <tina/runtime/EngineHost.hpp>
#include <tina/runtime/spi/EngineCompositionFactories.hpp>
#include <tina/task/disabled/DisabledTaskSystemFactory.hpp>
#endif

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <memory>
#include <new>

namespace {

// The single authority for the Java <-> C++ action mapping.
//
// docs/platform-input.md's lesson 5: cocos2d-x aligned its C++ and Java key enums by numeric
// coincidence, with no translation layer and no static assertion, so reordering either side would
// silently misroute every event. Here the C++ enum is authoritative, Java holds only integers that
// this table validates, and the static_asserts below make a reordering a compile error.
using Tina::Platform::AndroidTouchAction;

static_assert(static_cast<int>(AndroidTouchAction::Down) == 0);
static_assert(static_cast<int>(AndroidTouchAction::Move) == 1);
static_assert(static_cast<int>(AndroidTouchAction::Up) == 2);
static_assert(static_cast<int>(AndroidTouchAction::Cancel) == 3);

// Rejects an unknown value instead of casting it. A cast would turn a Java-side change into an
// out-of-range enum that then corrupts pointer state downstream.
[[nodiscard]] bool decodeTouchAction(jint rawAction, AndroidTouchAction& action) noexcept
{
    switch (rawAction)
    {
    case 0:
        action = AndroidTouchAction::Down;
        return true;
    case 1:
        action = AndroidTouchAction::Move;
        return true;
    case 2:
        action = AndroidTouchAction::Up;
        return true;
    case 3:
        action = AndroidTouchAction::Cancel;
        return true;
    default:
        return false;
    }
}

// Everything one activity needs, kept alive across the Java surface lifecycle.
//
// The queue outlives the backend on purpose: Android can deliver touches before the surface exists
// and after it is destroyed. A backend-owned queue would force every push to first check whether a
// backend happens to exist.
struct TinaAndroidSession final {
    std::shared_ptr<Tina::Platform::AndroidTouchEventQueue> touchEvents =
        std::make_shared<Tina::Platform::AndroidTouchEventQueue>();
    std::shared_ptr<Tina::Platform::AndroidKeyEventQueue> keyEvents =
        std::make_shared<Tina::Platform::AndroidKeyEventQueue>();
    std::shared_ptr<Tina::Platform::AndroidTextEventQueue> textEvents =
        std::make_shared<Tina::Platform::AndroidTextEventQueue>();
    std::shared_ptr<Tina::Platform::AndroidCompositionEventQueue> compositionEvents =
        std::make_shared<Tina::Platform::AndroidCompositionEventQueue>();
    // Whether the IME asked for cursor updates.
    //
    // Written from the UI thread (InputConnection.requestCursorUpdates) and read there too, but atomic
    // because the frame loop may not be the UI thread in a later host. Gated rather than reported
    // unconditionally: Android's contract is that a host sends CursorAnchorInfo after a request, and
    // most IMEs never ask -- doing it every frame is a JNI call plus an object allocation for nothing.
    std::atomic<bool> cursorUpdatesRequested{false};
    // Maps Android's sparse, reused pointer ids to dense engine slots. Lives here because the JNI
    // layer is the only thing that ever sees a raw Android pointer id.
    Tina::Platform::AndroidTouchSlotTable slots{};
    // Owned only until EngineHost takes it. EngineHost deliberately does not expose its backend, so
    // the Android lifecycle (which must reach onNativeWindowCreated/Destroyed) is reached through the
    // non-owning androidBackend pointer below, captured while the factory runs.
    std::unique_ptr<Tina::Integration::IWindowSurfacePlatformBackend> backend{};
    // Non-owning view of the live backend, valid for as long as whoever owns it. Set from inside the
    // platform factory, which is the only point where both the concrete type and the handover are
    // visible.
    Tina::Platform::IAndroidPlatformBackend* androidBackend = nullptr;
#if defined(TINA_ANDROID_WITH_BGFX)
    // EngineHost owns the platform backend and render device once it exists, so this session only
    // holds them before handover. That is why they are separate members rather than one composite:
    // Android's window arrives asynchronously, and EngineHost::Create runs the platform factory
    // immediately, so the host cannot be built until the first surface is bound.
    std::unique_ptr<Tina::EngineHost> host{};
    std::unique_ptr<Tina::IGameApplication> game{};
    Tina::Platform::Android::AndroidGameTelemetry telemetry{};
    // Set once the host has ended, so tick() is not called after a terminal outcome -- EngineHost
    // rejects that, and retrying would turn one clean exit into an error every frame.
    bool hostFinished = false;
    // Tracked because the game has one key to give: without alternating, every press would re-show a
    // keyboard that is already up and there would be no way to dismiss it.
    bool softKeyboardVisible = false;
    // Automatic prefers Vulkan on Android (Render::preferredRendererApi), which is right for real
    // devices. It is selectable because some Vulkan implementations are unusable and the GLES path
    // exists as the documented fallback -- measured 2026-08-29: the SDK emulator's vulkan.ranchu.so
    // segfaults inside SetDebugUtilsObjectNameEXT during swapchain creation, so verifying on an
    // emulator requires asking for OpenGLES explicitly.
    Tina::Render::RendererApi rendererApi = Tina::Render::RendererApi::Automatic;
#endif
    // Acquired references, released by this session. Java owns the Surface objects; these keep the
    // native handles alive for as long as bgfx might still touch them.
    ANativeWindow* window = nullptr;
    // The window a rebind replaced. bgfx acts on a new binding only at the next submitFrame, so the old
    // one must outlive that frame; it is released when a further replacement arrives or at teardown.
    ANativeWindow* retiredWindow = nullptr;

    [[nodiscard]] Tina::Platform::IAndroidPlatformBackend* androidFacet() noexcept
    {
        return androidBackend;
    }
};

[[nodiscard]] TinaAndroidSession* asSession(jlong handle) noexcept
{
    return reinterpret_cast<TinaAndroidSession*>(static_cast<std::uintptr_t>(handle));
}

#if defined(TINA_ANDROID_WITH_BGFX)
// Mirrors EngineHost::toRenderSurfaceState. Duplicated rather than shared because that one is
// private to EngineHost, and this slice drives the device directly instead of through EngineHost --
// the APK has no game state to run yet. Forwarding nativeBindingRevision is the part that matters:
// omitting it pins the value and the rebind never reaches the backend.
[[nodiscard]] Tina::Render::RenderSurfaceState
tinaRenderSurfaceState(const Tina::Integration::WindowSurfaceSnapshot& snapshot) noexcept
{
    return Tina::Render::RenderSurfaceState{
        .surface =
            {
                .owner = snapshot.surface.owner().value(),
                .index = snapshot.surface.index(),
                .generation = snapshot.surface.generation(),
            },
        .framebufferExtent =
            {
                .width = snapshot.framebufferExtent.width,
                .height = snapshot.framebufferExtent.height,
            },
        .contentScale =
            {
                .x = snapshot.contentScale.x,
                .y = snapshot.contentScale.y,
            },
        .sourceMetricsRevision = snapshot.sourceMetricsRevision,
        .surfaceRevision = snapshot.surfaceRevision,
        .nativeBindingRevision = snapshot.nativeBindingRevision,
        .availability = snapshot.suspended ? Tina::Render::RenderSurfaceAvailability::Suspended
                                           : Tina::Render::RenderSurfaceAvailability::Active,
    };
}
#endif

} // namespace

extern "C" {

// Creating the session does not create the backend: that needs a window, which arrives later via
// surfaceCreated. Splitting them is what lets touches queue up before the surface exists.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeCreateSession(JNIEnv*, jclass)
{
    auto* session = new (std::nothrow) TinaAndroidSession{};
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(session));
}

// Must be called before the first surfaceCreated, since that is when the device is built. Ignored
// entirely when the library was built without a renderer.
JNIEXPORT void JNICALL Java_dev_tina_TinaNative_nativeSetPreferOpenGles(JNIEnv*, jclass, jlong handle,
                                                                      jboolean preferOpenGles)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    session->rendererApi = preferOpenGles == JNI_TRUE ? Tina::Render::RendererApi::OpenGLES
                                                     : Tina::Render::RendererApi::Automatic;
#else
    (void)preferOpenGles;
#endif
}

JNIEXPORT void JNICALL Java_dev_tina_TinaNative_nativeDestroySession(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    // The host owns the backend and the device and tears them down in the right order internally, so
    // destroying it first is both necessary and sufficient. The observing pointer dies with it.
    session->host.reset();
    session->game.reset();
    session->androidBackend = nullptr;
#endif
    if (session->backend != nullptr)
    {
        session->backend->shutdown();
    }
    // Both references are released only now, after the host (and with it bgfx) is gone, so nothing can
    // still query them. Doing it here rather than relying on Java also means a session torn down
    // without surfaceDestroyed still gives its windows back.
    if (session->window != nullptr)
    {
        ANativeWindow_release(session->window);
    }
    if (session->retiredWindow != nullptr)
    {
        ANativeWindow_release(session->retiredWindow);
    }
    delete session;
}

// Returns false rather than throwing: a failed surface bind is a lifecycle outcome the Java side
// decides how to present, and throwing across JNI from a surface callback tends to abort.
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeSurfaceCreated(JNIEnv* env, jclass, jlong handle,
                                                                        jobject surface, jfloat density)
{
    auto* session = asSession(handle);
    if (session == nullptr || surface == nullptr || !(density > 0.0F))
    {
        return JNI_FALSE;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Tina", "ANativeWindow_fromSurface returned null");
        return JNI_FALSE;
    }

    const auto width = static_cast<Tina::u32>(ANativeWindow_getWidth(window));
    const auto height = static_cast<Tina::u32>(ANativeWindow_getHeight(window));
    const Tina::Platform::AndroidNativeWindowHandle nativeWindow{
        .nativeWindow = reinterpret_cast<std::uintptr_t>(window)};
    const Tina::Platform::FramebufferExtent extent{.width = width, .height = height};
    const Tina::Platform::ContentScale scale{.x = density, .y = density};


    // Keyed on the backend facet rather than on ownership: once EngineHost takes the backend, this
    // session no longer holds it, but a replacement window must still drive the rebind.
    if (session->androidFacet() != nullptr)
    {
        // A replacement window: drive the ADR 0034 rebind rather than rebuilding the backend, so the
        // render device keeps its device resources and only the backbuffer is recreated.
        auto* facet = session->androidFacet();
        if (auto status = facet->onNativeWindowCreated(nativeWindow, extent, scale); !status)
        {
            // Logged with the structured reason: a failed rebind stops the frame loop with no other
            // symptom, so without this the app just freezes silently.
            __android_log_print(ANDROID_LOG_ERROR, "Tina", "rebind rejected: domain=%d code=%d %s",
                                static_cast<int>(status.error().code.domain),
                                static_cast<int>(status.error().code.value), status.error().message.c_str());
            ANativeWindow_release(window);
            return JNI_FALSE;
        }
        // The previous window is retired, not released here.
        //
        // onNativeWindowCreated only publishes the new binding; bgfx does not act on it until the next
        // submitFrame calls setPlatformData + reset. Its render thread is therefore still using the old
        // window at this moment, and releasing the last reference now aborts the process with
        // "FORTIFY: pthread_mutex_lock called on a destroyed mutex" from Surface::hook_query.
        //
        // Measured twice on a device: first by rotating (fixed by holding the reference through
        // surfaceDestroyed), then again by backgrounding mid-touch, which reaches this path instead.
        // Holding one extra window until a *later* replacement arrives is a bounded cost -- at most two
        // native windows alive -- and it is the only point where bgfx is known to have moved on.
        if (session->retiredWindow != nullptr)
        {
            ANativeWindow_release(session->retiredWindow);
        }
        session->retiredWindow = session->window;
        session->window = window;
        return JNI_TRUE;
    }

#if defined(TINA_ANDROID_WITH_BGFX)
    // First surface: compose the engine around it.
    //
    // EngineHost::Create runs the platform factory immediately, which is why the host cannot exist
    // before now -- Android hands over its window asynchronously, so there is nothing to build a
    // backend from until this callback fires. That asymmetry is the whole reason ADR 0032's D3 chose
    // external frame driving over a blocking run().
    // One bound action, so the key path is observable end to end. Without a binding a key reaches
    // WindowInputSnapshot and stops there, which is indistinguishable from the bridge not working.
    // Arrows and D-pad map to the same engine Key, so a TV remote drives this too.
    Tina::EngineConfig engineConfig = Tina::EngineConfig::Defaults();
    // Enter is the key that still reaches game code once the TextEdit has focus, which it now does by
    // default. A focused TextEdit consumes *every* key except Tab, Enter/KeypadEnter and Escape
    // (UIInputRouteProducer keeps exactly those three available to the frame action mapper), so the
    // arrows below stop producing actions the moment a field is focused -- correct behaviour, but it
    // silently took the device's `keys=` counter to zero and left it there, which reads exactly like a
    // broken key bridge. The arrows stay bound so a TV remote still drives the highlight when focus is
    // elsewhere; Enter is what makes the counter meaningful with a field focused.
    for (const auto key : {Tina::Platform::Key::Up, Tina::Platform::Key::Down, Tina::Platform::Key::Left,
                           Tina::Platform::Key::Right, Tina::Platform::Key::Enter})
    {
        engineConfig.inputActions.bindings.push_back(Tina::InputActionBinding{
            .input = Tina::PrimaryWindowKeyBinding{.key = key},
            .action = Tina::Platform::Android::AndroidHighlightAction,
            .domain = Tina::InputActionDomain::Frame,
        });
    }

    auto host = Tina::EngineHost::Create(
        engineConfig,
        Tina::EngineCompositionFactories{
            .createMonotonicClock =
                []() -> Tina::Core::Result<std::unique_ptr<Tina::Core::IMonotonicClock>> {
                    return std::unique_ptr<Tina::Core::IMonotonicClock>{
                        std::make_unique<Tina::Core::SteadyMonotonicClock>()};
                },
            .createTaskSystem = Tina::Task::createDisabledTaskSystem,
            .platformRender =
                Tina::WindowSurfacePlatformRenderFactories{
                    .createWindowSurfacePlatformBackend =
                        [session, nativeWindow, extent, scale](const Tina::Platform::PlatformBackendCreateParams&
                                                                  platformParams)
                        -> Tina::Core::Result<
                            std::unique_ptr<Tina::Integration::IWindowSurfacePlatformBackend>> {
                        auto created = Tina::Platform::createAndroidWindowSurfacePlatformBackend(
                            Tina::Platform::AndroidPlatformBackendCreateParams{
                                .platform = platformParams,
                                .window = nativeWindow,
                                .touchEvents = session->touchEvents,
                                .keyEvents = session->keyEvents,
                                .textEvents = session->textEvents,
                                .compositionEvents = session->compositionEvents,
                                .framebufferExtent = extent,
                                .contentScale = scale,
                            });
                        if (!created)
                        {
                            return created;
                        }
                        // Captured here because this is the only point where the concrete backend is
                        // visible: EngineHost takes ownership and never exposes it again, while the
                        // Android lifecycle still has to reach onNativeWindowCreated/Destroyed.
                        session->androidBackend =
                            dynamic_cast<Tina::Platform::IAndroidPlatformBackend*>(created->get());
                        return created;
                    },
                    .createWindowSurfaceRenderDevice =
                        [session](const Tina::Render::RenderDeviceCreateParams& params,
                                  Tina::Integration::NativeWindowSurfaceLease lease)
                        -> Tina::Core::Result<std::unique_ptr<Tina::Render::IRenderDevice>> {
                        Tina::Render::RenderDeviceCreateParams effective = params;
                        // Overridden rather than passed through EngineConfig: the emulator's Vulkan
                        // driver is unusable, and the choice belongs to the host that knows it is on an
                        // emulator, not to engine configuration.
                        effective.rendererApi = session->rendererApi;
                        return Tina::Render::Bgfx::createBgfxRenderDevice(effective, std::move(lease));
                    },
                },
        });
    if (!host)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Tina", "EngineHost::Create rejected: domain=%d code=%d %s",
                            static_cast<int>(host.error().code.domain),
                            static_cast<int>(host.error().code.value), host.error().message.c_str());
        ANativeWindow_release(window);
        return JNI_FALSE;
    }
    session->host = std::move(*host);
    session->game = Tina::Platform::Android::createAndroidGameApplication(session->telemetry);
    if (auto started = session->host->start(*session->game); !started)
    {
        __android_log_print(ANDROID_LOG_ERROR, "Tina", "EngineHost::start rejected: domain=%d code=%d %s",
                            static_cast<int>(started.error().code.domain),
                            static_cast<int>(started.error().code.value), started.error().message.c_str());
        session->host.reset();
        session->game.reset();
        session->androidBackend = nullptr;
        ANativeWindow_release(window);
        return JNI_FALSE;
    }
#else
    // Without a renderer there is no EngineHost either: it requires a render device. The bare backend
    // still runs, which keeps the platform bridge verifiable in a checkout with no host shaderc.
    auto backend = Tina::Platform::createAndroidWindowSurfacePlatformBackend(
        Tina::Platform::AndroidPlatformBackendCreateParams{
            .platform = {},
            .window = nativeWindow,
            .touchEvents = session->touchEvents,
            .keyEvents = session->keyEvents,
            .textEvents = session->textEvents,
            .compositionEvents = session->compositionEvents,
            .framebufferExtent = extent,
            .contentScale = scale,
        });
    if (!backend)
    {
        ANativeWindow_release(window);
        return JNI_FALSE;
    }
    session->androidBackend = dynamic_cast<Tina::Platform::IAndroidPlatformBackend*>(backend->get());
    session->backend = std::move(*backend);
#endif

    session->window = window;
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_dev_tina_TinaNative_nativeSurfaceDestroyed(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return;
    }
    if (auto* facet = session->androidFacet(); facet != nullptr)
    {
        // Also releases every touch slot, so a drag interrupted by a task switch cannot strand its
        // finger -- the cocos2d-x defect ADR 0032 cites.
        (void)facet->onNativeWindowDestroyed();
    }
    session->slots.releaseAll();
#if defined(TINA_ANDROID_WITH_BGFX)
    // Tell the game its gesture stream is gone. The platform layer clears its own pointer slots, but a
    // UI listener latch lives in game state, and losing the window delivers neither an Up nor a Cancel.
    session->telemetry.gestureStreamLost.store(true, std::memory_order_release);
#endif
    // The window reference is deliberately NOT released here.
    //
    // bgfx keeps its own render thread and still holds this ANativeWindow: a suspended surface makes
    // submitFrame skip drawing, but it still pumps bgfx::frame(), and that thread queries the window.
    // Releasing our reference now drops the last one, and the next query aborts the process with
    // "FORTIFY: pthread_mutex_lock called on a destroyed mutex" from Surface::hook_query -- measured on
    // a device by rotating the screen.
    //
    // So the reference is held until either a replacement window arrives (surfaceCreated releases the
    // old one after the rebind succeeds) or the session is destroyed (which tears down the render
    // device first). Java's Surface object dies regardless; this only keeps the native handle alive,
    // which is exactly what ANativeWindow_acquire is for.
}

// One MotionEvent pointer, already split out on the Java side. Java passes the raw Android pointer
// id; the dense slot mapping happens here so both sides cannot disagree about it.
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeOnTouch(JNIEnv*, jclass, jlong handle,
                                                                 jint rawAction, jint androidPointerId, jfloat x,
                                                                 jfloat y)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return JNI_FALSE;
    }

    Tina::Platform::AndroidTouchAction action{};
    if (!decodeTouchAction(rawAction, action))
    {
        return JNI_FALSE;
    }

    Tina::u8 slot = Tina::Platform::AndroidTouchSlotTable::InvalidSlot;
    switch (action)
    {
    case Tina::Platform::AndroidTouchAction::Down:
        slot = session->slots.acquire(androidPointerId);
        break;
    case Tina::Platform::AndroidTouchAction::Move:
        // No acquire on Move: a Move for an untracked id means its Down was lost, and inventing a
        // slot here would report a finger that never went down.
        slot = session->slots.find(androidPointerId);
        break;
    case Tina::Platform::AndroidTouchAction::Up:
    case Tina::Platform::AndroidTouchAction::Cancel:
        slot = session->slots.find(androidPointerId);
        // Released before the push succeeds or fails: the finger is gone either way, and keeping the
        // mapping would leak the slot.
        session->slots.release(androidPointerId);
        break;
    }

    if (slot == Tina::Platform::AndroidTouchSlotTable::InvalidSlot)
    {
        return JNI_FALSE;
    }
    return session->touchEvents->tryPush(Tina::Platform::AndroidTouchEvent{
               .action = action, .pointerSlot = slot, .physicalX = x, .physicalY = y})
               ? JNI_TRUE
               : JNI_FALSE;
}

// One KeyEvent. The raw Android key code crosses as-is; translation to Tina's Key happens in C++ so
// Java owns no mapping, which is the lesson docs/platform-input.md draws from cocos2d-x keeping two
// hand-written enums aligned by coincidence.
//
// Returns false when the event was dropped: an unknown action, or a full queue.
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeOnKey(JNIEnv*, jclass, jlong handle, jint rawAction,
                                                               jint androidKeyCode, jboolean repeat)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return JNI_FALSE;
    }

    Tina::Platform::AndroidKeyAction action{};
    switch (rawAction)
    {
    case 0:
        action = Tina::Platform::AndroidKeyAction::Down;
        break;
    case 1:
        action = Tina::Platform::AndroidKeyAction::Up;
        break;
    default:
        // Rejected rather than cast, for the same reason as touch actions: a cast would turn a Java-side
        // change into an out-of-range enum.
        return JNI_FALSE;
    }

    return session->keyEvents->tryPush(Tina::Platform::AndroidKeyEvent{
               .action = action, .androidKeyCode = androidKeyCode, .repeat = repeat == JNI_TRUE})
               ? JNI_TRUE
               : JNI_FALSE;
}

// Committed IME text, from InputConnection.commitText.
//
// Separate from nativeOnKey because the soft keyboard produces no key codes at all: it delivers whole
// strings, so a backend handling only keys would see nothing while the user typed.
//
// Routed through the composition queue rather than the text queue, because a commit may be resolving an
// active composing pass -- and in that case the Ended stage must be published before this text. Only the
// native session knows whether a composition is in flight, so the decision cannot be made here. When
// there is none, the backend publishes text alone, which is the original behaviour unchanged.
//
// Returns false when the text was rejected -- not strict UTF-8, empty, too long for one slot, or a full
// queue. The caller can then decide whether to retry; silently succeeding would hide lost input.
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeOnTextCommit(JNIEnv* env, jclass, jlong handle,
                                                                     jstring text)
{
    auto* session = asSession(handle);
    if (session == nullptr || text == nullptr)
    {
        return JNI_FALSE;
    }

    // GetStringChars, not GetStringUTFChars.
    //
    // The latter returns *modified* UTF-8: NUL becomes two bytes, and non-BMP characters arrive as
    // CESU-8 surrogate pairs -- so an emoji shows up as two invalid three-byte sequences that strict
    // validation rejects, and the character is silently lost. UTF-16 is what Java actually stores, so
    // converting from it is the only way emoji survive.
    const jchar* utf16 = env->GetStringChars(text, nullptr);
    if (utf16 == nullptr)
    {
        return JNI_FALSE;
    }
    const jsize utf16Length = env->GetStringLength(text);

    Tina::Platform::AndroidCompositionEvent event{};
    const bool built = Tina::Platform::makeAndroidCompositionEventFromUtf16(
        std::u16string_view{reinterpret_cast<const char16_t*>(utf16), static_cast<std::size_t>(utf16Length)},
        // A commit carries no caret of its own -- the text replaces the composing region outright, and
        // the engine's own TextEdit places the caret after it.
        0, Tina::Platform::AndroidCompositionAction::Commit, event);
    env->ReleaseStringChars(text, utf16);
    // An empty commit is accepted by the composition converter (it is meaningful for a preedit) but
    // carries no information as a commit, so it is rejected here rather than queued as a no-op.
    if (!built || event.byteCount == 0)
    {
        return JNI_FALSE;
    }
    return session->compositionEvents->tryPush(event) ? JNI_TRUE : JNI_FALSE;
}

// Composing (preedit) text, from InputConnection.setComposingText.
//
// The raw call is forwarded with no interpretation: whether this is the start of a pass or an update of
// one, and whether an empty string means "cancel", is decided by the native session. Java holding that
// state would be a second copy of the stage semantics, and the two would eventually disagree -- at which
// point Started fires twice or Ended never fires, leaving a preedit on screen permanently.
//
// `cursorUtf16Offset` is Android's newCursorPosition, in UTF-16 code units relative to this text. It is
// converted to codepoints and clamped natively, because an out-of-range cursor makes PlatformFrameBuilder
// reject the entire frame rather than this one transition.
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeOnComposingText(JNIEnv* env, jclass, jlong handle,
                                                                        jstring text, jint cursorUtf16Offset)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return JNI_FALSE;
    }

    Tina::Platform::AndroidCompositionEvent event{};
    bool built = false;
    if (text == nullptr)
    {
        // A null composing text is Android's other way of saying the region is empty, and it means the
        // same thing as "": the session turns it into a cancel.
        built = Tina::Platform::makeAndroidCompositionEventFromUtf16(
            {}, 0, Tina::Platform::AndroidCompositionAction::SetText, event);
    } else
    {
        const jchar* utf16 = env->GetStringChars(text, nullptr);
        if (utf16 == nullptr)
        {
            return JNI_FALSE;
        }
        const jsize utf16Length = env->GetStringLength(text);
        built = Tina::Platform::makeAndroidCompositionEventFromUtf16(
            std::u16string_view{reinterpret_cast<const char16_t*>(utf16),
                                static_cast<std::size_t>(utf16Length)},
            cursorUtf16Offset, Tina::Platform::AndroidCompositionAction::SetText, event);
        env->ReleaseStringChars(text, utf16);
    }
    if (!built)
    {
        return JNI_FALSE;
    }
    return session->compositionEvents->tryPush(event) ? JNI_TRUE : JNI_FALSE;
}

// InputConnection.finishComposingText: the IME gave up the composing region without committing.
//
// Queued rather than acted on immediately, so it keeps its place relative to the setComposingText calls
// around it. Silently ignored natively when nothing was in flight, which IMEs do routinely as they
// attach and detach.
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeOnComposingFinish(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return JNI_FALSE;
    }
    Tina::Platform::AndroidCompositionEvent event{};
    event.action = Tina::Platform::AndroidCompositionAction::Finish;
    return session->compositionEvents->tryPush(event) ? JNI_TRUE : JNI_FALSE;
}

// Records that the IME asked for (or stopped asking for) cursor updates.
//
// Gated because Android's contract is that CursorAnchorInfo is reported *after* a request. Reporting
// unconditionally would cost a JNI call and a CursorAnchorInfo allocation every frame for the majority
// of IMEs that never ask.
JNIEXPORT void JNICALL Java_dev_tina_TinaNative_nativeSetCursorUpdatesRequested(JNIEnv*, jclass, jlong handle,
                                                                              jboolean requested)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return;
    }
    session->cursorUpdatesRequested.store(requested == JNI_TRUE, std::memory_order_release);
}

// Whether the host should be reporting CursorAnchorInfo this frame.
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeCursorUpdatesRequested(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return JNI_FALSE;
    }
    return session->cursorUpdatesRequested.load(std::memory_order_acquire) ? JNI_TRUE : JNI_FALSE;
}

// The focused caret in physical pixels, packed into one jlong, or -1 when nothing is focused.
//
// Packed rather than returned through four calls or an out-parameter array: the four values describe one
// rectangle and must come from the same observation. Four separate JNI reads could straddle a frame and
// hand Java a caret whose height came from a different layout.
//
// Each field is 16 bits, which covers any real display. A caret outside that range means the UI published
// geometry far off-screen, and reporting no caret is better than reporting a wrapped one.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeCaretPixels(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return -1;
    }
    auto* facet = session->androidFacet();
    if (facet == nullptr)
    {
        return -1;
    }
    const auto caret = facet->caretPixels();
    if (!caret.has_value())
    {
        return -1;
    }
    const auto fits = [](Tina::i32 value) noexcept { return value >= 0 && value <= 0xFFFF; };
    if (!fits(caret->x) || !fits(caret->y) || !fits(caret->width) || !fits(caret->height))
    {
        return -1;
    }
    return (static_cast<jlong>(caret->x) << 48) | (static_cast<jlong>(caret->y) << 32) |
           (static_cast<jlong>(caret->width) << 16) | static_cast<jlong>(caret->height);
}

JNIEXPORT void JNICALL Java_dev_tina_TinaNative_nativeOnSoftKeyboardOcclusion(JNIEnv*, jclass, jlong handle,
                                                                             jint occludedPhysicalHeight)
{
    auto* session = asSession(handle);
    if (session == nullptr || occludedPhysicalHeight < 0)
    {
        return;
    }
    if (auto* facet = session->androidFacet(); facet != nullptr)
    {
        (void)facet->onSoftKeyboardOcclusionChanged(static_cast<Tina::u32>(occludedPhysicalHeight));
    }
}

// The engine can only record intent; only Java can call InputMethodManager. Java polls this and acts
// on it, and the read clears the latch so one request yields exactly one IME call.
JNIEXPORT jint JNICALL Java_dev_tina_TinaNative_nativeTakePendingSoftKeyboardRequest(JNIEnv*, jclass,
                                                                                    jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return static_cast<jint>(Tina::Platform::AndroidSoftKeyboardRequest::None);
    }
    auto* facet = session->androidFacet();
    if (facet == nullptr)
    {
        return static_cast<jint>(Tina::Platform::AndroidSoftKeyboardRequest::None);
    }
    return static_cast<jint>(facet->takePendingSoftKeyboardRequest());
}

// Advances exactly one engine frame.
//
// @return the game's frame-update count, or -1 when no frame ran. A count that keeps climbing is what
// proves the engine's phase machinery -- fixed update, frame update, render -- is running on the
// device, which a screenshot alone cannot show.
JNIEXPORT jint JNICALL Java_dev_tina_TinaNative_nativePollFrame(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return -1;
    }

#if defined(TINA_ANDROID_WITH_BGFX)
    if (session->host == nullptr || session->game == nullptr || session->hostFinished)
    {
        return -1;
    }
    // The game may have asked for the keyboard last frame. Translated into the backend's latched
    // request here rather than in the game, because the game has no access to the platform facet -- and
    // the toggle has to alternate, since a phone has one key to give and no second one to hide with.
    if (session->telemetry.softKeyboardToggleRequested.exchange(false, std::memory_order_acq_rel))
    {
        if (auto* facet = session->androidFacet(); facet != nullptr)
        {
            session->softKeyboardVisible = !session->softKeyboardVisible;
            (void)(session->softKeyboardVisible ? facet->requestShowSoftKeyboard()
                                                : facet->requestHideSoftKeyboard());
        }
    }

    // One tick per call, driven by the host's frame callback. This is ADR 0032's D3 in practice: the
    // engine does not own the loop, so Android's own cadence stays in charge.
    auto outcome = session->host->tick(*session->game);
    if (!outcome)
    {
        // Logged because a rejected frame is indistinguishable from a black screen, and the structured
        // error names the invariant that failed. Two real defects were found exactly this way.
        __android_log_print(ANDROID_LOG_ERROR, "Tina", "tick rejected: domain=%d code=%d %s",
                            static_cast<int>(outcome.error().code.domain),
                            static_cast<int>(outcome.error().code.value), outcome.error().message.c_str());
        // Latched: EngineHost rejects a tick after a terminal outcome, so retrying would turn one
        // failure into an error every frame for the rest of the process.
        session->hostFinished = true;
        return -1;
    }
    if (outcome->has_value())
    {
        // A terminal outcome means teardown already ran inside tick(); nothing further may be driven.
        // Logged with the reason because an engine that stopped cleanly and one that failed look
        // identical from Java -- both simply stop producing frames.
        __android_log_print(ANDROID_LOG_INFO, "Tina", "engine run ended, reason=%d",
                            static_cast<int>(**outcome));
        session->hostFinished = true;
        return -1;
    }
    return static_cast<jint>(session->telemetry.frameUpdates.load(std::memory_order_relaxed));
#else
    // No renderer, so no EngineHost either. Polling the bare backend still exercises the window,
    // touch and lifecycle path, which is what makes a shaderc-less checkout verifiable.
    if (session->backend == nullptr)
    {
        return -1;
    }
    auto poll = session->backend->pollFrame();
    if (!poll || !poll->isContinueFrame() || poll->frame() == nullptr)
    {
        return -1;
    }
    return static_cast<jint>(poll->frame()->inputTransitions().size());
#endif
}

// Fixed updates run off the fixed-step accumulator rather than the frame clock. Exposed separately
// because seeing both counters advance is what shows the accumulator survived the move from a
// blocking run() loop to Android's externally driven tick().
// UI updates that actually published a tree. Compared against the frame count, this separates "UI is
// live" from "frames advance but the UI phase is skipped".
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeUiUpdateCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return static_cast<jlong>(session->telemetry.uiUpdates.load(std::memory_order_relaxed));
#else
    return 0;
#endif
}

// Current phase of the animated panel, so a host can correlate a screenshot with what the engine
// believed it was drawing rather than inferring it from pixels.
// Presses the UI actually routed to the panel. A touch can reach WindowInputSnapshot and still hit
// nothing, so this is what proves the hit test and listener registration work.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativePointerPressCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return static_cast<jlong>(session->telemetry.pointerPresses.load(std::memory_order_relaxed));
#else
    return 0;
#endif
}

// Should equal the press count once a gesture ends. Presses running ahead means one was left latched,
// which is what happens when a cancelled gesture is not handled.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativePointerReleaseCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return static_cast<jlong>(session->telemetry.pointerReleases.load(std::memory_order_relaxed));
#else
    return 0;
#endif
}

// Key presses the game observed as a bound action. A key can reach WindowInputSnapshot and still not
// reach game code, so this is what proves the binding and action resolution work too.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeKeyPressCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return static_cast<jlong>(session->telemetry.keyPresses.load(std::memory_order_relaxed));
#else
    return 0;
#endif
}

// Committed text transitions the backend published. Committed text leaves no held state behind, so
// without this there is no way to tell "the IME sent nothing" from "the text never reached a frame".
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeTextCommitCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
    auto* facet = session->androidFacet();
    return facet == nullptr ? 0 : static_cast<jlong>(facet->publishedTextCommitCount());
}

// Composition transitions published, packed as one jlong so all four stages come from one observation.
//
// Split by stage rather than totalled because each imbalance names a different defect: starts without
// ends means a preedit is stuck, ends without starts means the session missed the beginning, and cancels
// climbing alone means the IME keeps taking and dropping the region. A single total hides all three.
//
// 16 bits per stage. A composing pass is a handful of events per character, so 65535 of any one stage is
// far beyond a test session; saturating is better than wrapping into a smaller number that reads as a
// regression.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeCompositionCounts(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
    auto* facet = session->androidFacet();
    if (facet == nullptr)
    {
        return 0;
    }
    const auto pack = [](Tina::u64 value) noexcept -> jlong {
        return static_cast<jlong>(value > 0xFFFFU ? 0xFFFFU : value);
    };
    return (pack(facet->publishedCompositionStartCount()) << 48) |
           (pack(facet->publishedCompositionUpdateCount()) << 32) |
           (pack(facet->publishedCompositionEndCount()) << 16) |
           pack(facet->publishedCompositionCancelCount());
}

// Key events the ring had no room for, kept separate from the touch drop count so an undersized key
// capacity cannot hide behind a healthy touch path.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeDroppedKeyEventCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
    return static_cast<jlong>(session->keyEvents->droppedEventCount());
}

// Whether the UI is actually drawing a preedit right now.
//
// The one thing the backend's stage counters cannot show: a composition stage routed while no TextEdit
// has focus is dropped by routeTextComposition, and the counters advance regardless. So a climbing
// start count with this permanently false is precisely "the platform works, the UI never sees it".
JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeUiPreeditActive(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return JNI_FALSE;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return session->telemetry.uiPreeditActive.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

// Codepoints of committed text in the demo's TextEdit. Rising proves a composing pass resolved into
// real text rather than only painting a preedit that was then discarded.
JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeTextEditCodepointCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return static_cast<jlong>(session->telemetry.textEditCodepoints.load(std::memory_order_relaxed));
#else
    return 0;
#endif
}

JNIEXPORT jboolean JNICALL Java_dev_tina_TinaNative_nativeUiPulseOn(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return JNI_FALSE;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return session->telemetry.uiPulseOn.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeFixedUpdateCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
#if defined(TINA_ANDROID_WITH_BGFX)
    return static_cast<jlong>(session->telemetry.fixedUpdates.load(std::memory_order_relaxed));
#else
    return 0;
#endif
}

JNIEXPORT jlong JNICALL Java_dev_tina_TinaNative_nativeDroppedTouchEventCount(JNIEnv*, jclass, jlong handle)
{
    auto* session = asSession(handle);
    if (session == nullptr)
    {
        return 0;
    }
    return static_cast<jlong>(session->touchEvents->droppedEventCount());
}

} // extern "C"

namespace {

// Signatures are written out rather than inferred, because that is the whole point: a Java-side
// change that does not match one of these fails at System.loadLibrary with the offending name, not
// at the first call.
constexpr const char* TinaNativeClassName = "dev/tina/TinaNative";

const JNINativeMethod TinaNativeMethods[]{
    {"nativeCreateSession", "()J", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeCreateSession)},
    {"nativeDestroySession", "(J)V", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeDestroySession)},
    {"nativeSetPreferOpenGles", "(JZ)V",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeSetPreferOpenGles)},
    {"nativeSurfaceCreated", "(JLandroid/view/Surface;F)Z",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeSurfaceCreated)},
    {"nativeSurfaceDestroyed", "(J)V",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeSurfaceDestroyed)},
    {"nativeOnTouch", "(JIIFF)Z", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeOnTouch)},
    {"nativeOnKey", "(JIIZ)Z", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeOnKey)},
    {"nativeOnTextCommit", "(JLjava/lang/String;)Z",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeOnTextCommit)},
    {"nativeOnComposingText", "(JLjava/lang/String;I)Z",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeOnComposingText)},
    {"nativeOnComposingFinish", "(J)Z",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeOnComposingFinish)},
    {"nativeSetCursorUpdatesRequested", "(JZ)V",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeSetCursorUpdatesRequested)},
    {"nativeCursorUpdatesRequested", "(J)Z",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeCursorUpdatesRequested)},
    {"nativeCaretPixels", "(J)J", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeCaretPixels)},
    {"nativeCompositionCounts", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeCompositionCounts)},
    {"nativeOnSoftKeyboardOcclusion", "(JI)V",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeOnSoftKeyboardOcclusion)},
    {"nativeTakePendingSoftKeyboardRequest", "(J)I",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeTakePendingSoftKeyboardRequest)},
    {"nativePollFrame", "(J)I", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativePollFrame)},
    {"nativeFixedUpdateCount", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeFixedUpdateCount)},
    {"nativeUiUpdateCount", "(J)J", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeUiUpdateCount)},
    {"nativeUiPulseOn", "(J)Z", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeUiPulseOn)},
    {"nativeUiPreeditActive", "(J)Z",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeUiPreeditActive)},
    {"nativeTextEditCodepointCount", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeTextEditCodepointCount)},
    {"nativePointerPressCount", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativePointerPressCount)},
    {"nativePointerReleaseCount", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativePointerReleaseCount)},
    {"nativeKeyPressCount", "(J)J", reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeKeyPressCount)},
    {"nativeTextCommitCount", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeTextCommitCount)},
    {"nativeDroppedKeyEventCount", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeDroppedKeyEventCount)},
    {"nativeDroppedTouchEventCount", "(J)J",
     reinterpret_cast<void*>(&Java_dev_tina_TinaNative_nativeDroppedTouchEventCount)},
};

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*)
{
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || env == nullptr)
    {
        return JNI_ERR;
    }
    jclass nativeClass = env->FindClass(TinaNativeClassName);
    if (nativeClass == nullptr)
    {
        return JNI_ERR;
    }
    const auto methodCount = static_cast<jint>(sizeof(TinaNativeMethods) / sizeof(TinaNativeMethods[0]));
    if (env->RegisterNatives(nativeClass, TinaNativeMethods, methodCount) != JNI_OK)
    {
        return JNI_ERR;
    }
    return JNI_VERSION_1_6;
}
