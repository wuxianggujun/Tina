#pragma once

#import <UIKit/UIKit.h>

// Owns the C++ IosSession and the CADisplayLink that drives pollFrame.
// EngineHost is a later slice: ADR 0032 D3 already split start()/tick() so this callback can
// move from pollFrame() to tick() without changing the UIKit half.
@interface TinaViewController : UIViewController
@end
