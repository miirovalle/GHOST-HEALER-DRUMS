#include "Editor.h"
#include "MidiClipboard.h"
#include <cstring>
#import <UIKit/UIKit.h>

@interface GHMidiButton : UIButton <UIDragInteractionDelegate>
@property (nonatomic, copy) NSString* (^exportPath)(void);
@property (nonatomic, copy) void (^failure)(NSString*);
@property (nonatomic, copy) void (^tap)(void);
@end
@implementation GHMidiButton
- (void)tapMidi
{
    if (self.tap) self.tap();
    else [self shareMidi];
}
- (NSArray<UIDragItem*>*)dragInteraction:(UIDragInteraction*)interaction itemsForBeginningSession:(id<UIDragSession>)session
{
    (void)interaction; (void)session;
    NSString* path = self.exportPath();
    if (path.length == 0) return @[];
    NSURL* url = [NSURL fileURLWithPath:path];
    NSItemProvider* provider = [[NSItemProvider alloc] init];
    provider.suggestedName = url.lastPathComponent;
    [provider registerFileRepresentationForTypeIdentifier:@"public.midi-audio"
        fileOptions:0 visibility:NSItemProviderRepresentationVisibilityAll
        loadHandler:^NSProgress*(void (^completion)(NSURL*, BOOL, NSError*)) {
            completion(url, NO, nil);
            return nil;
        }];
    return @[[[UIDragItem alloc] initWithItemProvider:provider]];
}
- (void)shareMidi
{
    NSString* path = self.exportPath();
    if (path.length == 0) return;
    UIResponder* responder = self;
    while (responder && ![responder isKindOfClass:[UIViewController class]]) responder = responder.nextResponder;
    if (!responder) { self.failure(@"No presenting view controller. Use MIDI drag instead."); return; }
    UIViewController* parent = (UIViewController*)responder;
    while (parent.presentedViewController) parent = parent.presentedViewController;
    UIActivityViewController* sheet = [[UIActivityViewController alloc]
        initWithActivityItems:@[[NSURL fileURLWithPath:path]] applicationActivities:nil];
    sheet.popoverPresentationController.sourceView = self;
    sheet.popoverPresentationController.sourceRect = self.bounds;
    void (^failure)(NSString*) = self.failure;
    sheet.completionWithItemsHandler = ^(UIActivityType type, BOOL completed, NSArray* items, NSError* error) {
        (void)type; (void)completed; (void)items;
        if (error) failure(error.localizedDescription);
    };
    [parent presentViewController:sheet animated:YES completion:nil];
}
@end
namespace
{
std::unique_ptr<juce::UIViewComponent> makeMidiButton(std::function<juce::File()> exporter,
    std::function<void(juce::String)> failure, std::function<void()> tap)
{
    auto component = std::make_unique<juce::UIViewComponent>();
    GHMidiButton* button = [[GHMidiButton alloc] initWithFrame:CGRectZero];
    [button setTitle:@"Export MIDI" forState:UIControlStateNormal];
    button.backgroundColor = [UIColor colorWithRed:0.17 green:0.25 blue:0.29 alpha:1];
    [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    button.layer.cornerRadius = 5;
    button.exportPath = ^NSString* {
        const auto file = exporter();
        return file == juce::File{} ? @"" : [NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()];
    };
    button.failure = ^(NSString* message) { failure(juce::String::fromUTF8(message.UTF8String)); };
    if (tap)
    {
        button.tap = ^{ tap(); };
        button.contentHorizontalAlignment = UIControlContentHorizontalAlignmentLeft;
        button.titleLabel.font = [UIFont systemFontOfSize:14];
        button.titleLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    }
    [button addTarget:button action:@selector(tapMidi) forControlEvents:UIControlEventTouchUpInside];
    UIDragInteraction* drag = [[UIDragInteraction alloc] initWithDelegate:button];
    drag.enabled = YES; [button addInteraction:drag];
    component->setView((__bridge void*)button);
    return component;
}
}
std::unique_ptr<juce::Component> makeMidiShare(std::function<juce::File()> exporter, std::function<void(juce::String)> failure)
{
    return makeMidiButton(std::move(exporter), std::move(failure), {});
}
std::unique_ptr<juce::UIViewComponent> makeMidiNameDrag(std::function<juce::File()> exporter,
    std::function<void(juce::String)> failure, std::function<void()> tap)
{
    return makeMidiButton(std::move(exporter), std::move(failure), std::move(tap));
}
void setMidiNameDragText(juce::UIViewComponent& component, const juce::String& text)
{
    GHMidiButton* button = (__bridge GHMidiButton*)component.getView();
    [button setTitle:[NSString stringWithUTF8String:text.toRawUTF8()] forState:UIControlStateNormal];
}
juce::Result gh::copyMidiToClipboard(const juce::MemoryBlock& data)
{
    if (data.getSize() < 14 || std::memcmp(data.getData(), "MThd", 4) != 0)
        return juce::Result::fail("Cannot copy invalid MIDI data.");
    NSData* midi = [NSData dataWithBytes:data.getData() length:data.getSize()];
    if (midi == nil) return juce::Result::fail("Could not prepare iPad MIDI clipboard data.");
    [UIPasteboard.generalPasteboard setItems:@[@{@"public.midi-audio":midi}]];
    return juce::Result::ok();
}
