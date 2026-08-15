#import <AppKit/AppKit.h>

#include "airplay2/airplay_receiver.h"
#include "airplay2/apple_audio_sink.h"

#include <memory>

@interface GwlAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) NSTextField *deviceLabel;
@property(nonatomic, strong) NSTextView *logView;
@property(nonatomic, strong) NSButton *stopButton;
@end

@implementation GwlAppDelegate {
    std::unique_ptr<gwl::airplay2::AirPlayReceiver> _receiver;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;

    NSRect frame = NSMakeRect(0, 0, 900, 620);
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                               styleMask:(NSWindowStyleMaskTitled |
                                                          NSWindowStyleMaskClosable |
                                                          NSWindowStyleMaskMiniaturizable |
                                                          NSWindowStyleMaskResizable)
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    self.window.title = @"GWL AirPlay Receiver";
    [self.window center];

    NSView *content = self.window.contentView;

    NSTextField *title = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 565, 850, 32)];
    title.stringValue = @"GWL AirPlay Receiver";
    title.font = [NSFont boldSystemFontOfSize:24];
    title.editable = NO;
    title.bezeled = NO;
    title.drawsBackground = NO;
    [content addSubview:title];

    self.statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 525, 850, 28)];
    self.statusLabel.stringValue = @"●  Starting...";
    self.statusLabel.font = [NSFont systemFontOfSize:15 weight:NSFontWeightMedium];
    self.statusLabel.editable = NO;
    self.statusLabel.bezeled = NO;
    self.statusLabel.drawsBackground = NO;
    [content addSubview:self.statusLabel];

    self.deviceLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(24, 490, 850, 24)];
    self.deviceLabel.stringValue = @"Device: GWL AirPlay Demo    •    Audio: enabled    •    Video: discovery pending";
    self.deviceLabel.font = [NSFont systemFontOfSize:13];
    self.deviceLabel.editable = NO;
    self.deviceLabel.bezeled = NO;
    self.deviceLabel.drawsBackground = NO;
    [content addSubview:self.deviceLabel];

    NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(24, 70, 850, 400)];
    scroll.hasVerticalScroller = YES;
    scroll.borderType = NSBezelBorder;
    self.logView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, 830, 380)];
    self.logView.editable = NO;
    self.logView.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular];
    self.logView.string = @"GWL AirPlay diagnostic log\n---------------------------\n";
    scroll.documentView = self.logView;
    [content addSubview:scroll];

    NSButton *clearButton = [[NSButton alloc] initWithFrame:NSMakeRect(24, 24, 90, 32)];
    clearButton.title = @"Clear Log";
    clearButton.bezelStyle = NSBezelStyleRounded;
    clearButton.target = self;
    clearButton.action = @selector(clearLog:);
    [content addSubview:clearButton];

    self.stopButton = [[NSButton alloc] initWithFrame:NSMakeRect(784, 24, 90, 32)];
    self.stopButton.title = @"Stop";
    self.stopButton.bezelStyle = NSBezelStyleRounded;
    self.stopButton.target = self;
    self.stopButton.action = @selector(stopReceiver:);
    [content addSubview:self.stopButton];

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self startReceiver];
}

- (void)appendLog:(NSString *)message {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSString *line = [NSString stringWithFormat:@"%@\n", message];
        [[self.logView textStorage] appendAttributedString:[[NSAttributedString alloc] initWithString:line]];
        [self.logView scrollRangeToVisible:NSMakeRange(self.logView.string.length, 0)];
    });
}

- (void)startReceiver {
    __weak GwlAppDelegate *weakSelf = self;
    _receiver = std::make_unique<gwl::airplay2::AirPlayReceiver>();

    gwl::airplay2::ReceiverConfig config;
    config.device_name = "GWL AirPlay Demo";
    config.enable_audio = true;
    config.enable_video = true;
    config.audio_sink_factory = [] {
        return std::make_unique<gwl::airplay2::AppleAudioSink>();
    };
    config.log_callback = [weakSelf](const std::string& message) {
        NSString *text = [[NSString alloc] initWithUTF8String:message.c_str()];
        dispatch_async(dispatch_get_main_queue(), ^{
            GwlAppDelegate *self = weakSelf;
            if (!self) return;
            [self appendLog:text];
            if ([text hasPrefix:@"ERROR:"]) {
                self.statusLabel.stringValue = @"●  Error — see diagnostic log";
            } else if ([text hasPrefix:@"RTSP "]) {
                self.statusLabel.stringValue = @"●  AirPlay session active — see RTSP log";
            } else if ([text isEqualToString:@"Waiting for AirPlay connection..."]) {
                self.statusLabel.stringValue = @"●  Ready — waiting for AirPlay connection";
            }
        });
    };

    if (!_receiver->start(config)) {
        self.statusLabel.stringValue = @"●  Failed to start receiver";
        [self appendLog:@"ERROR: receiver start failed"];
        return;
    }
}

- (void)clearLog:(id)sender {
    (void)sender;
    self.logView.string = @"GWL AirPlay diagnostic log\n---------------------------\n";
}

- (void)stopReceiver:(id)sender {
    (void)sender;
    if (_receiver) {
        _receiver->stop();
        _receiver.reset();
    }
    self.statusLabel.stringValue = @"●  Stopped";
    self.stopButton.enabled = NO;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    (void)sender;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    (void)notification;
    if (_receiver) {
        _receiver->stop();
        _receiver.reset();
    }
}
@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        GwlAppDelegate *delegate = [[GwlAppDelegate alloc] init];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app run];
    }
    return 0;
}
