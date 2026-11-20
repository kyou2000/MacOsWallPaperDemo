#import <AVFoundation/AVFoundation.h>
#include <chrono>
#include "videorender.h"
#include <Metal/Metal.hpp>
#include <CoreVideo/CVMetalTextureCache.h>
#include "framesolt.h"

using namespace Render;
using namespace MTL;

VideoRender::VideoRender(){

}

VideoRender::~VideoRender(){
    // 销毁顺序：先停止后台线程，再释放解码器、纹理缓存和未消费的帧，
    // 避免线程访问已释放的成员。
    if (!shouldStop.load()){
        stopDecodeThread();
    }
    
    clean();
}

int VideoRender::loadVideo(const char* path){
    if (!path) return -1;
    videoPath = path;
    if (reloadReader()){
        return 0;
    }
    return -1;
}

bool VideoRender::reloadReader(){
    NSError* error = nil;
    // AVAssetReader 到达 Completed 后不能再次 startReading；
    // 循环播放时必须为每一轮重新创建 reader/output。
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:videoPath.c_str()]];
    AVAsset *asset = [AVAsset assetWithURL:url];

    if (metalRenderer) {
        CFRelease(static_cast<CFTypeRef>(metalRenderer));
        metalRenderer = nullptr;
    }
    if (metalRenderOutput) {
        CFRelease(static_cast<CFTypeRef>(metalRenderOutput));
        metalRenderOutput = nullptr;
    }

    AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
    // C++ 类通过 void* 持有 ObjC 对象时，需要使用 __bridge_retained
    // 取得所有权；后续通过 CFRelease 统一释放。
    metalRenderer = (__bridge_retained void *)reader;
    if (!reader || error) {
        NSString *str = @"reader error!";
        printf("%s\n", [str UTF8String]);
        if (metalRenderer) {
            CFRelease(static_cast<CFTypeRef>(metalRenderer));
            metalRenderer = nullptr;
        }
        reader = nil;
        return false;
    }

    AVAssetTrack* videoTrack = nil;
    for (AVAssetTrack* track in [asset tracksWithMediaType:AVMediaTypeVideo]) {
        videoTrack = track;
        break;
    }
    if (!videoTrack) {
        // loadVideo 失败时也必须释放刚创建的 reader，避免交由 clean() 释放。
        CFRelease(static_cast<CFTypeRef>(metalRenderer));
        metalRenderer = nullptr;
        return false;
    }

    NSDictionary* settings = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferMetalCompatibilityKey : @YES,
    };

    AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack
                                                        outputSettings:settings];
    // output 与 reader 一样由 C++ 侧显式持有。
    metalRenderOutput = (__bridge_retained void *)output;
    [reader addOutput:output];
    return true;
}

bool VideoRender::decode(){
    AVAssetReader* reader = (__bridge AVAssetReader*)metalRenderer;
    AVAssetReaderTrackOutput* output = (__bridge AVAssetReaderTrackOutput*)metalRenderOutput;

    if (!reader || !output) return false;
    if (![reader startReading]) {
        NSString *err = reader.error ? reader.error.localizedDescription : @"startReading failed";
        printf("startReading error: %s\n", [err UTF8String]);
        return false;
    }

    while (reader.status == AVAssetReaderStatusReading && !shouldStop.load()){
        CMSampleBufferRef sample = [output copyNextSampleBuffer];
        if (!sample) break;

        // 为防止实时渲染过快，这里获取播放速度
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
        double ptsSeconds = CMTimeGetSeconds(pts);

        CVPixelBufferRef pixel = CMSampleBufferGetImageBuffer(sample);
        if (pixel) {
            CVPixelBufferRetain(pixel);
            // retain 后把像素缓冲交给队列，后续由渲染端负责释放。
            const bool startsNewTimeline = nextStartsNewTimeline.exchange(false);
            insertFrameToSolt(pixel, ptsSeconds, startsNewTimeline);
        }

        CFRelease(sample);
    }

    if (reader.status == AVAssetReaderStatusFailed) {
        NSString *err = reader.error ? reader.error.localizedDescription : @"reading failed";
        printf("reader failed: %s\n", [err UTF8String]);
        return false;
    }

    return reader.status == AVAssetReaderStatusCompleted;
}

void VideoRender::startDecodeThread(){
    if (decodeThread.joinable()) return;
    if (isRunning.load()) return;
    isRunning.store(true);
    shouldStop.store(false);
    shouldSwitch.store(false);

    decodeThread = std::thread([this]{
        // 同步接口 copyNextSampleBuffer 不需要手动运行 NSRunLoop；
        //autoreleasepool 负责释放本轮 ObjC 自动释放对象。
        @autoreleasepool{
            while (!shouldStop.load()) {
                // decode() 返回 true 表示读完整轮视频；
                // 重建 reader 后继续下一轮，从而实现循环播放。
                bool need_decode = decode() && !shouldStop.load() && reloadReader();
                if (need_decode) {
                    nextStartsNewTimeline.store(true);
                    continue;
                }
                break;
            }
        }
        isRunning.store(false);
    });
}

// 映射Metal纹理
void VideoRender::initMetalTextureCache(void *device){
    // CoreVideo 会创建新的 cache 引用，成功后必须写回成员变量。
    CVMetalTextureCacheRef cache = (CVMetalTextureCacheRef)textureCache;
    CVReturn status = CVMetalTextureCacheCreate(
        kCFAllocatorDefault, nullptr, (__bridge id<MTLDevice>)device,
                                        nullptr, &cache);
    if (status == kCVReturnSuccess && cache) {
        textureCache = cache;
    } else if (cache) {
        CFRelease(cache);
    }
}

MetalTextureBind VideoRender::mapFrame(void *pixel){
    MetalTextureBind textureBind{};
    if (!textureCache || !pixel) return textureBind;
    size_t width = CVPixelBufferGetWidth((CVPixelBufferRef)pixel);
    size_t height = CVPixelBufferGetHeight((CVPixelBufferRef)pixel);

    CVMetalTextureRef cvTexture = nullptr;
    CVReturn status = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        (CVMetalTextureCacheRef)textureCache,
        (CVPixelBufferRef)pixel,
        nullptr,
        MTLPixelFormatBGRA8Unorm,
        width,
        height,
        0,
        &cvTexture
    );

    if (status != kCVReturnSuccess || !cvTexture) return textureBind;
    id<MTLTexture> mtlTexture = CVMetalTextureGetTexture(cvTexture);
    if (!mtlTexture) {
        CFRelease(cvTexture);
        return textureBind;
    }

    textureBind.cvTexture = cvTexture;
    textureBind.mtlTexture = (__bridge void *)mtlTexture;
    return textureBind;
}

void VideoRender::clean(){
    stopDecodeThread();

    {
        // FrameSolt 内部保证队列操作线程安全；上层负责释放取出的 CVPixelBuffer。
        while (true) {
            MetalFrame frame = solt.popSolt();
            if (frame.pixel == nullptr) break;
            CVPixelBufferRelease((CVPixelBufferRef)frame.pixel);
        }
    }

    if (textureCache){
        CFRelease(static_cast<CVMetalTextureCacheRef>(textureCache));
        textureCache = nullptr;
    }

    if (metalRenderer){
        CFRelease(static_cast<CFTypeRef>(metalRenderer));
        metalRenderer = nullptr;
        if (metalRenderOutput) {
            CFRelease(static_cast<CFTypeRef>(metalRenderOutput));
        }
        metalRenderOutput = nullptr;
        return;
    }
}

// 停止解码线程
void VideoRender::stopDecodeThread(){
    if (!isRunning.load()) return;
    shouldStop.store(true);
    // 只有已启动且未 join 的线程才能调用 join。
    if (decodeThread.joinable()) {
        decodeThread.join();
    }

    isRunning.store(false);
}

void VideoRender::clearFrameSlot(){
    while (true) {
        MetalFrame frame = solt.popSolt();
        if (frame.pixel == nullptr) break;
        CVPixelBufferRelease((CVPixelBufferRef)frame.pixel);
    }
}

    // 从槽位中取一帧；队列内部保证出队操作原子。
MetalFrame VideoRender::takeFrameFromSlot(){
    // FrameSolt 内部加锁，保证出队是原子的。
    return solt.popSolt();
}

// 产生帧插入槽位
void VideoRender::insertFrameToSolt(void *pixel, double pts, bool startsNewTimeline){
    if (!pixel) return;
    // // 队列满时按“丢最新帧”处理；该缓冲已经 retain，必须立即释放。
    // if (solt.pushSolt(pixel, pts) != 0) {
    //     CVPixelBufferRelease((CVPixelBufferRef)pixel);
    // }

    // 控制解码速度防止解码速度大于播放速度
    while (!shouldStop.load() && solt.pushSolt(pixel, pts, startsNewTimeline) != 0){
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void Render::freeCvTexture(void *cvTexture){
    if (!cvTexture) return;
    CVMetalTextureRef cvTexture_ = static_cast<CVMetalTextureRef>(cvTexture);
    CFRelease(cvTexture_);
}
