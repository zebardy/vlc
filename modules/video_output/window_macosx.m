/**
 * @file window_macosx.m
 * @brief macOS Window and View output provider
 */

/* Copyright (C) 2020 VLC authors and VideoLAN
 *
 * Authors: Marvin Scholz <epirat 07 at gmail dot com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#import <Cocoa/Cocoa.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_window.h>

#define VLC_ASSERT_MAINTHREAD NSAssert([[NSThread currentThread] isMainThread], \
    @"Must be called from the main thread!")

/**
 * Style mask for a decorated window
 */
static const NSWindowStyleMask decoratedWindowStyleMask =
    NSWindowStyleMaskTitled
    | NSWindowStyleMaskClosable
    | NSWindowStyleMaskMiniaturizable
    | NSWindowStyleMaskResizable;

/**
 * Style mask for a non-decorated window
 */
static const NSWindowStyleMask undecoratedWindowStyleMask =
    NSWindowStyleMaskBorderless
    | NSWindowStyleMaskResizable;


#pragma mark -
#pragma mark Obj-C Interfaces

NS_ASSUME_NONNULL_BEGIN

/**
 * Window delegate and Core interactions class
 *
 * Acts as delegate for the Window and handles communication
 * of the window state from/to the core. This is not done directly
 * in the window controller, as the module on VLCs side might be
 * already gone when the window and its controller is still around
 * so we need a separate object that we can invalidate as soon
 * as the VLC module object is gone.
 */
@interface VLCVideoWindowModuleDelegate : NSObject {
    @private
    // VLC window object, only use it on the eventQueue
    vout_window_t*     vlc_vout_window;
    dispatch_queue_t   eventQueue;

    BOOL               _isViewSet;
}

- (instancetype)initWithVLCVoutWindow:(vout_window_t *)vout_window;

/// Reports that the window is fullscreen now
- (void)reportFullscreen;

/// Reports that the previously fullscreen window is no longer fullscreen
- (void)reportWindowed;

/// Reports the new window size in pixels
- (void)reportSizeChanged:(NSSize)newSize;

/// Reports that the window was closed
- (void)reportClose;

@end

/**
 * Video output window class
 *
 * Custom NSWindow subclass, mostly to overwrite that the window
 * can become the key window even if its using the borderless
 * (undecorated) style.
 */
@interface VLCVideoWindow : NSWindow
@end


/**
 * Video view class
 *
 * Custom NSWindow subclass, used to track resizes so that
 * the core can be notified about the new sizes in a timely manner.
 */
@interface VLCVideoWindowContentView : NSView {
    @private
    __weak VLCVideoWindowModuleDelegate *_moduleDelegate;
}

- (instancetype)initWithModuleDelegate:(VLCVideoWindowModuleDelegate *)delegate;
@end

/**
 * Video output window controller class
 *
 * Controller for the VLC standalone video window (independent of the interface)
 *
 * Implements all interactions between the display module and the NSWindow
 * class, except for resizes (which is handled by VLCVideoWindowContentView).
 */
@interface VLCVideoStandaloneWindowController : NSWindowController <NSWindowDelegate> {
@private
    __weak VLCVideoWindowModuleDelegate *_moduleDelegate;
}

- (instancetype)initWithModuleDelegate:(VLCVideoWindowModuleDelegate *)delegate;
- (void)showWindowWithConfig:(const vout_window_cfg_t *restrict)config;

/* Methods called by the callbacks to change properties of the Window */
- (void)setWindowDecorated:(BOOL)decorated;
- (void)setWindowFullscreen:(BOOL)fullscreen;

@end


#pragma mark -
#pragma mark Obj-C Implementations

@implementation VLCVideoWindowModuleDelegate : NSObject

- (instancetype)initWithVLCVoutWindow:(vout_window_t *)vout_window
{
    NSAssert(vout_window != NULL,
             @"VLCVideoWindowDelegate must be initialized with a valid vout_window");

    self = [super init];
    if (self) {
        eventQueue = dispatch_queue_create("org.videolan.vlc.vout", DISPATCH_QUEUE_SERIAL);

        vlc_vout_window = vout_window;
    }

    return self;
}

- (void)enqueueEventBlock:(void (^)(void))block
{
    dispatch_async(eventQueue, block);
}

- (void)setViewObject:(id)view
{
    NSAssert(_isViewSet == NO,
             @"VLCVideoWindowDelegate's viewObject must only bet set once");
    vlc_vout_window->type = VOUT_WINDOW_TYPE_NSOBJECT;
    vlc_vout_window->handle.nsobject = (__bridge void*)view;
}

- (void)reportFullscreen
{
    [self enqueueEventBlock:^void (void) {
        vout_window_ReportFullscreen(vlc_vout_window, NULL);
    }];
}

- (void)reportWindowed
{
    [self enqueueEventBlock:^void (void) {
        vout_window_ReportWindowed(vlc_vout_window);
    }];
}

- (void)reportSizeChanged:(NSSize)newSize
{
    [self enqueueEventBlock:^void (void) {
        vout_window_ReportSize(vlc_vout_window,
                               (unsigned int)newSize.width,
                               (unsigned int)newSize.height);
    }];
}

- (void)reportClose
{
    [self enqueueEventBlock:^void (void) {
        vout_window_ReportClose(vlc_vout_window);
    }];
}

- (void)dealloc
{
    dispatch_sync(eventQueue, ^void (void) {
        self->vlc_vout_window = NULL;
    });
}

@end


@implementation VLCVideoStandaloneWindowController

/**
 * Initializes the window controller with the given module delegate
 */
- (instancetype)initWithModuleDelegate:(VLCVideoWindowModuleDelegate *)delegate;
{
    VLC_ASSERT_MAINTHREAD;

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSZeroRect
                                                   styleMask:decoratedWindowStyleMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:YES];

    self = [super initWithWindow:window];
    if (self) {
        // Set the initial vout title
        [window setTitle:[NSString stringWithUTF8String:VOUT_TITLE " (VLC Video Output)"]];

        // The content always changes during live resize
        [window setPreservesContentDuringLiveResize:NO];

        // Do not release on close (we might want to re-open the window later)
        [window setReleasedWhenClosed:NO];

        // Hint that the window should become a primary fullscreen window
        [window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];

        // Create and set custom content view for the window
        VLCVideoWindowContentView *view =
            [[VLCVideoWindowContentView alloc] initWithModuleDelegate:delegate];

        if (view == nil)
            return nil;

        [window setContentView:view];

        [window setDelegate:self];

        // Position the window in the center
        [window center];

        [self setWindowFrameAutosaveName:@"VLCVideoStandaloneWindow"];

        _moduleDelegate = delegate;

        [_moduleDelegate setViewObject:view];
    }

    return self;
}

/**
 * Applies the given config to the window and shows it.
 */
- (void)showWindowWithConfig:(const vout_window_cfg_t *restrict)config
{
    VLC_ASSERT_MAINTHREAD;

    // Convert from backing to window coordinates
    NSRect backingRect = NSMakeRect(0, 0, config->width, config->height);
    NSRect windowRect = [self.window convertRectFromBacking:backingRect];
    [self.window setContentSize:windowRect.size];

    // Set decoration
    [self setWindowDecorated:config->is_decorated];

    // This should always be called last, to ensure we only show the
    // window once its fully configured. Else there could be visible
    // changes or animations when the config is applied.
    [self showWindow:nil];
    [self.window makeKeyAndOrderFront:nil];
}

- (BOOL)windowShouldClose:(NSWindow *)sender
{
    [_moduleDelegate reportClose];
    return YES;
}

#pragma mark Helper methods

- (BOOL)isWindowFullscreen
{
    return ((self.window.styleMask & NSFullScreenWindowMask) == NSFullScreenWindowMask);
}

#pragma mark Module interactions

- (void)setWindowDecorated:(BOOL)decorated
{
    NSWindowStyleMask mask =
        (decorated) ? decoratedWindowStyleMask : undecoratedWindowStyleMask;

    [self.window setStyleMask:mask];
}

- (void)setWindowFullscreen:(BOOL)fullscreen
{
    if (!!fullscreen == !![self isWindowFullscreen]) {
        // Nothing to do, just report the state to core
        if (fullscreen) {
            [_moduleDelegate reportFullscreen];
        } else {
            [_moduleDelegate reportWindowed];
        }
        return;
    }

    [self.window toggleFullScreen:nil];
}

#pragma mark Window delegate

- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
    [_moduleDelegate reportFullscreen];
}

- (void)windowDidExitFullScreen:(NSNotification *)notification
{
    [_moduleDelegate reportWindowed];
}

@end



@implementation VLCVideoWindowContentView

- (instancetype)initWithModuleDelegate:(VLCVideoWindowModuleDelegate *)delegate;
{
    self = [super init];
    if (self) {
        NSAssert(delegate != nil, @"Invalid VLCVideoWindowModuleDelegate passed.");
        _moduleDelegate = delegate;
    }
    return self;
}

- (void)drawRect:(NSRect)dirtyRect
{
    [[NSColor blackColor] setFill];
    NSRectFill(dirtyRect);
}

/**
 * Report the view size in the backing size dimensions to VLC core
 */
- (void)reportBackingSize
{
    NSRect bounds = [self convertRectToBacking:self.bounds];
    [_moduleDelegate reportSizeChanged:bounds.size];
}

/**
 * Handle view size changes
 */
- (void)resizeSubviewsWithOldSize:(NSSize)oldSize
{
    [self reportBackingSize];
    [super resizeSubviewsWithOldSize:oldSize];
}

/**
 * Handle view backing property changes
 */
- (void)viewDidChangeBackingProperties
{
    // When the view backing size changes, it means the view effectively
    // resizes from VLC core perspective, as it operates on the real
    // backing dimensions, not the view point size.
    [self reportBackingSize];
    [super viewDidChangeBackingProperties];
}

@end

@implementation VLCVideoWindow

- (BOOL)canBecomeKeyWindow
{
    // A window with NSWindowStyleMaskBorderless can usually not become key
    // window, unless we return YES here.
    return YES;
}

@end

NS_ASSUME_NONNULL_END


#pragma mark -
#pragma mark VLC module

typedef struct
{
    VLCVideoStandaloneWindowController *windowController;
    VLCVideoWindowModuleDelegate *delegate;
} vout_window_sys_t;

/* Enable Window
 */
static int WindowEnable(vout_window_t *wnd, const vout_window_cfg_t *restrict cfg)
{
    vout_window_sys_t *sys = wnd->sys;

    @autoreleasepool {
        __weak VLCVideoStandaloneWindowController *weakWc = sys->windowController;
        dispatch_sync(dispatch_get_main_queue(), ^{
            [weakWc showWindowWithConfig:cfg];
        });
    }

    return VLC_SUCCESS;
}

/* Request to close the window */
static void WindowDisable(vout_window_t *wnd)
{
    vout_window_sys_t *sys = wnd->sys;

    @autoreleasepool {
        __weak VLCVideoStandaloneWindowController *weakWc = sys->windowController;
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakWc close];
        });
    }
}

/* Request to resize the window */
static void WindowResize(vout_window_t *wnd, unsigned width, unsigned height)
{
    vout_window_sys_t *sys = wnd->sys;

    @autoreleasepool {
        __weak VLCVideoStandaloneWindowController *weakWc = sys->windowController;
        dispatch_async(dispatch_get_main_queue(), ^{
            VLCVideoStandaloneWindowController *wc = weakWc;
            // Convert from backing to window coordinates
            NSRect backingRect = NSMakeRect(0, 0, width, height);
            NSRect windowRect = [wc.window convertRectFromBacking:backingRect];
            [wc.window setContentSize:windowRect.size];

            // Size is reported by resizeSubviewsWithOldSize:, do not
            // report it here, else it would get reported twice.
        });
    }
}

/* Request to enable/disable Window decorations */
static void SetDecoration(vout_window_t *wnd, bool decorated)
{
    vout_window_sys_t *sys = wnd->sys;

    @autoreleasepool {
        __weak VLCVideoStandaloneWindowController *weakWc = sys->windowController;
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakWc setWindowDecorated:decorated];
        });
    }
}

/* Request to enter fullscreen */
static void WindowSetFullscreen(vout_window_t *wnd, const char *idstr)
{
    vout_window_sys_t *sys = wnd->sys;

    @autoreleasepool {
        __weak VLCVideoStandaloneWindowController *weakWc = sys->windowController;
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakWc setWindowFullscreen:YES];
        });
    }
}

/* Request to exit fullscreen */
static void WindowUnsetFullscreen(vout_window_t *wnd)
{
    vout_window_sys_t *sys = wnd->sys;

    @autoreleasepool {
        __weak VLCVideoStandaloneWindowController *weakWc = sys->windowController;
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakWc setWindowFullscreen:NO];
        });
    }
}

static void WindowSetTitle(struct vout_window_t *wnd, const char *title)
{
    vout_window_sys_t *sys = wnd->sys;
    @autoreleasepool {
        __weak VLCVideoStandaloneWindowController *weakWc = sys->windowController;
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakWc.window setTitle:[NSString stringWithUTF8String:title]];
        });
    }
}

/*
 * Module destruction
 */
void Close(vout_window_t *wnd)
{
    vout_window_sys_t *sys = wnd->sys;

    // ARC can not know when to release an object in a heap-allocated
    // struct, so we need to explicitly set it to nil here.
    sys->windowController = nil;
    sys->delegate = nil;
}

/*
 * Callbacks
 */
static const struct vout_window_operations ops = {
    .enable = WindowEnable,
    .disable = WindowDisable,
    .resize = WindowResize,
    .set_state = NULL,
    .unset_fullscreen = WindowUnsetFullscreen,
    .set_fullscreen = WindowSetFullscreen,
    .set_title = WindowSetTitle,
    .destroy = Close,
};

/*
 * Module initialization
 */
int Open(vout_window_t *wnd)
{
    @autoreleasepool {
        msg_Info(wnd, "using the macOS new video output window module");

        // Check if there is an NSApplication, needed for the connection
        // to the Window Server so we can use NSWindows, NSViews, etc.
        if (NSApp == nil) {
            msg_Err(wnd, "cannot create video output window without NSApplication");
            return VLC_EGENERIC;
        }

        vout_window_sys_t *sys = vlc_obj_calloc(VLC_OBJECT(wnd), 1, sizeof(*sys));
        if (unlikely(sys == NULL))
            return VLC_ENOMEM;

        VLCVideoWindowModuleDelegate *_moduleDelegate;
        _moduleDelegate = [[VLCVideoWindowModuleDelegate alloc] initWithVLCVoutWindow:wnd];
        if (unlikely(_moduleDelegate == nil))
            return VLC_ENOMEM;
        sys->delegate = _moduleDelegate;

        __block VLCVideoStandaloneWindowController *_windowController;
        dispatch_sync(dispatch_get_main_queue(), ^{
            _windowController = [[VLCVideoStandaloneWindowController alloc] initWithModuleDelegate:_moduleDelegate];
        });
        if (unlikely(_windowController == nil))
            return VLC_ENOMEM;
        sys->windowController = _windowController;

        wnd->ops = &ops;
        wnd->sys = sys;

        return VLC_SUCCESS;
    }
}

/*
 * Module declaration
 */
vlc_module_begin()
    set_description("macOS Video Output Window")
    set_capability("vout window", 1000)
    set_callback(Open)
vlc_module_end()
