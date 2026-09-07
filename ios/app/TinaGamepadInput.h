#pragma once

#import <Foundation/Foundation.h>

namespace Tina::Platform { class IosSession; }

@interface TinaGamepadInput : NSObject
- (instancetype)initWithSession:(Tina::Platform::IosSession*)session;
- (void)start;
- (void)stop;
- (void)serviceResync;
- (void)invalidate;
@end
