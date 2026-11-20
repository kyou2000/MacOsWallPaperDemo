#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>
#include <QuartzCore/QuartzCore.hpp>
#include <Metal/Metal.hpp>
#include <functional>
#include <atomic>
#include <csignal>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include "backgroundrender.h"
#include "application.h"
#include "videorender.h"

using namespace Render;

static std::atomic<bool> g_shouldQuit{false};

static void SignalHandler(int sig) {
    g_shouldQuit.store(true, std::memory_order_release);
}

// 为了让OC循环中使用c++的函数，这里编写一个类进行回调函数注册
static BackgroundRender *g_render = nullptr;
static VideoRender* g_videoRender = nullptr;

@interface FrameTicker : NSObject
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) void (*renderFunc)(void);
- (void)tick:(CADisplayLink *)link;
@end

@implementation FrameTicker
- (void)tick:(CADisplayLink *)link {
    if (g_render && _renderFunc) {
        _renderFunc();
    }
}
@end

// 背景暂时全用OC
void Application::run1(const std::string& file, int type, const std::string& execPath){
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    int parentWatchFd = -1;
    if (const char* parentFdText = std::getenv("WALLPAPER_PARENT_FD")){
        int candidateFd = std::atoi(parentFdText);
        if (candidateFd >= 0 && fcntl(candidateFd, F_GETFD) != -1){
            parentWatchFd = candidateFd;
        }
    }

    int ret = 0;

    @autoreleasepool {
        [NSApplication sharedApplication];
        // 输入的size暂时无效
        // 窗口大小和渲染像素大小不同，需要乘缩放
        NSScreen *screen = [NSScreen mainScreen]; // 目前只管主屏幕的
        NSRect screenRect = screen.frame;
        CGFloat h = screenRect.size.height;
        CGFloat w = screenRect.size.width;
        CGFloat scale = screen.backingScaleFactor;
        NSRect rect = NSMakeRect(0, 0, w, h);
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:rect
            styleMask:NSWindowStyleMaskBorderless
            backing:NSBackingStoreBuffered
            defer:NO];

        window.level = CGWindowLevelForKey(kCGDesktopWindowLevelKey);
        window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
                                | NSWindowCollectionBehaviorStationary;

        window.ignoresMouseEvents = YES;
        window.opaque = NO;
        window.backgroundColor = [NSColor clearColor];
        window.hasShadow = NO;

        NSView* contentView = window.contentView;
        contentView.wantsLayer = YES;

        CAMetalLayer* layer = [CAMetalLayer layer];
        layer.drawableSize = CGSizeMake(w * scale, h * scale);
        contentView.layer = layer;

        [window orderFront:nil];

        // 调用c++接口,转换为c++指针
        CA::MetalLayer *cppLayer = (__bridge CA::MetalLayer *)layer;
        
        ShaderMode mode;
        if (type == 0){
            mode = ShaderMode::ModePic;
        } else if (type == 1){
            mode = ShaderMode::ModeVideo;
        }

        VideoRender videoRender;
        if (mode == ShaderMode::ModeVideo){
            ret = videoRender.loadVideo(file.c_str());
            if (ret < 0){
                videoRender.clean();
                return;
            }
        }
        
        BackgroundRender render;
        render.setExecPath(execPath);
        if (mode == ShaderMode::ModePic){
            render.setFilepath(file);
        }

        ret = render.init((void *)cppLayer, mode, videoRender);
        if (ret < 0){
            render.destroy();
            videoRender.clean();
            return;
        }
        
        if (mode == ShaderMode::ModeVideo){
            videoRender.startDecodeThread();
        }

        g_render = &render;
        g_videoRender = &videoRender;

        FrameTicker *ticker = [[FrameTicker alloc] init];
        
        if (mode == ShaderMode::ModeVideo) {
            ticker.renderFunc = []() {
                g_render->drawVideoFrame(*g_videoRender);
            };
        } else {
            ticker.renderFunc = []() {
                g_render->draw();
            };
        }

        ticker.displayLink = [window displayLinkWithTarget:ticker
                                                selector:@selector(tick:)];
        [ticker.displayLink addToRunLoop:[NSRunLoop mainRunLoop]
                                forMode:NSDefaultRunLoopMode];

        // 进入主事件循环，窗口会一直显示
        while (!g_shouldQuit.load(std::memory_order_acquire)) {
            if (parentWatchFd != -1){
                char parentByte;
                ssize_t parentResult = read(parentWatchFd, &parentByte, 1);
                if (parentResult == 0 ||
                    (parentResult < 0 && errno != EAGAIN && errno != EINTR)){
                    g_shouldQuit.store(true, std::memory_order_release);
                    break;
                }
            }

            [[NSRunLoop mainRunLoop]
                runMode:NSDefaultRunLoopMode
            beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        }

        // 资源清理
        [ticker.displayLink invalidate];

        render.destroy();
        if (mode == ShaderMode::ModeVideo){
            videoRender.stopDecodeThread();
            videoRender.clean();
        }
    }
}
