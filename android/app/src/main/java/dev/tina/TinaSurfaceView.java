package dev.tina;

import android.content.Context;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

/**
 * Feeds the engine a Surface and the touches that land on it.
 *
 * <p>Every callback here runs on the UI thread. It never touches engine state directly: touches go
 * into the C++ ring buffer, which the engine drains on its own thread. That is why no marshalling
 * Runnable is needed -- docs/platform-input.md records cocos2d-x allocating one per event, which is
 * real GC pressure under an event flood.
 */
public final class TinaSurfaceView extends SurfaceView implements SurfaceHolder.Callback {

    private final long session;
    private boolean surfaceBound;

    public TinaSurfaceView(Context context, long session) {
        super(context);
        this.session = session;
        getHolder().addCallback(this);
        // Without this the view never receives ACTION_DOWN, and every later pointer event with it.
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    public boolean isSurfaceBound() {
        return surfaceBound;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Deliberately empty: the extent is not final until surfaceChanged, and binding here would
        // hand the engine geometry that is about to change.
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        final Surface surface = holder.getSurface();
        if (surface == null || !surface.isValid()) {
            return;
        }
        final float density = getResources().getDisplayMetrics().density;
        // Called for the first bind and again for every replacement window; the native side decides
        // whether that means create or rebind.
        surfaceBound = TinaNative.nativeSurfaceCreated(session, surface, density);
        if (!surfaceBound) {
            // Logged because an unbound surface stops the frame loop silently: the engine simply never
            // ticks again, with no error anywhere. That cost a debugging round.
            android.util.Log.e("Tina", "surface bind failed (" + width + "x" + height + ")");
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceBound = false;
        TinaNative.nativeSurfaceDestroyed(session);
    }

    @Override
    public boolean onCheckIsTextEditor() {
        // Must be true or the IME never asks for an InputConnection, and the soft keyboard delivers
        // nothing -- it would appear on screen and typing would do nothing at all.
        return true;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        // TYPE_CLASS_TEXT with no flags: a plain text field. IME_ACTION_DONE so the keyboard shows a
        // confirm key rather than a newline, and IME_FLAG_NO_EXTRACT_UI so it does not replace the
        // engine's own rendering with a fullscreen extract view on small screens.
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT;
        outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI;
        return new TinaInputConnection(this, session);
    }

    /**
     * Commits a fixed emoji through the same path a real IME uses.
     *
     * <p>Exists because there is no way to inject non-ASCII text from a test harness: {@code adb shell
     * input text} synthesises key codes and cannot express a surrogate pair, and driving a real IME
     * needs a human. This routes through {@link TinaInputConnection#commitText} exactly as the keyboard
     * would, so it exercises the UTF-16 conversion rather than bypassing it.
     */
    void commitEmojiForDiagnostics() {
        // U+1F600 GRINNING FACE, written as the surrogate pair Java actually stores.
        onCreateInputConnection(new EditorInfo()).commitText("😀", 1);
    }

    /**
     * Advances a scripted composing pass by one step, through the same path a real IME uses.
     *
     * <p>Exists for the same reason {@link #commitEmojiForDiagnostics} does: a composing pass cannot be
     * injected from a test harness. {@code adb shell input text} synthesises key codes, which carry no
     * composing region at all, and driving a real Chinese or Japanese IME needs a human.
     *
     * <p>Calls {@code setComposingText} twice and then {@code commitText}, exactly as a pinyin keyboard
     * does, so it exercises the native session's Started/Updated/Ended sequence rather than bypassing it
     * to fabricate the outcome. It cannot prove any particular keyboard behaves this way; that still
     * needs a person.
     *
     * <p>One step per call, spread across frames, for two reasons: the preedit is then actually on screen
     * long enough to appear in a screenshot, and each stage lands in its own frame so the counters
     * separate instead of collapsing into one poll.
     *
     * <p>The connection is created once and reused, because a composing pass is stateful -- a fresh
     * {@link InputConnection} per step would leave {@code BaseInputConnection}'s own composing-region
     * tracking out of step with the region it thinks it owns.
     *
     * @return true while more steps remain
     */
    boolean advanceComposeDiagnostics() {
        if (diagnosticsConnection == null) {
            diagnosticsConnection = onCreateInputConnection(new EditorInfo());
        }
        switch (composeStep++) {
            case 0:
                diagnosticsConnection.setComposingText("ni", 1);
                return true;
            case 1:
                diagnosticsConnection.setComposingText("nihao", 1);
                return true;
            case 2:
                diagnosticsConnection.commitText("你好", 1);
                return false;
            default:
                return false;
        }
    }

    /** Reused across the scripted pass; see {@link #advanceComposeDiagnostics}. */
    private InputConnection diagnosticsConnection;

    private int composeStep;

    /**
     * {@code KeyEvent.KEYCODE_BACK}, spelled out to match how the rest of this file treats key codes:
     * they are raw platform integers that C++ translates, so this side names no key table.
     */
    private static final int KEYCODE_BACK = 4;

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        // getRepeatCount() > 0 is Android's own key-hold repeat. Forwarded rather than filtered here,
        // because held-key navigation depends on it and the engine's KeyTransition can express it.
        final boolean consumed =
                TinaNative.nativeOnKey(session, TinaNative.KEY_DOWN, keyCode, event.getRepeatCount() > 0);
        if (keyCode == KEYCODE_BACK) {
            // Back is claimed unconditionally, because the return value above says only "queued", not
            // "the game used it" -- keys cross into the engine asynchronously and are handled a frame
            // later, long after this method must answer.
            //
            // Letting it fall through meant Back did both things at once: measured on a device, the
            // gallery popped its scene *and* the activity went back to the launcher in the same press.
            //
            // The consequence is that the engine now owns leaving the app. A state with nowhere to go
            // back to must call requestExitAfterFrame, or Back does nothing and the user is stuck.
            return true;
        }
        // Unconsumed keys fall through to the system on purpose: swallowing Back would trap the user in
        // the app, and swallowing volume would break the device's own controls. The engine only claims
        // keys it actually mapped.
        return consumed || super.onKeyDown(keyCode, event);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        final boolean consumed = TinaNative.nativeOnKey(session, TinaNative.KEY_UP, keyCode, false);
        return consumed || super.onKeyUp(keyCode, event);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                // Only the pointer that changed, not every pointer in the event: ACTION_POINTER_DOWN
                // carries all live pointers, and re-reporting the others as Down would double-press
                // fingers that are already held.
                final int index = event.getActionIndex();
                TinaNative.nativeOnTouch(
                        session,
                        TinaNative.TOUCH_DOWN,
                        event.getPointerId(index),
                        event.getX(index),
                        event.getY(index));
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                // ACTION_MOVE has no action index: it reports every pointer at once, so all of them
                // must be forwarded or fingers other than the first would freeze in place.
                for (int index = 0; index < event.getPointerCount(); ++index) {
                    TinaNative.nativeOnTouch(
                            session,
                            TinaNative.TOUCH_MOVE,
                            event.getPointerId(index),
                            event.getX(index),
                            event.getY(index));
                }
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                final int index = event.getActionIndex();
                TinaNative.nativeOnTouch(
                        session,
                        TinaNative.TOUCH_UP,
                        event.getPointerId(index),
                        event.getX(index),
                        event.getY(index));
                return true;
            }
            case MotionEvent.ACTION_CANCEL: {
                // A cancel names no single pointer, so every live one is cancelled. Distinct from Up:
                // the interaction did not complete, so downstream must release capture without
                // treating it as a tap.
                for (int index = 0; index < event.getPointerCount(); ++index) {
                    TinaNative.nativeOnTouch(
                            session,
                            TinaNative.TOUCH_CANCEL,
                            event.getPointerId(index),
                            event.getX(index),
                            event.getY(index));
                }
                return true;
            }
            default:
                return super.onTouchEvent(event);
        }
    }
}
