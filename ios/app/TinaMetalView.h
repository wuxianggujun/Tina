#pragma once

#import <UIKit/UIKit.h>

namespace Tina::Platform {
class IosSession;
}

// CAMetalLayer-backed view. The layer object is what bgfx's SwapChainMtl::init casts nwh to;
// handing a UIView across that boundary fails at draw time rather than at bind time.
//
// Also the first responder and UITextInput surface: only UIKit can make a view first responder,
// so the software keyboard and hardware-key presses land here and are forwarded into IosSession.
@interface TinaMetalView : UIView <UIKeyInput, UITextInput>

@property (nonatomic, assign, nullable) Tina::Platform::IosSession* session;
@property (nonatomic, readonly) BOOL layerBound;

- (void)bindOrResizeLayer;
- (void)unbindLayer;

@end
