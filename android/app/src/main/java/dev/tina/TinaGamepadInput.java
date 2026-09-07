package dev.tina;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Handler;
import android.os.Looper;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/** Main-thread owner of native device subscriptions. Tina control mapping stays in C++. */
final class TinaGamepadInput implements InputManager.InputDeviceListener {
    private static final int MAXIMUM_CONTROLLERS = 16;
    private final long session;
    private final InputManager inputManager;
    private final SparseArray<InputDevice> connected = new SparseArray<>();
    private boolean active;

    TinaGamepadInput(Context context, long session) {
        this.session = session;
        inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
    }

    void start() {
        if (active || session == 0 || inputManager == null) { return; }
        active = true;
        inputManager.registerInputDeviceListener(this, new Handler(Looper.getMainLooper()));
        enumerateDevices();
    }

    void stop() {
        if (!active) { return; }
        active = false;
        inputManager.unregisterInputDeviceListener(this);
        for (int index = 0; index < connected.size(); ++index) {
            disconnect(connected.keyAt(index));
        }
        connected.clear();
    }

    void serviceResync() {
        if (active && TinaNative.nativeTakeGamepadResyncRequest(session)) {
            // A request can also follow surface loss without retiring the native registry.
            // Retire the old enumeration before replacing it, including removed devices.
            for (int index = 0; index < connected.size(); ++index) { disconnect(connected.keyAt(index)); }
            connected.clear();
            enumerateDevices();
        }
    }

    private static boolean isController(InputDevice device) {
        return device != null && !device.isVirtual() &&
                (device.supportsSource(InputDevice.SOURCE_GAMEPAD) ||
                 device.supportsSource(InputDevice.SOURCE_JOYSTICK) ||
                 (device.supportsSource(InputDevice.SOURCE_DPAD) &&
                  device.getKeyboardType() != InputDevice.KEYBOARD_TYPE_ALPHABETIC));
    }

    private void enumerateDevices() {
        for (int deviceId : InputDevice.getDeviceIds()) { connect(InputDevice.getDevice(deviceId)); }
    }

    private boolean connect(InputDevice device) {
        if (!active || !isController(device)) { return false; }
        if (connected.get(device.getId()) != null) { return true; }
        if (connected.size() >= MAXIMUM_CONTROLLERS) { return false; }
        if (!TinaNative.nativeOnGamepadConnected(session, device.getId(), device.getName(),
                device.getDescriptor(), device.getVendorId())) {
            android.util.Log.w("Tina", "gamepad connect could not be queued: device=" + device.getId());
            return false;
        }
        connected.put(device.getId(), device);
        return true;
    }

    private void disconnect(int deviceId) {
        if (!TinaNative.nativeOnGamepadDisconnected(session, deviceId)) {
            android.util.Log.w("Tina", "gamepad disconnect could not be queued: device=" + deviceId);
        }
    }

    @Override public void onInputDeviceAdded(int deviceId) {
        if (active) { connect(InputDevice.getDevice(deviceId)); }
    }

    @Override public void onInputDeviceRemoved(int deviceId) {
        if (!active) { return; }
        if (connected.get(deviceId) != null) {
            disconnect(deviceId);
            connected.remove(deviceId);
        }
        enumerateDevices();
    }

    @Override public void onInputDeviceChanged(int deviceId) {
        if (!active) { return; }
        if (connected.get(deviceId) != null) {
            disconnect(deviceId);
            connected.remove(deviceId);
        }
        connect(InputDevice.getDevice(deviceId));
    }

    boolean onKeyEvent(KeyEvent event) {
        final InputDevice device = event.getDevice();
        if (!isController(device)) { return false; }
        final int keyCode = event.getKeyCode();
        final boolean controllerSource = event.isFromSource(InputDevice.SOURCE_GAMEPAD) ||
                event.isFromSource(InputDevice.SOURCE_JOYSTICK) || event.isFromSource(InputDevice.SOURCE_DPAD);
        if (!controllerSource && !KeyEvent.isGamepadButton(keyCode)) { return false; }
        final boolean direction = keyCode == KeyEvent.KEYCODE_DPAD_UP || keyCode == KeyEvent.KEYCODE_DPAD_DOWN ||
                keyCode == KeyEvent.KEYCODE_DPAD_LEFT || keyCode == KeyEvent.KEYCODE_DPAD_RIGHT ||
                keyCode == KeyEvent.KEYCODE_DPAD_CENTER;
        if (!KeyEvent.isGamepadButton(keyCode) && !direction && keyCode != KeyEvent.KEYCODE_BACK) { return false; }
        if (!active) { return true; }
        serviceResync();
        if (!connect(device)) { return true; }
        if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() != 0) { return true; }
        if (event.getAction() != KeyEvent.ACTION_DOWN && event.getAction() != KeyEvent.ACTION_UP) { return true; }
        final boolean down = event.getAction() == KeyEvent.ACTION_DOWN;
        // Some older drivers only report digital triggers. Never let a duplicate key
        // override the analog axis when both representations are present.
        if (keyCode == KeyEvent.KEYCODE_BUTTON_L2 || keyCode == KeyEvent.KEYCODE_BUTTON_R2) {
            final boolean left = keyCode == KeyEvent.KEYCODE_BUTTON_L2;
            final int axis = left ? MotionEvent.AXIS_LTRIGGER : MotionEvent.AXIS_RTRIGGER;
            final int alias = left ? MotionEvent.AXIS_BRAKE : MotionEvent.AXIS_GAS;
            if (selectRange(device, axis, alias) == null) {
                TinaNative.nativeOnGamepadAxis(session, device.getId(), axis, down ? 1.0f : 0.0f);
            }
        } else {
            TinaNative.nativeOnGamepadButton(session, device.getId(), keyCode, down);
        }
        // Queue loss still consumes the native event: it must not also become a keyboard key.
        return true;
    }

    boolean onMotionEvent(MotionEvent event) {
        if (!event.isFromSource(InputDevice.SOURCE_JOYSTICK) &&
                !event.isFromSource(InputDevice.SOURCE_GAMEPAD)) { return false; }
        final InputDevice device = event.getDevice();
        if (!isController(device) || event.getActionMasked() != MotionEvent.ACTION_MOVE) { return false; }
        if (!active) { return true; }
        serviceResync();
        if (!connect(device)) { return true; }
        for (int history = 0; history < event.getHistorySize(); ++history) { sendAxes(device, event, history); }
        sendAxes(device, event, -1);
        return true;
    }

    private static InputDevice.MotionRange selectRange(InputDevice device, int primary, int alias) {
        final InputDevice.MotionRange range = controllerRange(device, primary);
        return range != null ? range : controllerRange(device, alias);
    }

    private static InputDevice.MotionRange controllerRange(InputDevice device, int axis) {
        final InputDevice.MotionRange range = device.getMotionRange(axis, InputDevice.SOURCE_JOYSTICK);
        return range != null ? range : device.getMotionRange(axis, InputDevice.SOURCE_GAMEPAD);
    }

    private void sendAxes(InputDevice device, MotionEvent event, int history) {
        sendAxis(device, event, history, MotionEvent.AXIS_X, MotionEvent.AXIS_X, false);
        sendAxis(device, event, history, MotionEvent.AXIS_Y, MotionEvent.AXIS_Y, false);
        sendAxis(device, event, history, MotionEvent.AXIS_Z, MotionEvent.AXIS_RX, false);
        sendAxis(device, event, history, MotionEvent.AXIS_RZ, MotionEvent.AXIS_RY, false);
        sendAxis(device, event, history, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE, true);
        sendAxis(device, event, history, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS, true);
        sendAxis(device, event, history, MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_X, false);
        sendAxis(device, event, history, MotionEvent.AXIS_HAT_Y, MotionEvent.AXIS_HAT_Y, false);
    }

    private void sendAxis(InputDevice device, MotionEvent event, int history, int targetAxis,
            int alias, boolean trigger) {
        final InputDevice.MotionRange range = selectRange(device, targetAxis, alias);
        if (range == null) { return; }
        float value = history < 0 ? event.getAxisValue(range.getAxis())
                : event.getHistoricalAxisValue(range.getAxis(), history);
        if (Float.isNaN(value) || Float.isInfinite(value)) { return; }
        if (trigger) {
            final float extent = range.getMax() - range.getMin();
            value = extent > 0.0f ? (value - range.getMin()) / extent : 0.0f;
            value = Math.max(0.0f, Math.min(1.0f, value));
        } else {
            if (Math.abs(value) <= range.getFlat()) { value = 0.0f; }
            final float extent = value < 0.0f ? -range.getMin() : range.getMax();
            value = extent > 0.0f ? value / extent : 0.0f;
            value = Math.max(-1.0f, Math.min(1.0f, value));
        }
        TinaNative.nativeOnGamepadAxis(session, device.getId(), targetAxis, value);
    }
}
