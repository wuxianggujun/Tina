package dev.tina;

import android.view.View;
import android.view.inputmethod.BaseInputConnection;

/**
 * Receives committed text from the soft keyboard.
 *
 * <p>This exists because a soft keyboard does not produce key codes. Typing on it calls
 * {@code commitText} with whole strings -- autocomplete, paste and IME conversion all arrive as one
 * call -- so a host that only handled {@code onKeyDown} would see nothing at all while the user typed.
 *
 * <p>Extends {@link BaseInputConnection} in no-edit mode: the engine owns its own text state, so there
 * is no local editable buffer for the IME to manipulate.
 *
 * <p>Every method here forwards the raw call and keeps no composition state of its own. The mapping onto
 * Tina's four preedit stages lives natively, for the same reason the key table does: two copies of the
 * semantics on either side of JNI eventually disagree, and the failure is silent.
 */
final class TinaInputConnection extends BaseInputConnection {

    private final long session;

    TinaInputConnection(View targetView, long session) {
        // fullEditor=false: no local Editable. With true, BaseInputConnection would maintain its own
        // buffer that nothing reads, and the two copies of the text would drift.
        super(targetView, false);
        this.session = session;
    }

    @Override
    public boolean commitText(CharSequence text, int newCursorPosition) {
        if (text == null || text.length() == 0) {
            return super.commitText(text, newCursorPosition);
        }
        // The native queue publishes split commits atomically. Returning its result keeps a capacity or
        // validation failure visible to the IME instead of acknowledging text the engine never received.
        return TinaNative.nativeOnTextCommit(session, text.toString());
    }

    @Override
    public boolean setComposingText(CharSequence text, int newCursorPosition) {
        // Forwarded raw. Which of Tina's four stages this is -- Started, Updated, or (for an empty
        // string) Cancelled -- is decided natively, because the distinction is not in the Android call:
        // it depends on whether a pass is already in flight, which only one side should remember.
        //
        // Chinese and Japanese input depend entirely on this. Without it the user sees nothing until the
        // pass ends in a commitText, so the whole conversion happens invisibly.
        final boolean accepted = TinaNative.nativeOnComposingText(
                session, text == null ? null : text.toString(), newCursorPosition);
        // super still runs: BaseInputConnection tracks the composing region itself, and skipping it makes
        // the IME's own bookkeeping disagree with the region it thinks it owns.
        return super.setComposingText(text, newCursorPosition) && accepted;
    }

    @Override
    public boolean finishComposingText() {
        // The IME gave up the region without committing. Distinct from a commit and reported as such --
        // native maps it to Cancelled, because nothing was produced.
        final boolean accepted = TinaNative.nativeOnComposingFinish(session);
        return super.finishComposingText() && accepted;
    }

    @Override
    public boolean requestCursorUpdates(int cursorUpdateMode) {
        // Android's contract: report CursorAnchorInfo only after the IME asks. The mode carries two
        // independent bits -- IMMEDIATE wants one report now, MONITOR wants them until further notice --
        // and the host satisfies both by reporting per frame while either is set.
        final boolean wanted = cursorUpdateMode != 0;
        TinaNative.nativeSetCursorUpdatesRequested(session, wanted);
        // Claimed rather than delegated to super, which returns false: returning false tells the IME the
        // editor cannot report cursor positions, and it then never places its candidate window against
        // the caret at all.
        return wanted;
    }

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        // Translated to Backspace presses rather than a text edit, because the engine has no editable
        // buffer here to delete from -- the key path is what its TextEdit consumers already handle.
        for (int index = 0; index < beforeLength; ++index) {
            TinaNative.nativeOnKey(session, TinaNative.KEY_DOWN, KEYCODE_DEL, false);
            TinaNative.nativeOnKey(session, TinaNative.KEY_UP, KEYCODE_DEL, false);
        }
        return true;
    }

    /** KEYCODE_DEL, which Android names for backspace rather than forward delete. */
    private static final int KEYCODE_DEL = 67;
}
