#import "TinaMetalView.h"

#include <tina/platform/ios/IosSession.hpp>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <string>

using Tina::Platform::IosKeyAction;
using Tina::Platform::IosSession;
using Tina::Platform::IosSoftKeyboardRequest;
using Tina::Platform::IosTouchPhase;

namespace {

[[nodiscard]] std::u16string utf16FromNsString(NSString* text)
{
    if (text == nil || text.length == 0)
    {
        return {};
    }
    std::u16string out(static_cast<std::size_t>(text.length), u'\0');
    [text getCharacters:reinterpret_cast<unichar*>(out.data()) range:NSMakeRange(0, text.length)];
    return out;
}

void forwardTouches(IosSession* session, NSSet<UITouch*>* touches, UIView* view, IosTouchPhase phase)
{
    if (session == nullptr)
    {
        return;
    }
    for (UITouch* touch in touches)
    {
        const CGPoint point = [touch locationInView:view];
        // Points, not pixels: IosSession / the backend treat locationInView as already logical.
        (void)session->onTouch(reinterpret_cast<std::uintptr_t>((__bridge void*)touch), phase,
                               static_cast<float>(point.x), static_cast<float>(point.y));
    }
}

} // namespace

@interface TinaTextPosition : UITextPosition
@property (nonatomic, assign) NSInteger offset;
+ (instancetype)positionWithOffset:(NSInteger)offset;
@end

@implementation TinaTextPosition
+ (instancetype)positionWithOffset:(NSInteger)offset
{
    TinaTextPosition* position = [[TinaTextPosition alloc] init];
    position.offset = offset;
    return position;
}
@end

@interface TinaTextRange : UITextRange
@property (nonatomic, strong) TinaTextPosition* startPosition;
@property (nonatomic, strong) TinaTextPosition* endPosition;
+ (instancetype)rangeWithStart:(NSInteger)start end:(NSInteger)end;
@end

@implementation TinaTextRange
+ (instancetype)rangeWithStart:(NSInteger)start end:(NSInteger)end
{
    TinaTextRange* range = [[TinaTextRange alloc] init];
    range.startPosition = [TinaTextPosition positionWithOffset:start];
    range.endPosition = [TinaTextPosition positionWithOffset:end];
    return range;
}
- (UITextPosition*)start
{
    return self.startPosition;
}
- (UITextPosition*)end
{
    return self.endPosition;
}
- (BOOL)isEmpty
{
    return self.startPosition.offset == self.endPosition.offset;
}
@end

@implementation TinaMetalView {
    NSString* _markedText;
    id<UITextInputDelegate> _inputDelegate;
    id<UITextInputTokenizer> _tokenizer;
    UITextRange* _selectedTextRange;
    NSDictionary<NSAttributedStringKey, id>* _markedTextStyle;
    BOOL _layerBound;
}

@synthesize beginningOfDocument = _beginningOfDocument;
@synthesize endOfDocument = _endOfDocument;
@synthesize inputDelegate = _inputDelegate;
@synthesize tokenizer = _tokenizer;
@synthesize selectedTextRange = _selectedTextRange;
@synthesize markedTextStyle = _markedTextStyle;
@synthesize autocapitalizationType;
@synthesize autocorrectionType;
@synthesize spellCheckingType;
@synthesize smartQuotesType;
@synthesize smartDashesType;
@synthesize smartInsertDeleteType;
@synthesize keyboardType;
@synthesize keyboardAppearance;
@synthesize returnKeyType;
@synthesize enablesReturnKeyAutomatically;
@synthesize secureTextEntry;
@synthesize textContentType;
@synthesize passwordRules;

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self == nil)
    {
        return nil;
    }
    self.multipleTouchEnabled = YES;
    self.contentScaleFactor = UITraitCollection.currentTraitCollection.displayScale;
    _markedText = @"";
    _selectedTextRange = [TinaTextRange rangeWithStart:0 end:0];
    _tokenizer = [[UITextInputStringTokenizer alloc] initWithTextInput:self];
    _beginningOfDocument = [TinaTextPosition positionWithOffset:0];
    _endOfDocument = [TinaTextPosition positionWithOffset:0];
    self.autocapitalizationType = UITextAutocapitalizationTypeNone;
    self.autocorrectionType = UITextAutocorrectionTypeNo;
    self.spellCheckingType = UITextSpellCheckingTypeNo;
    self.keyboardType = UIKeyboardTypeDefault;
    self.returnKeyType = UIReturnKeyDone;
    [self configureMetalLayer];
    return self;
}

- (BOOL)layerBound
{
    return _layerBound;
}

- (BOOL)canBecomeFirstResponder
{
    return YES;
}

- (void)configureMetalLayer
{
    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    const CGFloat scale = self.contentScaleFactor > 0.0 ? self.contentScaleFactor : 1.0;
    layer.contentsScale = scale;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    const CGSize bounds = self.bounds.size;
    layer.drawableSize = CGSizeMake(bounds.width * scale, bounds.height * scale);
}

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    if (self.window == nil)
    {
        [self unbindLayer];
        return;
    }
    self.contentScaleFactor = self.window.screen.scale;
    [self bindOrResizeLayer];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    if (self.window == nil)
    {
        return;
    }
    [self bindOrResizeLayer];
}

- (void)bindOrResizeLayer
{
    [self configureMetalLayer];
    if (_session == nullptr)
    {
        return;
    }

    CAMetalLayer* layer = (CAMetalLayer*)self.layer;
    const Tina::Platform::IosNativeLayerHandle handle{
        .metalLayer = reinterpret_cast<std::uintptr_t>((__bridge void*)layer),
    };
    const Tina::Platform::FramebufferExtent extent{
        static_cast<Tina::u32>(layer.drawableSize.width),
        static_cast<Tina::u32>(layer.drawableSize.height),
    };
    const Tina::Platform::ContentScale scale{
        static_cast<float>(layer.contentsScale),
        static_cast<float>(layer.contentsScale),
    };
    if (extent.width == 0 || extent.height == 0)
    {
        return;
    }

    if (_layerBound)
    {
        (void)_session->resizeDrawable(extent, scale);
        return;
    }
    _layerBound = _session->bindLayer(handle, extent, scale).has_value();
}

- (void)unbindLayer
{
    if (_session != nullptr)
    {
        _session->unbindLayer();
    }
    _layerBound = NO;
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    forwardTouches(_session, touches, self, IosTouchPhase::Began);
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    forwardTouches(_session, touches, self, IosTouchPhase::Moved);
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    forwardTouches(_session, touches, self, IosTouchPhase::Ended);
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    forwardTouches(_session, touches, self, IosTouchPhase::Cancelled);
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    (void)event;
    if (_session == nullptr)
    {
        return;
    }
    for (UIPress* press in presses)
    {
        UIKey* key = press.key;
        if (key == nil)
        {
            continue;
        }
        (void)_session->onKey(static_cast<Tina::i32>(key.keyCode), IosKeyAction::Down, NO);
    }
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    (void)event;
    if (_session == nullptr)
    {
        return;
    }
    for (UIPress* press in presses)
    {
        UIKey* key = press.key;
        if (key == nil)
        {
            continue;
        }
        (void)_session->onKey(static_cast<Tina::i32>(key.keyCode), IosKeyAction::Up, NO);
    }
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    [self pressesEnded:presses withEvent:event];
}

- (BOOL)hasText
{
    return YES;
}

- (void)insertText:(NSString*)text
{
    if (_session == nullptr || text.length == 0)
    {
        return;
    }
    const std::u16string utf16 = utf16FromNsString(text);
    (void)_session->onTextCommitUtf16(utf16);
    _markedText = @"";
}

- (void)deleteBackward
{
    if (_session == nullptr)
    {
        return;
    }
    // UIKeyboardHIDUsageKeyboardDeleteOrBackspace. The C++ table maps this to Key::Backspace.
    (void)_session->onKey(0x2A, IosKeyAction::Down, NO);
    (void)_session->onKey(0x2A, IosKeyAction::Up, NO);
}

- (NSString*)textInRange:(UITextRange*)range
{
    (void)range;
    return _markedText;
}

- (void)replaceRange:(UITextRange*)range withText:(NSString*)text
{
    (void)range;
    [self insertText:text];
}

- (UITextRange*)markedTextRange
{
    if (_markedText.length == 0)
    {
        return nil;
    }
    return [TinaTextRange rangeWithStart:0 end:static_cast<NSInteger>(_markedText.length)];
}

- (void)setMarkedText:(NSString*)markedText selectedRange:(NSRange)selectedRange
{
    _markedText = markedText ?: @"";
    if (_session == nullptr)
    {
        return;
    }
    const std::u16string utf16 = utf16FromNsString(_markedText);
    const Tina::i32 cursor = static_cast<Tina::i32>(selectedRange.location + selectedRange.length);
    (void)_session->onSetMarkedTextUtf16(utf16, cursor);
}

- (void)unmarkText
{
    _markedText = @"";
    if (_session != nullptr)
    {
        (void)_session->onUnmarkText();
    }
}

- (UITextRange*)textRangeFromPosition:(UITextPosition*)fromPosition toPosition:(UITextPosition*)toPosition
{
    const NSInteger start = [(TinaTextPosition*)fromPosition offset];
    const NSInteger end = [(TinaTextPosition*)toPosition offset];
    return [TinaTextRange rangeWithStart:start end:end];
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position offset:(NSInteger)offset
{
    const NSInteger next = [(TinaTextPosition*)position offset] + offset;
    return [TinaTextPosition positionWithOffset:next < 0 ? 0 : next];
}

- (UITextPosition*)positionFromPosition:(UITextPosition*)position
                            inDirection:(UITextLayoutDirection)direction
                                 offset:(NSInteger)offset
{
    const NSInteger delta = (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp)
                                ? -offset
                                : offset;
    return [self positionFromPosition:position offset:delta];
}

- (NSComparisonResult)comparePosition:(UITextPosition*)position toPosition:(UITextPosition*)other
{
    const NSInteger left = [(TinaTextPosition*)position offset];
    const NSInteger right = [(TinaTextPosition*)other offset];
    if (left < right)
    {
        return NSOrderedAscending;
    }
    if (left > right)
    {
        return NSOrderedDescending;
    }
    return NSOrderedSame;
}

- (NSInteger)offsetFromPosition:(UITextPosition*)fromPosition toPosition:(UITextPosition*)toPosition
{
    return [(TinaTextPosition*)toPosition offset] - [(TinaTextPosition*)fromPosition offset];
}

- (UITextPosition*)positionWithinRange:(UITextRange*)range farthestInDirection:(UITextLayoutDirection)direction
{
    TinaTextRange* typed = (TinaTextRange*)range;
    if (direction == UITextLayoutDirectionRight || direction == UITextLayoutDirectionDown)
    {
        return typed.endPosition;
    }
    return typed.startPosition;
}

- (UITextRange*)characterRangeByExtendingPosition:(UITextPosition*)position
                                      inDirection:(UITextLayoutDirection)direction
{
    const NSInteger offset = [(TinaTextPosition*)position offset];
    if (direction == UITextLayoutDirectionLeft || direction == UITextLayoutDirectionUp)
    {
        return [TinaTextRange rangeWithStart:offset > 0 ? offset - 1 : 0 end:offset];
    }
    return [TinaTextRange rangeWithStart:offset end:offset + 1];
}

- (NSWritingDirection)baseWritingDirectionForPosition:(UITextPosition*)position
                                          inDirection:(UITextStorageDirection)direction
{
    (void)position;
    (void)direction;
    return NSWritingDirectionLeftToRight;
}

- (void)setBaseWritingDirection:(NSWritingDirection)writingDirection forRange:(UITextRange*)range
{
    (void)writingDirection;
    (void)range;
}

- (CGRect)caretRectFromSession
{
    if (_session == nullptr)
    {
        return CGRectZero;
    }
    const auto caret = _session->caretPoints();
    if (!caret.has_value())
    {
        return CGRectZero;
    }
    return CGRectMake(caret->x, caret->y, caret->width > 0.0F ? caret->width : 1.0F,
                      caret->height > 0.0F ? caret->height : 1.0F);
}

- (CGRect)firstRectForRange:(UITextRange*)range
{
    (void)range;
    return [self caretRectFromSession];
}

- (CGRect)caretRectForPosition:(UITextPosition*)position
{
    (void)position;
    return [self caretRectFromSession];
}

- (NSArray<UITextSelectionRect*>*)selectionRectsForRange:(UITextRange*)range
{
    (void)range;
    return @[];
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point
{
    (void)point;
    return [TinaTextPosition positionWithOffset:0];
}

- (UITextPosition*)closestPositionToPoint:(CGPoint)point withinRange:(UITextRange*)range
{
    (void)point;
    (void)range;
    return [TinaTextPosition positionWithOffset:0];
}

- (UITextRange*)characterRangeAtPoint:(CGPoint)point
{
    (void)point;
    return [TinaTextRange rangeWithStart:0 end:0];
}

@end
