package dev.tina;

import android.view.Surface;

/**
 * The complete Java/C++ boundary. Nothing else in the app calls native code.
 *
 * <p>Every method here is registered from C++ via RegisterNatives in JNI_OnLoad, not resolved by
 * name mangling. A signature that stops matching therefore fails at {@link System#loadLibrary},
 * naming the offending method, rather than at the first call. docs/platform-input.md records the
 * alternative: cocos2d-x relied on mangling and string class lookup, and its default project failed
 * silently at the call site.
 */
public final class TinaNative {

    static {
        System.loadLibrary("tina_android");
    }

    private TinaNative() {
    }

    /**
     * Touch action codes. These integers are NOT authoritative -- {@code Tina::Platform::
     * AndroidTouchAction} is, and the C++ side static_asserts these exact values and rejects
     * anything else instead of casting it.
     *
     * <p>That asymmetry is deliberate. cocos2d-x kept two hand-written enums aligned by numeric
     * coincidence with no translation layer and no assertion, so reordering either side silently
     * misrouted every event.
     */
    public static final int TOUCH_DOWN = 0;
    public static final int TOUCH_MOVE = 1;
    public static final int TOUCH_UP = 2;
    public static final int TOUCH_CANCEL = 3;

    /** Soft keyboard intent, mirroring {@code Tina::Platform::AndroidSoftKeyboardRequest}. */
    public static final int KEYBOARD_REQUEST_NONE = 0;
    public static final int KEYBOARD_REQUEST_SHOW = 1;
    public static final int KEYBOARD_REQUEST_HIDE = 2;

    /**
     * Cursor-update bits, mirroring {@code InputConnection.CURSOR_UPDATE_IMMEDIATE} and
     * {@code CURSOR_UPDATE_MONITOR}.
     *
     * <p>Restated here rather than referenced from {@code InputConnection} because the native side stores
     * and masks these values, so they are part of the JNI contract, not just the framework's. The values
     * are the framework's own -- {@code requestCursorUpdates} hands the raw mode across unchanged, so a
     * divergence would misread the IME's request rather than fail to compile.
     *
     * <p>Any other bit the framework adds later is dropped natively: reporting for a mode whose semantics
     * this host does not implement is worse than not claiming it.
     */
    public static final int CURSOR_UPDATE_IMMEDIATE = 1;
    public static final int CURSOR_UPDATE_MONITOR = 2;

    /**
     * Allocates the engine session. Does not create the platform backend: that needs a Surface,
     * which arrives later. The split is what lets touches queue before a surface exists.
     */
    public static native long nativeCreateSession();

    /**
     * Forces the OpenGL ES renderer instead of the default (which prefers Vulkan on Android).
     *
     * <p>Must be called before the first surface is bound, since that is when the render device is
     * created. Needed on emulators: the SDK emulator's Vulkan implementation
     * ({@code vulkan.ranchu.so}) segfaults during swapchain creation, so GLES is the only way to
     * verify rendering there. Real devices should keep the default.
     */
    public static native void nativeSetPreferOpenGles(long session, boolean preferOpenGles);

    /**
     * Runs the browsable sample gallery instead of the telemetry demo.
     *
     * <p>Must be called before the first surface is bound, since that is when the application is built.
     *
     * <p>Opt-in rather than the default: the telemetry demo is what carries the device evidence -- the
     * counters this class exposes all read from it -- so replacing it would trade something proven for
     * something nicer to look at. Enable with {@code am start --ez tina.gallery true}.
     */
    public static native void nativeSetUseGallery(long session, boolean useGallery);

    /**
     * Supplies the UI font as a filesystem path.
     *
     * <p>Java picks it because /system/fonts is a stable location but its contents are not -- DroidSans on
     * older emulators, Roboto on modern devices, vendor names elsewhere. A name hard-coded in C++ would
     * render text on the devices that happen to match and silently fall back to blocks on the rest.
     *
     * <p>A path rather than bytes: the engine reads it with its own memory-resource-aware file IO, so
     * nothing copies a megabyte of font through a JNI array.
     *
     * <p>Must be called before the first surface is bound, since that is when the UI context is built.
     * Without it every glyph draws as a solid block.
     */
    public static native void nativeSetUiFontPath(long session, String path);

    /**
     * Supplies the directory this app's shipped content was extracted to.
     *
     * <p>Java owns this because only Java can: APK assets are not files, so nothing in the engine's read
     * path can open them, and the destination to copy them to is a {@code Context} property with no
     * native equivalent. Extracting rather than teaching the engine to read an {@code AAssetManager}
     * keeps one read path across desktop, browser and device.
     *
     * <p>The engine puts this in its content root, so game code asks for {@code "assets/game.recipe"}
     * here exactly as it does beside the executable on desktop -- it never learns which platform
     * answered.
     *
     * <p>Must be called before the first surface is bound, since that is when the engine config is
     * built. Optional: an app that ships no content simply does not call it, and the root then rejects
     * every lookup rather than returning a path that cannot exist.
     */
    public static native void nativeSetContentRootPath(long session, String path);

    public static native void nativeDestroySession(long session);

    /**
     * Binds a Surface. Called again with a replacement window after a background/foreground cycle,
     * which drives the surface rebind rather than rebuilding the backend.
     *
     * @param density display density as a scale factor (densityDpi / 160)
     * @return false when the surface could not be bound; the caller decides how to present that
     */
    public static native boolean nativeSurfaceCreated(long session, Surface surface, float density);

    /** Releases the window and every tracked finger, so an interrupted drag cannot strand one. */
    public static native void nativeSurfaceDestroyed(long session);

    /**
     * One pointer of one MotionEvent. The raw Android pointer id is passed through as-is; mapping it
     * to a dense engine slot happens in C++ so the two sides cannot disagree about it.
     *
     * @return false when the event was dropped (unknown action, untracked pointer, or a full queue)
     */
    public static native boolean nativeOnTouch(
            long session, int action, int androidPointerId, float x, float y);

    /** Key action codes, mirroring {@code Tina::Platform::AndroidKeyAction}. */
    public static final int KEY_DOWN = 0;
    public static final int KEY_UP = 1;

    /**
     * One KeyEvent. The raw Android {@code KEYCODE_*} is passed through untranslated -- the mapping to
     * the engine's Key lives in C++, so this side owns no key table at all.
     *
     * <p>That asymmetry is the point: cocos2d-x kept two hand-written key enums aligned by numeric
     * coincidence, with no translation layer and no assertion, so reordering either side silently
     * misrouted every event.
     *
     * @return false when the event was dropped (unknown action, unmapped key, or a full queue)
     */
    public static native boolean nativeOnKey(
            long session, int action, int androidKeyCode, boolean repeat);

    /**
     * Committed IME text, from {@code InputConnection.commitText}.
     *
     * <p>Separate from {@link #nativeOnKey} because the soft keyboard produces no key codes at all -- it
     * delivers whole strings, so a host handling only keys would see nothing while the user typed.
     *
     * <p>A commit may be resolving an active composing pass, in which case the engine must publish the
     * composition's end before this text. Only the native session knows whether one is in flight, so it
     * decides -- this side just forwards the commit.
     *
     * @return false when the text was rejected: not strict UTF-8 (which includes emoji, see the native
     *     comment), empty, longer than one queue slot, or a full queue
     */
    public static native boolean nativeOnTextCommit(long session, String text);

    /**
     * Composing (preedit) text, from {@code InputConnection.setComposingText}.
     *
     * <p>Forwarded raw, with no interpretation. Whether this begins a composing pass or updates one, and
     * whether an empty string means the pass was abandoned, is decided natively -- this side holds no
     * composition state at all. Holding it here would be a second copy of the stage semantics, and the
     * two would eventually disagree: Started would fire twice, or the end would never fire and a preedit
     * would stay on screen for the rest of the run.
     *
     * @param cursorUtf16Offset Android's {@code newCursorPosition}, in UTF-16 code units relative to
     *     {@code text}. Converted to codepoints and clamped natively, because an out-of-range cursor
     *     makes the engine reject the whole frame rather than this one event.
     * @return false when the event was dropped (invalid UTF-16, too long for one slot, or a full queue)
     */
    public static native boolean nativeOnComposingText(long session, String text, int cursorUtf16Offset);

    /**
     * {@code InputConnection.finishComposingText}: the region was given up without being committed.
     *
     * <p>Queued rather than acted on immediately so it keeps its place relative to the surrounding
     * {@link #nativeOnComposingText} calls. Ignored natively when nothing was in flight, which IMEs do
     * routinely as they attach and detach.
     */
    public static native boolean nativeOnComposingFinish(long session);

    /**
     * Records the cursor-update mode the IME asked for, as the raw bit set from
     * {@code requestCursorUpdates}.
     *
     * <p>Gated because Android's contract is that {@code CursorAnchorInfo} is reported after a request.
     * Reporting unconditionally costs a JNI call and an object allocation every frame for the majority of
     * IMEs that never ask.
     *
     * <p>The mode is carried as a bit set rather than a boolean because Android's two bits mean different
     * things: {@link #CURSOR_UPDATE_IMMEDIATE} wants exactly one report, {@link #CURSOR_UPDATE_MONITOR}
     * wants them until cancelled. Collapsing them loses the ability to stop after one, which turns a
     * one-shot request into a permanent per-frame report.
     */
    public static native void nativeSetCursorUpdateMode(long session, int mode);

    /**
     * The cursor-update bits currently in force, or 0 when the IME wants nothing.
     *
     * <p>Non-consuming: the host reports while either bit is set and retires IMMEDIATE explicitly via
     * {@link #nativeAcknowledgeImmediateCursorUpdate}, so a frame that cannot build a report does not
     * lose the request.
     */
    public static native int nativeCursorUpdateMode(long session);

    /**
     * Clears the IMMEDIATE bit after one report was actually delivered to the IME.
     *
     * <p>Separate from the read because delivery can fail -- no focused caret, no InputMethodManager, no
     * window token -- and clearing at read time would drop the single report the IME asked for.
     */
    public static native void nativeAcknowledgeImmediateCursorUpdate(long session);

    /**
     * The focused caret in physical pixels, packed as {@code x<<48 | y<<32 | width<<16 | height}, or -1
     * when nothing is focused.
     *
     * <p>Packed rather than read through four calls because the four values describe one rectangle and
     * must come from the same observation -- separate reads could straddle a frame and produce a caret
     * whose height came from a different layout. Also -1 when any field exceeds 16 bits, which means the
     * UI published geometry far off-screen; no caret beats a wrapped one.
     */
    public static native long nativeCaretPixels(long session);

    /**
     * Composition transitions published, packed as
     * {@code started<<48 | updated<<32 | ended<<16 | cancelled}.
     *
     * <p>Split by stage rather than totalled because each imbalance names a different defect: starts
     * without ends means a preedit is stuck on screen, ends without starts means the session missed the
     * beginning, and cancels climbing alone means the IME keeps taking and dropping the region. One total
     * hides all three. Each field saturates at 65535.
     */
    public static native long nativeCompositionCounts(long session);

    /**
     * Reports how much of the window bottom the IME currently covers, in physical pixels.
     *
     * <p>Reported rather than computed by the engine: Android's IME height depends on the keyboard
     * app, the language, whether a suggestion strip shows, and split-screen geometry.
     */
    public static native void nativeOnSoftKeyboardOcclusion(long session, int occludedPhysicalHeight);

    /**
     * The engine's pending keyboard intent. Only Java can call InputMethodManager, so the engine records
     * what it wants and this is how the host learns it.
     *
     * <p>The read does <em>not</em> clear the latch: InputMethodManager may be unavailable, or reject the
     * call while the view holds no window token. Consuming here would discard that intent permanently and
     * present as a keyboard that never appears. The host clears it with
     * {@link #nativeAcknowledgeSoftKeyboardRequest} once the IME call actually went through.
     */
    public static native int nativePendingSoftKeyboardRequest(long session);

    /**
     * Clears the pending intent, but only if it still matches {@code request}.
     *
     * <p>Match-checked rather than unconditional so a newer opposite request cannot be erased by a late
     * acknowledgement of an older read -- otherwise a show issued while a hide was in flight would be
     * silently dropped.
     *
     * @param request one of {@link #KEYBOARD_REQUEST_SHOW} or {@link #KEYBOARD_REQUEST_HIDE}
     * @return whether the latch was cleared
     */
    public static native boolean nativeAcknowledgeSoftKeyboardRequest(long session, int request);

    /**
     * Advances exactly one engine frame.
     *
     * @return the game's frame-update count, or -1 when no frame ran. A count that keeps climbing is
     *     what shows the engine's phases are actually running, which a screenshot cannot show. Without
     *     a renderer this instead returns the input-transition count of the polled platform frame.
     */
    public static native int nativePollFrame(long session);

    /**
     * Fixed updates completed, which run off the fixed-step accumulator rather than the frame clock.
     *
     * <p>Separate from the frame count because seeing both advance is what shows the accumulator
     * survived the move from a blocking run() loop to Android's externally driven tick().
     */
    public static native long nativeFixedUpdateCount(long session);

    /**
     * UI updates that actually published a tree. Compared against the frame count this separates "UI is
     * live" from "frames advance but the UI phase is skipped".
     */
    public static native long nativeUiUpdateCount(long session);

    /**
     * Current phase of the animated panel, so a screenshot can be correlated with what the engine
     * believed it was drawing instead of inferring it from pixels.
     */
    public static native boolean nativeUiPulseOn(long session);

    /**
     * Whether the UI is drawing a preedit right now.
     *
     * <p>The one thing {@link #nativeCompositionCounts} cannot show: a composition stage routed while no
     * TextEdit has focus is dropped by the UI, and the counters advance regardless. A climbing start count
     * with this permanently false is exactly "the platform works, the UI never sees it".
     */
    public static native boolean nativeUiPreeditActive(long session);

    /**
     * Codepoints of committed text in the demo's TextEdit.
     *
     * <p>Rising proves a composing pass resolved into real text, rather than only painting a preedit that
     * was later discarded.
     */
    public static native long nativeTextEditCodepointCount(long session);

    /**
     * Presses the UI actually routed to its panel. A touch can reach the engine and still hit nothing,
     * so this is what proves the hit test and listener registration work -- not just the input bridge.
     */
    public static native long nativePointerPressCount(long session);

    /**
     * Should equal the press count once a gesture ends. Presses running ahead means one was left
     * latched, which is what happens when a cancelled gesture is not handled.
     */
    public static native long nativePointerReleaseCount(long session);

    /**
     * Key presses the game observed as a bound action. A key can reach the input snapshot and still not
     * reach game code, so this is what proves the binding and action resolution work too.
     */
    public static native long nativeKeyPressCount(long session);

    /** Key events the bridge queue had no room for, counted separately from touch drops. */
    public static native long nativeDroppedKeyEventCount(long session);

    /**
     * Committed text transitions the backend published into frames.
     *
     * <p>Committed text leaves no held state behind, unlike a key, so without this there is no way to
     * tell "the IME sent nothing" from "the text never reached a frame".
     */
    public static native long nativeTextCommitCount(long session);

    /** Total touch events the bridge queue had no room for, over the session's lifetime. */
    public static native long nativeDroppedTouchEventCount(long session);

    /**
     * What the shipped content chain produced, packed as
     * {@code catalogEntries<<32 | assetsLoaded<<16 | textureExtentCorrect}.
     *
     * <p>This is the only signal that separates the three ways Android content can fail, all of which
     * otherwise look identical from outside: no catalog packaged (every field zero, and no error
     * logged -- a build without {@code -Ptina.assetc} legitimately ships none), a catalog packaged but
     * unopenable (zero with an error line naming the path), and a catalog that opened but whose objects
     * are unreadable or altered (entries non-zero, assets loaded zero).
     *
     * <p>The last bit is the strongest of the three: the catalog is validated against each object's
     * recorded byte size on open, and the extent is then checked against the recipe's 2x2, so a set bit
     * means the host cook, the APK packaging and the first-launch extraction all agreed on the bytes.
     *
     * <p>Packed because the three values describe one outcome, and separate reads could straddle the
     * frame that publishes them. Counts saturate at 65535.
     */
    public static native long nativeContentCounts(long session);
}
