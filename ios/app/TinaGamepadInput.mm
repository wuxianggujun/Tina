#import "TinaGamepadInput.h"

#import <GameController/GameController.h>
#include <tina/platform/ios/IosSession.hpp>

#include <array>
#include <string_view>

using namespace Tina::Platform;

namespace {

struct ControllerSlot final {
    GCController* controller = nil;
    std::array<bool, GamepadButtonCount> buttons{};
    std::array<float, GamepadAxisCount> axes{};
};

std::uintptr_t identity(GCController* controller) noexcept
{
    return reinterpret_cast<std::uintptr_t>((__bridge void*)controller);
}

std::string_view utf8View(NSString* string) noexcept
{
    const char* utf8 = string.UTF8String;
    return utf8 == nullptr ? std::string_view{} : std::string_view{utf8};
}

} // namespace

@implementation TinaGamepadInput {
    IosSession* _session;
    std::array<ControllerSlot, PlatformFrameBuilder::MaximumGamepadSlots> _slots;
    BOOL _active;
}

- (instancetype)initWithSession:(IosSession*)session
{
    self = [super init];
    if (self != nil) { _session = session; }
    return self;
}

- (void)start
{
    if (_active || _session == nullptr) { return; }
    _active = YES;
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    [center addObserver:self selector:@selector(controllerConnected:)
                  name:GCControllerDidConnectNotification object:nil];
    [center addObserver:self selector:@selector(controllerDisconnected:)
                  name:GCControllerDidDisconnectNotification object:nil];
    for (GCController* controller in GCController.controllers) { [self connectController:controller]; }
}

- (void)stop
{
    if (!_active) { return; }
    _active = NO;
    [NSNotificationCenter.defaultCenter removeObserver:self];
    for (auto& slot : _slots) {
        if (slot.controller == nil) { continue; }
        slot.controller.extendedGamepad.valueChangedHandler = nil;
        slot.controller.microGamepad.valueChangedHandler = nil;
        if (_session != nullptr && !_session->onGamepadDisconnected(identity(slot.controller))) {
            NSLog(@"Tina gamepad disconnect could not be queued");
        }
        slot = {};
    }
}

- (void)invalidate
{
    [self stop];
    _session = nullptr;
}

- (void)dealloc
{
    [self invalidate];
}

- (void)serviceResync
{
    if (_active && _session != nullptr && _session->takeGamepadResyncRequest()) {
        [self stop];
        [self start];
    }
}

- (void)controllerConnected:(NSNotification*)notification
{
    if (!NSThread.isMainThread) {
        __weak TinaGamepadInput* weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf controllerConnected:notification]; });
        return;
    }
    [self connectController:(GCController*)notification.object];
}

- (void)controllerDisconnected:(NSNotification*)notification
{
    if (!NSThread.isMainThread) {
        __weak TinaGamepadInput* weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{ [weakSelf controllerDisconnected:notification]; });
        return;
    }
    GCController* controller = (GCController*)notification.object;
    for (auto& slot : _slots) {
        if (slot.controller != controller) { continue; }
        controller.extendedGamepad.valueChangedHandler = nil;
        controller.microGamepad.valueChangedHandler = nil;
        if (_session != nullptr && !_session->onGamepadDisconnected(identity(controller))) {
            NSLog(@"Tina gamepad disconnect could not be queued");
        }
        slot = {};
        break;
    }
    for (GCController* remaining in GCController.controllers) { [self connectController:remaining]; }
}

- (void)connectController:(GCController*)controller
{
    if (!_active || _session == nullptr || controller == nil ||
        ![GCController.controllers containsObject:controller] ||
        (controller.extendedGamepad == nil && controller.microGamepad == nil)) { return; }
    ControllerSlot* available = nullptr;
    for (auto& slot : _slots) {
        if (slot.controller == controller) { return; }
        if (slot.controller == nil && available == nullptr) { available = &slot; }
    }
    if (available == nullptr) { return; }
    NSString* model = @"";
    if (@available(iOS 13.0, *)) { model = controller.productCategory; }
    if (!_session->onGamepadConnected(identity(controller), utf8View(controller.vendorName), utf8View(model))) {
        NSLog(@"Tina gamepad connect could not be queued");
        return;
    }
    available->controller = controller;
    // All callbacks share the UIKit producer. Weak captures avoid a controller/profile/block cycle.
    controller.handlerQueue = dispatch_get_main_queue();
    __weak TinaGamepadInput* weakSelf = self;
    __weak GCController* weakController = controller;
    if (controller.extendedGamepad != nil) {
        controller.extendedGamepad.valueChangedHandler = ^(GCExtendedGamepad*, GCControllerElement*) {
            [weakSelf sampleController:weakController];
        };
    } else {
        controller.microGamepad.reportsAbsoluteDpadValues = YES;
        controller.microGamepad.valueChangedHandler = ^(GCMicroGamepad*, GCControllerElement*) {
            [weakSelf sampleController:weakController];
        };
    }
    [self sampleController:controller];
}

- (void)sampleController:(GCController*)controller
{
    if (!_active || _session == nullptr || controller == nil) { return; }
    [self serviceResync];
    ControllerSlot* previous = nullptr;
    for (auto& slot : _slots) {
        if (slot.controller == controller) { previous = &slot; break; }
    }
    if (previous == nullptr) { return; }
    std::array<bool, GamepadButtonCount> buttons{};
    std::array<float, GamepadAxisCount> axes{};
    const auto button = [&](GamepadButton target, GCControllerButtonInput* source) {
        buttons[static_cast<Tina::usize>(target)] = source.isPressed;
    };
    const auto axis = [&](GamepadAxis target, float value) { axes[static_cast<Tina::usize>(target)] = value; };
    GCControllerDirectionPad* dpad = nil;
    if (GCExtendedGamepad* pad = controller.extendedGamepad; pad != nil) {
        button(GamepadButton::South, pad.buttonA);
        button(GamepadButton::East, pad.buttonB);
        button(GamepadButton::West, pad.buttonX);
        button(GamepadButton::North, pad.buttonY);
        button(GamepadButton::LeftBumper, pad.leftShoulder);
        button(GamepadButton::RightBumper, pad.rightShoulder);
        if (@available(iOS 12.1, *)) {
            button(GamepadButton::LeftStick, pad.leftThumbstickButton);
            button(GamepadButton::RightStick, pad.rightThumbstickButton);
        }
        if (@available(iOS 13.0, *)) {
            button(GamepadButton::Start, pad.buttonMenu);
            button(GamepadButton::Back, pad.buttonOptions);
        }
        if (@available(iOS 14.0, *)) { button(GamepadButton::Guide, pad.buttonHome); }
        axis(GamepadAxis::LeftX, pad.leftThumbstick.xAxis.value);
        axis(GamepadAxis::LeftY, -pad.leftThumbstick.yAxis.value);
        axis(GamepadAxis::RightX, pad.rightThumbstick.xAxis.value);
        axis(GamepadAxis::RightY, -pad.rightThumbstick.yAxis.value);
        axis(GamepadAxis::LeftTrigger, pad.leftTrigger.value);
        axis(GamepadAxis::RightTrigger, pad.rightTrigger.value);
        dpad = pad.dpad;
    } else if (GCMicroGamepad* pad = controller.microGamepad; pad != nil) {
        button(GamepadButton::South, pad.buttonA);
        button(GamepadButton::West, pad.buttonX);
        if (@available(iOS 13.0, *)) { button(GamepadButton::Start, pad.buttonMenu); }
        dpad = pad.dpad;
    }
    if (dpad != nil) {
        button(GamepadButton::DpadUp, dpad.up);
        button(GamepadButton::DpadRight, dpad.right);
        button(GamepadButton::DpadDown, dpad.down);
        button(GamepadButton::DpadLeft, dpad.left);
    }
    bool queued = true;
    for (Tina::usize index = 0; index < buttons.size(); ++index) {
        if (buttons[index] == previous->buttons[index]) { continue; }
        if (_session->onGamepadButton(identity(controller), static_cast<GamepadButton>(index),
                buttons[index] ? DigitalTransition::Down : DigitalTransition::Up)) {
            previous->buttons[index] = buttons[index];
        } else { queued = false; }
    }
    for (Tina::usize index = 0; index < axes.size(); ++index) {
        if (axes[index] == previous->axes[index]) { continue; }
        if (_session->onGamepadAxis(identity(controller), static_cast<GamepadAxis>(index), axes[index])) {
            previous->axes[index] = axes[index];
        } else { queued = false; }
    }
    if (!queued) { NSLog(@"Tina gamepad input could not be queued; resync pending"); }
}

@end
