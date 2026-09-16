package dev.tina;

import android.app.Activity;
import android.graphics.Matrix;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.view.Choreographer;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.inputmethod.CursorAnchorInfo;
import android.view.inputmethod.InputMethodManager;

/**
 * Minimal host that drives the engine's frame loop and services soft-keyboard intent.
 *
 * <p>Frames are driven from Choreographer on the UI thread rather than a render thread. That is enough
 * for the current slice -- there is no renderer wired up yet, so this proves the platform bridge and
 * nothing more. ADR 0032's D3 chose external frame driving precisely so this choice stays open: a
 * later renderer can move ticking to its own thread without changing the engine contract.
 */
public final class TinaActivity extends Activity {

    private long session;
    private TinaSurfaceView surfaceView;
    private TinaGamepadInput gamepadInput;
    private Choreographer choreographer;
    private boolean running;
    private boolean engineEnded;
    private boolean commitEmojiOnFirstFrame;
    private boolean composeTextDiagnostics;
    /** Whether the gallery is running, which decides what the progress log can meaningfully report. */
    private boolean useGallery;
    private int lastLoggedFrame = -1;

    /**
     * Drives one engine frame per display refresh.
     *
     * <p>{@link Choreographer}, not {@code Handler.post}. The handler version re-posted itself with no
     * delay, which is not a frame loop at all -- it ran as fast as the UI thread could dispatch, produced
     * frames the display would never show, and starved the same thread it depends on for touch delivery.
     * Choreographer is the display's own vsync signal, so the engine now runs at the panel's rate and
     * inherits variable refresh for free.
     *
     * <p>Still on the UI thread. That is deliberate for now: input arrives here, and moving ticking to
     * its own thread means the platform backend's owner thread changes, which is a real design decision
     * rather than a tweak. ADR 0032's D3 chose external frame driving precisely so that stays open.
     */
    private final Choreographer.FrameCallback frameTick = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            if (!running) {
                return;
            }
            if (!engineEnded && surfaceView != null && surfaceView.isSurfaceBound()) {
                gamepadInput.serviceResync();
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
                    serviceImeGeometry(frames);
                    reportSafeInsets();
                    logProgress(frames);
                }
            }
            // Re-posted rather than looping: a loop here would block the UI thread and the app would
            // stop delivering the very touches it is polling for.
            choreographer.postFrameCallback(this);
        }
    };

    /**
     * Reports IME geometry, but not on every frame.
     *
     * <p>Both calls are pure overhead while nothing is focused: the occlusion measurement walks the view
     * hierarchy for a visible-frame rectangle, and the cursor report crosses JNI and allocates a
     * {@link CursorAnchorInfo}. Neither changes at frame rate -- a keyboard appearing or a caret moving
     * are user-speed events -- so sampling them costs a fraction of the frame budget and loses nothing a
     * person can perceive.
     *
     * <p>Occlusion is also skipped entirely unless the report would differ, so a steady state costs one
     * rectangle read rather than a JNI call as well.
     */
    private void serviceImeGeometry(int frames) {
        if (frames % ImeGeometryIntervalFrames != 0) {
            return;
        }
        reportSoftKeyboardOcclusion();
        reportCursorAnchorInfo();
    }

    /**
     * How often IME geometry is sampled. Roughly every 100 ms at 60Hz, which is well inside the latency a
     * keyboard animation or a caret move is perceived at.
     */
    private static final int ImeGeometryIntervalFrames = 6;

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
        gamepadInput = new TinaGamepadInput(this, session);
        // The session handle is logged at create and destroy on purpose. bgfx can only be initialised
        // once per process, so a second session is fatal to the engine -- and a second activity instance
        // is otherwise indistinguishable from a plain background/foreground cycle. Two onCreate lines
        // with no onDestroy between them is the signature of exactly that bug.
        android.util.Log.i("Tina", "onCreate session=0x" + Long.toHexString(session));
        // Match the swapchain to an SDR window. A wide-gamut Activity plus a
        // UNORM Vulkan backbuffer presents crushed/black UI on some devices.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            getWindow().setColorMode(android.content.pm.ActivityInfo.COLOR_MODE_DEFAULT);
        }
        // Default: emulator uses GLES (ranchu Vulkan historically dies in
        // swapchain create). Physical devices keep Automatic → Vulkan.
        // `tina.vulkan=true` forces Vulkan even on emulator so that path can
        // be tested. `tina.gles=true` forces GLES on a physical device.
        final boolean forceVulkan = getIntent().getBooleanExtra("tina.vulkan", false);
        if (!forceVulkan && (isEmulator() || getIntent().getBooleanExtra("tina.gles", false))) {
            TinaNative.nativeSetPreferOpenGles(session, true);
        }
        android.util.Log.i("Tina", "renderer preference forceVulkan=" + forceVulkan
                + " emulator=" + isEmulator());
        // The browsable gallery, opt-in via `am start --ez tina.gallery true`. Must be set before the
        // first surface binds, because that is when the application is built. The telemetry demo stays
        // the default so the device evidence the counters provide is not traded away.
        useGallery = getIntent().getBooleanExtra("tina.gallery", false);
        if (useGallery) {
            TinaNative.nativeSetUseGallery(session, true);
        }
        // Before the surface binds, because that is when the UI context is built.
        final String fontPath = findSystemFont();
        if (fontPath != null) {
            TinaNative.nativeSetUiFontPath(session, fontPath);
        } else {
            android.util.Log.w("Tina", "no system font found; UI text will draw as solid blocks");
        }
        // Also before the surface binds, because that is when the engine config is built.
        final String contentRoot = extractContent();
        if (contentRoot != null) {
            TinaNative.nativeSetContentRootPath(session, contentRoot);
        }
        surfaceView = new TinaSurfaceView(this, session);
        setContentView(surfaceView);
        // Bound to this thread's Looper, which is why it is fetched here rather than held statically:
        // Choreographer.getInstance() is per-thread and would throw off a thread without a Looper.
        choreographer = Choreographer.getInstance();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Re-applied on every resume, not once in onCreate: the system restores the bars whenever the
        // window loses focus, so a single call at startup is undone by the first notification shade pull
        // or app switch.
        applyEdgeToEdgeWindow();
        gamepadInput.start();
        TinaNative.nativeOnResume(session);
        running = true;
        choreographer.postFrameCallback(frameTick);
    }

    /**
     * Lays the surface out edge to edge, keeping a transparent status bar on top of it.
     *
     * <p>The status bar is deliberately <em>not</em> hidden. Hiding it is what forced games into
     * reading insets and applying negative offsets: with the bar gone the window is the display, but
     * the moment anything shows it again the surface is pushed below it and every coordinate the game
     * computed is wrong. Keeping it visible and transparent means one geometry for both cases -- the
     * engine draws to the physical top of the screen and the system icons composite over it.
     *
     * <p>Three things are required together, and any one missing reproduces the same symptom:
     * {@code decorFitsSystemWindows=false} stops the system from shrinking the window to fit the bars,
     * a transparent status bar colour stops it from painting an opaque strip over the result, and
     * {@code FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS} is what makes that colour take effect at all. Without
     * the first, the surface starts below the bar; without the others, the top of the game is a solid
     * block of colour.
     *
     * <p>The navigation bar <em>is</em> hidden, transiently. It sits over content a game is using,
     * has no icons worth compositing, and {@code BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE} keeps the
     * back gesture one swipe away -- hiding it permanently is what traps users in an app.
     *
     * <p>Two implementations because {@code WindowInsetsController} is API 30 and {@code minSdk} is 24.
     * Calling it unconditionally would throw {@code NoSuchMethodError} on every device below 30 --
     * a crash on exactly the older hardware that cannot be tested here.
     */
    private void applyEdgeToEdgeWindow() {
        final android.view.Window window = getWindow();
        // Required for setStatusBarColor to do anything, on every API level that has it.
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        // The deprecated translucent flags force the system's own scrim back on and override the
        // transparent colour below, so they are cleared rather than merely left unset.
        window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS
                | android.view.WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION);
        window.setStatusBarColor(android.graphics.Color.TRANSPARENT);
        window.setNavigationBarColor(android.graphics.Color.TRANSPARENT);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            final WindowInsetsController insets = window.getInsetsController();
            if (insets == null) {
                return;
            }
            insets.hide(WindowInsets.Type.navigationBars());
            insets.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            return;
        }
        // Pre-30 path. Deprecated on newer releases, which is why it is confined to this branch.
        //
        // LAYOUT_FULLSCREEN and LAYOUT_HIDE_NAVIGATION are the pre-30 spelling of
        // decorFitsSystemWindows=false: they lay the window out as if the bars were absent while
        // leaving the status bar itself on screen. FULLSCREEN is deliberately absent -- that is the
        // flag that would hide the status bar and reintroduce the split geometry above.
        surfaceView.setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        // Immersive *sticky*: the navigation bar comes back transiently on a swipe and
                        // hides again on its own, so the user is never stranded without the back gesture.
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    protected void onPause() {
        gamepadInput.stop();
        // Stop mixing before the activity is no longer visible. Stopping Choreographer alone
        // leaves AAudio/OpenSL running in the background.
        TinaNative.nativeOnPause(session);
        // Stop ticking before the window goes away. Android does not stop delivering frames on its
        // own -- docs record cocos2d-x leaving its CADisplayLink running in the background, waking 60
        // times a second for nothing.
        running = false;
        choreographer.removeFrameCallback(frameTick);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        // Logged because an activity recreate looks identical to a plain background/foreground cycle
        // from the native side, yet it destroys the engine session -- and bgfx cannot be initialised
        // twice in one process, so the second bind then fails with no obvious cause.
        android.util.Log.i("Tina", "onDestroy session=0x" + Long.toHexString(session));
        running = false;
        if (choreographer != null) {
            choreographer.removeFrameCallback(frameTick);
        }
        // After the session is gone every native handle is dangling, so nothing may poll afterwards.
        if (gamepadInput != null) { gamepadInput.stop(); }
        TinaNative.nativeDestroySession(session);
        session = 0;
        super.onDestroy();
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        return (gamepadInput != null && gamepadInput.onKeyEvent(event)) || super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        return (gamepadInput != null && gamepadInput.onMotionEvent(event)) || super.dispatchGenericMotionEvent(event);
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
            if (useGallery) {
                // The gallery is a different IGameApplication, so *every* counter below reads zero -- they
                // all come from the telemetry demo's own state, including fixedUpdates and uiUpdates.
                // Logging them anyway would print a wall of zeros that reads exactly like a broken input
                // bridge. The frame count is the one number that still means something here, because
                // nativePollFrame returns it from the engine rather than from the demo.
                android.util.Log.i("Tina", "gallery frame=" + frameUpdates);
                return;
            }
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
                            + " droppedKeys=" + TinaNative.nativeDroppedKeyEventCount(session)
                            + " content=" + describeContentCounts());
        }
    }

    /**
     * The content chain's outcome, unpacked.
     *
     * <p>Logged as three fields rather than one verdict because each combination names a different
     * defect: all zero means nothing was packaged, entries without a load means the manifest was readable
     * but its objects were not, and a load with {@code extentOk=false} means an asset parsed but is not
     * the one the recipe cooked.
     */
    private String describeContentCounts() {
        final long packed = TinaNative.nativeContentCounts(session);
        return ((packed >>> 32) & 0xFFFF)
                + "/" + ((packed >>> 16) & 0xFFFF)
                + "/" + ((packed & 1) != 0)
                + "(entries/loaded/extentOk)";
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
     * Copies the APK's {@code assets/} tree into a real directory and returns it, or null when the app
     * ships no assets.
     *
     * <p>Why copy at all: APK assets are entries in a zip, not files. The engine reads content through
     * one function backed by {@code std::ifstream}, and the NDK's {@code ifstream} cannot see inside an
     * APK -- {@code AAssetManager} is the only native way in, and it is a different API with no
     * {@code FILE*} to hand out. Extracting once means desktop, browser and device all load through the
     * same path, so a content bug is never platform-specific by construction.
     *
     * <p>Destination is {@code getFilesDir()/content}: app-private, survives reboots, and is cleared on
     * uninstall. Not the cache directory, which the system may delete between launches, and not external
     * storage, which needs a permission and may be absent.
     *
     * <p>Re-extracts whenever the installed version changes, tracked by a stamp file holding the version
     * code. Skipping that check would leave an updated APK running the previous release's assets, which
     * presents as content that ignores the update -- much harder to recognise than a slow first launch.
     */
    private String extractContent() {
        final java.io.File destination = new java.io.File(getFilesDir(), "content");
        final java.io.File stamp = new java.io.File(destination, ".version");
        final String version = installedVersion();
        if (destination.isDirectory() && version.equals(readStamp(stamp))) {
            return destination.getAbsolutePath();
        }
        // A stale tree is deleted rather than merged into: a file removed from the new APK would
        // otherwise survive forever and keep loading.
        deleteRecursively(destination);
        try {
            if (!copyAssetDirectory("", destination)) {
                // No assets/ in the APK. Not an error -- the telemetry demo ships none -- and returning
                // null leaves the engine's content root empty, which is the honest description.
                deleteRecursively(destination);
                return null;
            }
            writeStamp(stamp, version);
            return destination.getAbsolutePath();
        } catch (final java.io.IOException exception) {
            // Partial output is worse than none: it would satisfy the isDirectory() check above on the
            // next launch and pin the app to a half-extracted tree.
            deleteRecursively(destination);
            android.util.Log.e("Tina", "extracting content failed", exception);
            return null;
        }
    }

    /**
     * Copies one asset directory recursively. Returns false when {@code assetPath} holds nothing.
     *
     * <p>{@code AssetManager.list} cannot distinguish an empty directory from a file, so a leaf is
     * detected by trying to open it. That is also why an empty directory in the APK simply does not
     * appear in the output -- the packager drops those anyway.
     */
    private boolean copyAssetDirectory(String assetPath, java.io.File destination)
            throws java.io.IOException {
        final android.content.res.AssetManager assets = getAssets();
        final String[] entries = assets.list(assetPath);
        if (entries == null || entries.length == 0) {
            return false;
        }
        if (!destination.isDirectory() && !destination.mkdirs()) {
            throw new java.io.IOException("could not create " + destination);
        }
        for (final String entry : entries) {
            final String childAssetPath = assetPath.isEmpty() ? entry : assetPath + "/" + entry;
            final java.io.File childDestination = new java.io.File(destination, entry);
            if (!copyAssetDirectory(childAssetPath, childDestination)) {
                copyAssetFile(childAssetPath, childDestination);
            }
        }
        return true;
    }

    private void copyAssetFile(String assetPath, java.io.File destination) throws java.io.IOException {
        final java.io.File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new java.io.IOException("could not create " + parent);
        }
        try (java.io.InputStream input = getAssets().open(assetPath);
             java.io.OutputStream output = new java.io.FileOutputStream(destination)) {
            final byte[] buffer = new byte[64 * 1024];
            int read;
            while ((read = input.read(buffer)) != -1) {
                output.write(buffer, 0, read);
            }
        }
    }

    /** The installed version code, as the re-extraction key. */
    private String installedVersion() {
        try {
            final android.content.pm.PackageInfo info =
                getPackageManager().getPackageInfo(getPackageName(), 0);
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                return Long.toString(info.getLongVersionCode());
            }
            return Integer.toString(info.versionCode);
        } catch (final android.content.pm.PackageManager.NameNotFoundException exception) {
            // Cannot happen for the running package, but a wrong answer here must not silently skip
            // re-extraction, so fall back to a value that never matches a stamp.
            return "unknown";
        }
    }

    private static String readStamp(java.io.File stamp) {
        try (java.io.BufferedReader reader = new java.io.BufferedReader(
                new java.io.InputStreamReader(new java.io.FileInputStream(stamp), "UTF-8"))) {
            final String line = reader.readLine();
            return line == null ? "" : line.trim();
        } catch (final java.io.IOException exception) {
            return "";
        }
    }

    private static void writeStamp(java.io.File stamp, String version) throws java.io.IOException {
        try (java.io.Writer writer = new java.io.OutputStreamWriter(
                new java.io.FileOutputStream(stamp), "UTF-8")) {
            writer.write(version);
        }
    }

    private static void deleteRecursively(java.io.File file) {
        final java.io.File[] children = file.listFiles();
        if (children != null) {
            for (final java.io.File child : children) {
                deleteRecursively(child);
            }
        }
        file.delete();
    }

    /**
     * Finds a usable UI font in the system font directory, or null.
     *
     * <p>Read from the device rather than shipped in the APK: /system/fonts always has a Latin sans-serif,
     * so bundling one would add a megabyte to every build to duplicate a file already present. It also
     * means the app inherits whatever the device actually uses.
     *
     * <p>Candidates are tried in order rather than guessing one name. Roboto is the modern Android
     * default, DroidSans is what older images and the SDK emulator ship, and the {@code -Regular} variants
     * cover devices that split the family into per-weight files. The final fallback is any .ttf at all:
     * a wrong-looking font is far better than a screen of solid blocks.
     */
    private static String findSystemFont() {
        final String[] candidates = {
            "/system/fonts/Roboto-Regular.ttf",
            "/system/fonts/DroidSans.ttf",
            "/system/fonts/NotoSans-Regular.ttf",
            "/system/fonts/DroidSansFallback.ttf",
        };
        for (final String candidate : candidates) {
            if (new java.io.File(candidate).canRead()) {
                return candidate;
            }
        }
        // Nothing matched a known name. Take the first readable .ttf, because a vendor font is still text.
        final java.io.File[] fonts = new java.io.File("/system/fonts").listFiles();
        if (fonts != null) {
            for (final java.io.File font : fonts) {
                if (font.canRead() && font.getName().endsWith(".ttf")) {
                    return font.getAbsolutePath();
                }
            }
        }
        return null;
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

    /**
     * Only Java can call InputMethodManager, so the engine's latched intent is performed here.
     *
     * <p>Read, act, then acknowledge -- not read-and-consume. InputMethodManager can be absent, and
     * {@code hideSoftInputFromWindow} needs a window token the view may not hold yet, so an intent that
     * was consumed at read time would be gone with nothing having happened. Leaving it latched means the
     * next frame retries, which is what makes "the keyboard appears once the window is ready" work.
     *
     * <p>Acknowledged only on the reported outcome of the IME call, and natively only if the latch still
     * holds that same request, so a newer opposite intent cannot be erased by this acknowledgement.
     */
    private void applyPendingSoftKeyboardRequest() {
        final int request = TinaNative.nativePendingSoftKeyboardRequest(session);
        if (request == TinaNative.KEYBOARD_REQUEST_NONE) {
            return;
        }
        final InputMethodManager ime =
                (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (ime == null || surfaceView == null) {
            return;
        }
        final boolean applied;
        if (request == TinaNative.KEYBOARD_REQUEST_SHOW) {
            surfaceView.requestFocus();
            applied = ime.showSoftInput(surfaceView, InputMethodManager.SHOW_IMPLICIT);
        } else {
            // A null token is the "no window yet" case the retry exists for, and passing it would throw.
            final android.os.IBinder token = surfaceView.getWindowToken();
            applied = token != null && ime.hideSoftInputFromWindow(token, 0);
        }
        if (applied) {
            TinaNative.nativeAcknowledgeSoftKeyboardRequest(session, request);
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
        final int occluded;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Read from the IME inset rather than the visible frame, because
            // applyEdgeToEdgeWindow sets decorFitsSystemWindows=false: the window is then no longer
            // resized for the keyboard, so getWindowVisibleDisplayFrame spans the full display and the
            // measurement below reports zero occlusion with a keyboard plainly on screen.
            //
            // The navigation bar inset is subtracted because the IME inset is measured from the bottom
            // of the window and includes the space the navigation bar occupies. That bar is hidden
            // here, so counting it would over-report by its height and push UI content too far up.
            final WindowInsets insets = root.getRootWindowInsets();
            if (insets == null) {
                return;
            }
            final int ime = insets.getInsets(WindowInsets.Type.ime()).bottom;
            final int navigation = insets.getInsets(WindowInsets.Type.navigationBars()).bottom;
            occluded = Math.max(0, ime - navigation);
        } else {
            final Rect visible = new Rect();
            root.getWindowVisibleDisplayFrame(visible);
            occluded = Math.max(0, root.getHeight() - visible.bottom);
        }
        // Only crossed when it changed. The value is constant for long stretches -- a keyboard is either
        // up or down -- so re-reporting it is a JNI call that provably cannot alter engine state.
        if (occluded == lastReportedOcclusion) {
            return;
        }
        lastReportedOcclusion = occluded;
        TinaNative.nativeOnSoftKeyboardOcclusion(session, occluded);
    }

    /**
     * Hands system-bar and cutout padding to the engine, in physical pixels.
     *
     * <p>This is the whole of what the engine learns about the bars, and it is why game code never
     * reads insets or offsets itself: the window always spans the display (see
     * {@link #applyEdgeToEdgeWindow}), and these four numbers say which part of it is under a system
     * bar or a cutout. The engine pads root UI content by them while leaving the root border box
     * covering the full window, so backgrounds reach the screen edge and controls do not.
     *
     * <p>The status bar is visible, so the top inset is normally its height. The navigation bar is
     * hidden, so its edge reports zero -- {@code getInsets} returns nothing for a hidden type, and a
     * transiently swiped bar does not alter insets either, so neither case makes layout thrash.
     *
     * <p>Physical pixels here on purpose. The native side divides by the reported content scale to get
     * the logical values UI layout uses, which keeps one conversion in one place rather than a density
     * multiply on each side of the JNI boundary.
     */
    private void reportSafeInsets() {
        final View root = getWindow().getDecorView();
        final WindowInsets insets = root.getRootWindowInsets();
        if (insets == null) {
            return;
        }
        final int left;
        final int top;
        final int right;
        final int bottom;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            final android.graphics.Insets sys =
                    insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            left = sys.left;
            top = sys.top;
            right = sys.right;
            bottom = sys.bottom;
        } else {
            left = insets.getSystemWindowInsetLeft();
            top = insets.getSystemWindowInsetTop();
            right = insets.getSystemWindowInsetRight();
            bottom = insets.getSystemWindowInsetBottom();
        }
        if (left == lastReportedInsetLeft && top == lastReportedInsetTop &&
                right == lastReportedInsetRight && bottom == lastReportedInsetBottom) {
            return;
        }
        lastReportedInsetLeft = left;
        lastReportedInsetTop = top;
        lastReportedInsetRight = right;
        lastReportedInsetBottom = bottom;
        TinaNative.nativeOnSafeInsets(session, left, top, right, bottom);
    }

    /** Last occlusion handed to the engine. -1 so the first report always goes through. */
    private int lastReportedOcclusion = -1;
    private int lastReportedInsetLeft = -1;
    private int lastReportedInsetTop = -1;
    private int lastReportedInsetRight = -1;
    private int lastReportedInsetBottom = -1;

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
     * <p>The two request bits are honoured separately. Either one makes this frame report; only IMMEDIATE
     * is then retired, and only after the report actually reached the IME, so a frame with no focused
     * caret or no InputMethodManager retries instead of swallowing the single report that was asked for.
     * MONITOR is left alone -- it ends when the IME cancels it with mode 0.
     *
     * <p>Whether a given keyboard actually asks is not under this app's control -- so this path being
     * exercised at all depends on the installed IME. See docs/testing.md.
     */
    private void reportCursorAnchorInfo() {
        final int mode = TinaNative.nativeCursorUpdateMode(session);
        if (mode == 0) {
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
        if ((mode & TinaNative.CURSOR_UPDATE_IMMEDIATE) != 0) {
            TinaNative.nativeAcknowledgeImmediateCursorUpdate(session);
        }
    }
}
