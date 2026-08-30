package dev.tina;

import android.app.Activity;
import android.graphics.Matrix;
import android.graphics.Rect;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.inputmethod.CursorAnchorInfo;
import android.view.inputmethod.InputMethodManager;

/**
 * Minimal host that drives the engine's frame loop and services soft-keyboard intent.
 *
 * <p>Frames are driven from a Handler on the UI thread rather than a render thread. That is enough
 * for the current slice -- there is no renderer wired up yet, so this proves the platform bridge and
 * nothing more. ADR 0032's D3 chose external frame driving precisely so this choice stays open: a
 * later renderer can move ticking to its own thread without changing the engine contract.
 */
public final class TinaActivity extends Activity {

    private long session;
    private TinaSurfaceView surfaceView;
    private Handler frameHandler;
    private boolean running;
    private boolean engineEnded;
    private boolean commitEmojiOnFirstFrame;
    private boolean composeTextDiagnostics;
    private int lastLoggedFrame = -1;

    private final Runnable frameTick = new Runnable() {
        @Override
        public void run() {
            if (!running) {
                return;
            }
            if (!engineEnded && surfaceView != null && surfaceView.isSurfaceBound()) {
                final int frames = TinaNative.nativePollFrame(session);
                if (frames < 0) {
                    // Latched: once the engine has ended it will never produce another frame, so
                    // continuing to tick logs the same failure at frame rate. An unlatched version of
                    // this produced 500k identical lines in one background/foreground cycle.
                    engineEnded = true;
                    android.util.Log.e("Tina", "engine stopped producing frames; halting the frame loop");
                } else {
                    if (commitEmojiOnFirstFrame) {
                        commitEmojiOnFirstFrame = false;
                        surfaceView.commitEmojiForDiagnostics();
                    }
                    // One composing step every 30 frames, not per frame: the TextEdit needs a committed
                    // frame to be focusable at all, and spacing the steps leaves the preedit on screen
                    // long enough to be screenshot and to land in separate polls.
                    if (composeTextDiagnostics && frames > 30 && frames % 30 == 0) {
                        composeTextDiagnostics = surfaceView.advanceComposeDiagnostics();
                    }
                    applyPendingSoftKeyboardRequest();
                    reportSoftKeyboardOcclusion();
                    reportCursorAnchorInfo();
                    logProgress(frames);
                }
            }
            // Re-posted rather than looping: a loop here would block the UI thread and the app would
            // stop delivering the very touches it is polling for.
            frameHandler.post(this);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Opt-in only, via `am start --ez tina.commitEmoji true`. Non-ASCII text cannot be injected from
        // a test harness -- adb synthesises key codes and cannot express a surrogate pair -- so this is
        // the only way to verify the emoji path without a human driving a real keyboard.
        commitEmojiOnFirstFrame = getIntent().getBooleanExtra("tina.commitEmoji", false);
        // Same reasoning as the emoji flag, one level further: a composing pass cannot be injected at all
        // from a harness -- key codes carry no composing region -- so this scripts one through the real
        // InputConnection rather than faking its result. `am start --ez tina.composeText true`.
        composeTextDiagnostics = getIntent().getBooleanExtra("tina.composeText", false);
        session = TinaNative.nativeCreateSession();
        // The session handle is logged at create and destroy on purpose. bgfx can only be initialised
        // once per process, so a second session is fatal to the engine -- and a second activity instance
        // is otherwise indistinguishable from a plain background/foreground cycle. Two onCreate lines
        // with no onDestroy between them is the signature of exactly that bug.
        android.util.Log.i("Tina", "onCreate session=0x" + Long.toHexString(session));
        // Emulators need the GLES fallback: the SDK emulator's Vulkan implementation segfaults during
        // swapchain creation. Detected rather than hard-coded so a real device still gets the default,
        // which prefers Vulkan.
        if (isEmulator()) {
            TinaNative.nativeSetPreferOpenGles(session, true);
        }
        surfaceView = new TinaSurfaceView(this, session);
        setContentView(surfaceView);
        frameHandler = new Handler(Looper.getMainLooper());
    }

    @Override
    protected void onResume() {
        super.onResume();
        running = true;
        frameHandler.post(frameTick);
    }

    @Override
    protected void onPause() {
        // Stop ticking before the window goes away. Android does not stop delivering frames on its
        // own -- docs record cocos2d-x leaving its CADisplayLink running in the background, waking 60
        // times a second for nothing.
        running = false;
        frameHandler.removeCallbacks(frameTick);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        // Logged because an activity recreate looks identical to a plain background/foreground cycle
        // from the native side, yet it destroys the engine session -- and bgfx cannot be initialised
        // twice in one process, so the second bind then fails with no obvious cause.
        android.util.Log.i("Tina", "onDestroy session=0x" + Long.toHexString(session));
        running = false;
        if (frameHandler != null) {
            frameHandler.removeCallbacks(frameTick);
        }
        // After the session is gone every native handle is dangling, so nothing may poll afterwards.
        TinaNative.nativeDestroySession(session);
        session = 0;
        super.onDestroy();
    }

    /**
     * Periodically reports engine progress to logcat.
     *
     * <p>This is the only way to tell a running engine from a stalled one: the screen shows a flat
     * clear colour either way. Sampled rather than logged per frame, which would flood logcat and skew
     * the very frame timing being reported.
     */
    private void logProgress(int frameUpdates) {
        if (frameUpdates > 0 && frameUpdates % 60 == 0 && frameUpdates != lastLoggedFrame) {
            lastLoggedFrame = frameUpdates;
            android.util.Log.i(
                    "Tina",
                    "frameUpdates=" + frameUpdates
                            + " fixedUpdates=" + TinaNative.nativeFixedUpdateCount(session)
                            + " uiUpdates=" + TinaNative.nativeUiUpdateCount(session)
                            + " pulseOn=" + TinaNative.nativeUiPulseOn(session)
                            + " presses=" + TinaNative.nativePointerPressCount(session)
                            + " releases=" + TinaNative.nativePointerReleaseCount(session)
                            + " keys=" + TinaNative.nativeKeyPressCount(session)
                            + " textCommits=" + TinaNative.nativeTextCommitCount(session)
                            + " composition=" + describeCompositionCounts()
                            + " preeditDrawn=" + TinaNative.nativeUiPreeditActive(session)
                            + " editCodepoints=" + TinaNative.nativeTextEditCodepointCount(session)
                            + " droppedTouches=" + TinaNative.nativeDroppedTouchEventCount(session)
                            + " droppedKeys=" + TinaNative.nativeDroppedKeyEventCount(session));
        }
    }

    /**
     * The four composition stage counts, unpacked.
     *
     * <p>Logged separately rather than as one total because each imbalance names a different defect:
     * started running ahead of ended+cancelled means a preedit is stuck on screen, ended without started
     * means the session missed the beginning, and cancelled climbing alone means the IME keeps taking and
     * dropping the region.
     */
    private String describeCompositionCounts() {
        final long packed = TinaNative.nativeCompositionCounts(session);
        return ((packed >>> 48) & 0xFFFF)
                + "/" + ((packed >>> 32) & 0xFFFF)
                + "/" + ((packed >>> 16) & 0xFFFF)
                + "/" + (packed & 0xFFFF)
                + "(start/update/end/cancel)";
    }

    /**
     * Whether this is an emulator, which decides the renderer fallback.
     *
     * <p>Checks the ranchu/goldfish hardware names rather than a Build.FINGERPRINT substring, because
     * that is what actually identifies the emulator's Vulkan driver ({@code vulkan.ranchu.so}) -- the
     * component that segfaults during swapchain creation.
     */
    private static boolean isEmulator() {
        final String hardware = android.os.Build.HARDWARE;
        return hardware != null && (hardware.contains("ranchu") || hardware.contains("goldfish"));
    }

    /** Only Java can call InputMethodManager, so the engine's latched intent is performed here. */
    private void applyPendingSoftKeyboardRequest() {
        final int request = TinaNative.nativeTakePendingSoftKeyboardRequest(session);
        if (request == TinaNative.KEYBOARD_REQUEST_NONE) {
            return;
        }
        final InputMethodManager ime =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (ime == null || surfaceView == null) {
            return;
        }
        if (request == TinaNative.KEYBOARD_REQUEST_SHOW) {
            surfaceView.requestFocus();
            ime.showSoftInput(surfaceView, InputMethodManager.SHOW_IMPLICIT);
        } else {
            ime.hideSoftInputFromWindow(surfaceView.getWindowToken(), 0);
        }
    }

    /**
     * Measures how much of the window the IME covers and hands it to the engine.
     *
     * <p>Measured rather than derived: the height depends on the keyboard app, the language, whether a
     * suggestion strip is showing, and split-screen geometry, so the engine cannot compute it.
     */
    private void reportSoftKeyboardOcclusion() {
        final View root = getWindow().getDecorView();
        final Rect visible = new Rect();
        root.getWindowVisibleDisplayFrame(visible);
        final int occluded = Math.max(0, root.getHeight() - visible.bottom);
        TinaNative.nativeOnSoftKeyboardOcclusion(session, occluded);
    }

    /**
     * Tells the IME where the caret is, so its candidate window can follow it.
     *
     * <p>This is Android's actual caret protocol, and it is nothing like IMM32: there is no window to
     * position from the app side, because the candidate window belongs to the IME process. The app
     * reports geometry and the IME decides.
     *
     * <p>Only sent when the IME asked via {@code requestCursorUpdates}. Sending unconditionally would
     * allocate a {@link CursorAnchorInfo} every frame for the majority of IMEs that never ask, and
     * Android's contract is a request/report pair rather than a broadcast.
     *
     * <p>Whether a given keyboard actually asks is not under this app's control -- so this path being
     * exercised at all depends on the installed IME. See docs/testing.md.
     */
    private void reportCursorAnchorInfo() {
        if (!TinaNative.nativeCursorUpdatesRequested(session)) {
            return;
        }
        final long packed = TinaNative.nativeCaretPixels(session);
        if (packed < 0) {
            // No focused caret. Nothing is reported rather than a zero rectangle, which the IME would
            // read as a real caret in the window's top-left corner and place its candidates there.
            return;
        }
        final InputMethodManager ime = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (ime == null || surfaceView == null) {
            return;
        }
        final float x = (packed >>> 48) & 0xFFFF;
        final float y = (packed >>> 32) & 0xFFFF;
        // Width is deliberately not unpacked. An insertion marker is a vertical line -- CursorAnchorInfo
        // has no field for a caret's thickness -- so there is nowhere to put it. The engine still reports
        // it because the desktop IMM32 path uses it, and dropping it here is the right end to drop it at.
        final float height = packed & 0xFFFF;

        // The engine reports view-local pixels, and they are handed over as-is with a view-to-screen
        // matrix, rather than being pre-offset into screen space.
        //
        // Not a style choice: Builder.build() throws IllegalArgumentException("Coordinate transformation
        // matrix is required when positional parameters are specified") if any position was set without a
        // matrix. Measured on a device -- the app died the first time a real IME asked for cursor updates,
        // which is later than any of the scripted diagnostics reach, so nothing before that point had
        // exercised this path at all.
        //
        // Supplying the matrix is also the correct half to fix: the IME applies it itself, so a later
        // scrolled or transformed surface stays right, whereas a pre-baked screen offset would silently
        // go stale.
        final int[] viewOnScreen = new int[2];
        surfaceView.getLocationOnScreen(viewOnScreen);
        final Matrix viewToScreen = new Matrix();
        viewToScreen.setTranslate(viewOnScreen[0], viewOnScreen[1]);

        final CursorAnchorInfo info = new CursorAnchorInfo.Builder()
                .setMatrix(viewToScreen)
                // Insertion marker rather than a character bounds entry: the engine reports one caret
                // rectangle, not per-glyph geometry, and claiming character bounds it does not have would
                // make the IME place candidates against positions that were never measured.
                //
                // baseline and bottom are both the caret's bottom edge: the engine measures a caret
                // rectangle, not a text baseline, and inventing an offset between them would tell the IME
                // about typography that was never computed.
                .setInsertionMarkerLocation(
                        x, y, y + height, y + height, CursorAnchorInfo.FLAG_HAS_VISIBLE_REGION)
                .build();
        ime.updateCursorAnchorInfo(surfaceView, info);
    }
}
