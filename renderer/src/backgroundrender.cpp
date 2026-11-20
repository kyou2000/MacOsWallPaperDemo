#include "backgroundrender.h"
#include "framesolt.h"
#include "videorender.h"
#include <thread>
#include <objc/runtime.h>
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <CoreVideo/CVPixelBufferPool.h>
#include <QuartzCore/QuartzCore.hpp>
#include "utils.h"
using namespace MTL;

namespace Render{
    int BackgroundRender::createDevice(){
        // 统一使用 metal-cpp 创建和持有设备对象。
        device = (void *)MTL::CreateSystemDefaultDevice();
        if (!device){
            // throw std::runtime_error("device is not support !");
            printf("device is not support ! \n");
            return -1;
        }
        return 0;
    }

    int BackgroundRender::bindWindow(){
        if (!metalLayer){
            // throw std::runtime_error("MetalLayer error !");
            printf("MetalLayer error ! \n");
            return -1;
        }
        CA::MetalLayer* metalLayer_ = static_cast<CA::MetalLayer*>(metalLayer);
        // layer、CoreVideo 像素缓冲和 render pipeline 必须使用相同的像素格式。
        metalLayer_->setDevice(static_cast<Device *>(device));
        // 这个格式可以接受解码视频的格式MTL::PixelFormatBGRA8Unorm
        metalLayer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        // 设置飞行帧为3，这个性能是比较好的
        metalLayer_->setMaximumDrawableCount(backgroundMaximumDrawableCount);
        return 0;
    }

    int BackgroundRender::allocCommandQueue(){
        commandQueue = (void *)((Device *)device)->newCommandQueue();
        if (!commandQueue){
            // throw std::runtime_error("create commandQueue error !");
            printf("create commandQueue error ! \n");
            return -1;
        }
        return 0;
    }

    int BackgroundRender::buildShader(){
        std::string realPath = execPath + "../shader/background.metallib";
        NS::Error* pError = nullptr;
        Library* pLibrary = ((Device *)device)->newLibrary(NS::String::string(realPath.c_str(), NS::UTF8StringEncoding),
                                                                                &pError);

        if (!pLibrary){
            // throw std::runtime_error("pLibrary error !");
            printf("pLibrary error ! \n");
            return -1;
        }

        Function* pVertexFn = pLibrary->newFunction( NS::String::string("vertexMain", NS::StringEncoding::UTF8StringEncoding));
        Function* pFragFn = pLibrary->newFunction( NS::String::string("fragmentMain", NS::StringEncoding::UTF8StringEncoding));

        // 创建描述符
        RenderPipelineDescriptor* pDesc = RenderPipelineDescriptor::alloc()->init();
        pDesc->setVertexFunction(pVertexFn);
        pDesc->setFragmentFunction(pFragFn);
        // 与 CAMetalLayer / CVPixelBuffer 保持相同的 PixelFormatBGRA8Unorm 格式。
        // 如果是图片这里需要BGRA8Unorm
        if (mod == ShaderMode::ModeVideo){
            pDesc->colorAttachments()->object(0)->setPixelFormat(PixelFormat::PixelFormatBGRA8Unorm);
        } else if (mod == ShaderMode::ModePic){
            pDesc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
        }
        
        renderPipelineState = ((Device *)device)->newRenderPipelineState(pDesc, &pError);
        if (!renderPipelineState){
            // throw std::runtime_error("create renderPipelineState error !");
            printf("create renderPipelineState error ! \n");
            return -1;
        }

        // 清理资源
        pDesc->release();
        pFragFn->release();
        pVertexFn->release();
        pLibrary->release();
        return 0;
    }

    // 如果加载静态图片需要创建Texture
    int BackgroundRender::createTexture(){
        Image image;
        if (loadImage(image, filePath) < 0){
            return -1;
        }
        TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
        desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
        desc->setWidth(image.width);
        desc->setHeight(image.hight);
        desc->setUsage(MTL::TextureUsageShaderRead);

        Texture* texture_ = ((Device *)device)->newTexture(desc);
        if (!texture_){
            // throw std::runtime_error("create texture error !");
            printf("create texture error ! \n");
            return -1;
        }
        
        Region region(0, 0, 0, image.width, image.hight, 1);
        texture_->replaceRegion(region, 0, image.data, 4 * image.width);
        texture = (void *)texture_;
        desc->release();
        return 0;
    }

    void BackgroundRender::createSampler(){
        SamplerDescriptor* samplerDesc = SamplerDescriptor::alloc()->init();
        samplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
        samplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampler = ((Device *)device)->newSamplerState(samplerDesc);
        samplerDesc->release();
    }

    bool BackgroundRender::shouldPresentFrame(const MetalFrame& frame){
        const auto now = std::chrono::steady_clock::now();

        if (frame.startsNewTimeline || !presentationClockInitialized) {
            presentationClockInitialized = true;
            firstFramePts = frame.pts;
            firstFrameWallTime = now;
            return true;
        }

        const double pts = frame.pts;

        const auto frameTimeOffset = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(pts - firstFramePts)
        );
        const auto targetWallTime = firstFrameWallTime + frameTimeOffset;
        if (targetWallTime > now) {
            std::this_thread::sleep_until(targetWallTime);
        }
        return true;
    }

    void BackgroundRender::draw(){
        CA::MetalLayer *layer = static_cast<CA::MetalLayer *>(metalLayer);
        CGSize size = layer->drawableSize();
        int32_t windowHeight = static_cast<int32_t>(size.height);
        int32_t windowWidth = static_cast<int32_t>(size.width);

        if (windowHeight <= 0 || windowWidth <= 0){
            return;
        }
        // 不使用macOS的桌面需要手动对资源进行控制
        NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
        
        CA::MetalDrawable* drawable = layer->nextDrawable();
        if (!drawable) {
            pool->release();
            return;
        }

        MTL::Texture* texture_ = drawable->texture();
        MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::alloc()->init();
        MTL::RenderPassColorAttachmentDescriptor* ca = rpd->colorAttachments()->object(0);
        ca->setTexture(texture_);
        ca->setLoadAction(MTL::LoadActionClear);
        ca->setStoreAction(MTL::StoreActionStore);
        ca->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));

        MTL::CommandBuffer* cmd = ((CommandQueue *)commandQueue)->commandBuffer();
        MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rpd);
        
        // 设置纹理
        enc->setFragmentTexture((Texture *)texture, 0);
        enc->setFragmentSamplerState((SamplerState *)sampler, 0);
        // 
        enc->setRenderPipelineState((RenderPipelineState *)renderPipelineState);
        enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));

        MTL::Viewport vp{};
        vp.originX = 0.0;
        vp.originY = 0.0;
        vp.width   = static_cast<double>(windowWidth);
        vp.height  = static_cast<double>(windowHeight);
        vp.znear   = 0.0;
        vp.zfar    = 1.0;
        enc->setViewport(vp);
        enc->endEncoding();
        cmd->presentDrawable(drawable);
        cmd->commit();

        rpd->release();
        pool->release();
    }

    void BackgroundRender::drawVideoFrame(const VideoRender& render){
        CA::MetalLayer *layer = static_cast<CA::MetalLayer *>(metalLayer);
        CGSize size = layer->drawableSize();
        int32_t windowHeight = static_cast<int32_t>(size.height);
        int32_t windowWidth = static_cast<int32_t>(size.width);

        if (windowHeight <= 0 || windowWidth <= 0){
            return;
        }
        // 不使用macOS的桌面需要手动对资源进行控制
        NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
        
        CA::MetalDrawable* drawable = layer->nextDrawable();
        if (!drawable) {
            pool->release();
            return;
        }

        // 每次取出一帧，映射成 Metal 纹理后作为 fragment texture 0 提交给管线。
        VideoRender& videoRenderer = const_cast<VideoRender&>(render);
        MetalFrame curFrame = videoRenderer.takeFrameFromSlot();
        void* pixelBuffer = curFrame.pixel;
        if (!pixelBuffer) {
            pool->release();
            return;
        }
        
        if (!shouldPresentFrame(curFrame)) {
            CVPixelBufferRelease((CVPixelBufferRef)pixelBuffer);
            pool->release();
            return;
        }

        MetalTextureBind frameTexture = videoRenderer.mapFrame(pixelBuffer);
        if (!frameTexture.mtlTexture) {
            CVPixelBufferRelease((CVPixelBufferRef)pixelBuffer);
            if (frameTexture.cvTexture) freeCvTexture(frameTexture.cvTexture);
            pool->release();
            return;
        }

        MTL::Texture* texture_ = drawable->texture();
        MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::alloc()->init();
        MTL::RenderPassColorAttachmentDescriptor* ca = rpd->colorAttachments()->object(0);
        ca->setTexture(texture_);
        ca->setLoadAction(MTL::LoadActionClear);
        ca->setStoreAction(MTL::StoreActionStore);
        ca->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));

        MTL::CommandBuffer* cmd = ((CommandQueue *)commandQueue)->commandBuffer();
        MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(rpd);
        
        // 设置纹理
        enc->setFragmentTexture((Texture *)frameTexture.mtlTexture, 0);
        enc->setFragmentSamplerState((SamplerState *)sampler, 0);
        // 
        enc->setRenderPipelineState((RenderPipelineState *)renderPipelineState);
        enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));

        MTL::Viewport vp{};
        vp.originX = 0.0;
        vp.originY = 0.0;
        vp.width   = static_cast<double>(windowWidth);
        vp.height  = static_cast<double>(windowHeight);
        vp.znear   = 0.0;
        vp.zfar    = 1.0;
        enc->setViewport(vp);
        enc->endEncoding();
        cmd->presentDrawable(drawable);
        if (mod == ShaderMode::ModeVideo){
            cmd->addCompletedHandler([frameTexture, pixelBuffer](MTL::CommandBuffer* commandBuffer) {
            // commit 是异步提交；帧资源必须等 command buffer 完成后再释放。
            if (pixelBuffer) CVPixelBufferRelease((CVPixelBufferRef)pixelBuffer);
            if (frameTexture.cvTexture) freeCvTexture(frameTexture.cvTexture);
        });
        }
        
        cmd->commit();
        rpd->release();
        pool->release();
    }

    int BackgroundRender::init(void *metalLayer, ShaderMode mod, VideoRender& videoRender){
        this->metalLayer = metalLayer;
        this->mod = mod;
        if (createDevice() < 0) return -1;
        if (bindWindow() < 0) return -1;
        if (allocCommandQueue() < 0) return -1;
        if (buildShader() < 0) return -1;
        if (mod == ShaderMode::ModePic){
            if (createTexture() < 0)
                return -1;
        }
        createSampler();
        videoRender.initMetalTextureCache(device);
        return 0;
    }

    void BackgroundRender::destroy(){
        if (sampler){
            ((SamplerState *)sampler)->release();
            sampler = nullptr;
        }

        if (texture){
            ((Texture *)texture)->release();
            texture = nullptr;
        }

        if (renderPipelineState){
            ((RenderPipelineState *)renderPipelineState)->release();
            renderPipelineState = nullptr;
        }

        if (commandQueue){
            ((CommandQueue *)commandQueue)->release();
            commandQueue = nullptr;
        }

        if (device){
            ((Device *)device)->release();
            device = nullptr;
        }
    }

    void BackgroundRender::setFilepath(const std::string& file){
        this->filePath = file;
    }

    void BackgroundRender::setExecPath(const std::string& execPath){
        this->execPath = execPath;
    }
}
