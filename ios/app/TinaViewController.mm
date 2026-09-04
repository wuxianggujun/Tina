#import "TinaViewController.h"
#import "TinaMetalView.h"

#include <tina/platform/ios/IosSession.hpp>

#import <QuartzCore/CADisplayLink.h>
#import <UIKit/UIKit.h>

#include <memory>

using Tina::Platform::IosSession;
using Tina::Platform::IosSoftKeyboardRequest;

@implementation TinaViewController {
    std::unique_ptr<IosSession> _session;
    TinaMetalView* _metalView;
    CADisplayLink* _displayLink;
    BOOL _engineEnded;
    Tina::u32 _lastOcclusion;
}

- (void)loadView
{
    _metalView = [[TinaMetalView alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.view = _metalView;
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;

    auto created = IosSession::Create();
    if (!created)
    {
        return;
    }
    _session = std::move(*created);
    _metalView.session = _session.get();
    [_metalView bindOrResizeLayer];

    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    [center addObserver:self selector:@selector(keyboardFrameChanged:)
                   name:UIKeyboardWillChangeFrameNotification
                 object:nil];
    [center addObserver:self selector:@selector(keyboardDidHide:) name:UIKeyboardDidHideNotification object:nil];
}

- (void)viewDidAppear:(BOOL)animated
{
    [super viewDidAppear:animated];
    [self startDisplayLink];
}

- (void)viewWillDisappear:(BOOL)animated
{
    [super viewWillDisappear:animated];
    [self stopDisplayLink];
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [self stopDisplayLink];
    if (_session)
    {
        _session->shutdown();
    }
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (void)startDisplayLink
{
    if (_displayLink != nil)
    {
        return;
    }
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)stopDisplayLink
{
    [_displayLink invalidate];
    _displayLink = nil;
}

- (void)tick:(CADisplayLink*)link
{
    (void)link;
    if (!_session || _engineEnded)
    {
        return;
    }
    auto poll = _session->pollFrame();
    if (!poll || poll->isExitRequested())
    {
        _engineEnded = YES;
        [self stopDisplayLink];
        return;
    }
    [self applySoftKeyboardRequest];
}

- (void)applySoftKeyboardRequest
{
    if (!_session)
    {
        return;
    }
    const IosSoftKeyboardRequest request = _session->pendingSoftKeyboardRequest();
    if (request == IosSoftKeyboardRequest::None)
    {
        return;
    }
    BOOL applied = NO;
    if (request == IosSoftKeyboardRequest::Show)
    {
        applied = [_metalView becomeFirstResponder];
    } else if (request == IosSoftKeyboardRequest::Hide)
    {
        applied = [_metalView resignFirstResponder];
    }
    if (applied)
    {
        (void)_session->acknowledgeSoftKeyboardRequest(request);
    }
}

- (void)keyboardFrameChanged:(NSNotification*)notification
{
    [self reportOcclusionFromNotification:notification hidden:NO];
}

- (void)keyboardDidHide:(NSNotification*)notification
{
    (void)notification;
    [self reportOcclusionFromNotification:nil hidden:YES];
}

- (void)reportOcclusionFromNotification:(NSNotification*)notification hidden:(BOOL)hidden
{
    if (!_session)
    {
        return;
    }
    Tina::u32 physicalHeight = 0;
    if (!hidden && notification != nil)
    {
        NSValue* frameValue = notification.userInfo[UIKeyboardFrameEndUserInfoKey];
        const CGRect keyboardScreen = frameValue.CGRectValue;
        const CGRect keyboardInView = [self.view convertRect:keyboardScreen fromView:nil];
        const CGRect intersection = CGRectIntersection(self.view.bounds, keyboardInView);
        if (!CGRectIsNull(intersection) && !CGRectIsEmpty(intersection))
        {
            const CGFloat scale = _metalView.contentScaleFactor > 0.0 ? _metalView.contentScaleFactor : 1.0;
            const CGFloat pixels = intersection.size.height * scale;
            if (pixels > 0.0)
            {
                physicalHeight = static_cast<Tina::u32>(pixels);
            }
        }
    }
    if (physicalHeight == _lastOcclusion)
    {
        return;
    }
    _lastOcclusion = physicalHeight;
    (void)_session->onSoftKeyboardOcclusionChanged(physicalHeight);
}

@end
