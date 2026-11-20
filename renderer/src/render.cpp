#include "render.h"
#include <objc/runtime.h>
#include <cstdint>
#include <stdexcept>
#include <array>
#include <string>
#include <CoreVideo/CVPixelBufferPool.h>
#include "utils.h"

#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

using namespace MTL;
namespace Render{
    struct Vec4f{
        public:
            Vec4f(float x_, float y_, float z_, float w_) : 
                x(x_), y(y_), z(z_), w(w_){}
        float x, y, z, w;
    };

    void MetalRender::createDevice(){
        // 统一使用 metal-cpp 创建和持有设备对象。
        device = (void *)MTL::CreateSystemDefaultDevice();
        if (!device){
            throw std::runtime_error("device is not support !");
        }
    }
 
    void MetalRender::bindWindow(){
        metalView = SDL_Metal_CreateView(window);
        if (!metalView){
            throw std::runtime_error("SDL_Metal_CreateView error !");
        }
        void* layerPtr = SDL_Metal_GetLayer(metalView);
        metalLayer = layerPtr;
        if (!layerPtr){
            throw std::runtime_error("SDL_Metal_GetLayer error !");
        }
        CA::MetalLayer* metalLayer_ = static_cast<CA::MetalLayer*>(layerPtr);
        // layer、CoreVideo 像素缓冲和 render pipeline 必须使用相同的像素格式。
        metalLayer_->setDevice(static_cast<Device *>(device));
        metalLayer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        // 设置飞行帧为3，这个性能是比较好的
        metalLayer_->setMaximumDrawableCount(maximumDrawableCount);
    }

    void MetalRender::allocCommandQueue(){
        commandQueue = (void *)((Device *)device)->newCommandQueue();
        if (!commandQueue){
            throw std::runtime_error("create commandQueue error !");
        }
    }

    void MetalRender::buildShader(){
        NS::Error* pError = nullptr;
        Library* pLibrary = ((Device *)device)->newLibrary(NS::String::string("../shader/background.metallib", NS::UTF8StringEncoding),
                                                                                &pError);

        if (!pLibrary){
            throw std::runtime_error("pLibrary error !");
        }

        Function* pVertexFn = pLibrary->newFunction( NS::String::string("vertexMain", NS::StringEncoding::UTF8StringEncoding));
        Function* pFragFn = pLibrary->newFunction( NS::String::string("fragmentMain", NS::StringEncoding::UTF8StringEncoding));

        // 创建描述符
        RenderPipelineDescriptor* pDesc = RenderPipelineDescriptor::alloc()->init();
        pDesc->setVertexFunction(pVertexFn);
        pDesc->setFragmentFunction(pFragFn);
        // 与 CAMetalLayer / CVPixelBuffer 保持相同的 BGRA8Unorm 格式。
        pDesc->colorAttachments()->object(0)->setPixelFormat(PixelFormat::PixelFormatBGRA8Unorm);
        renderPipelineState = ((Device *)device)->newRenderPipelineState(pDesc, &pError);
        if (!renderPipelineState){
            throw std::runtime_error("create renderPipelineState error !");
        }

        // 清理资源
        pDesc->release();
        pFragFn->release();
        pVertexFn->release();
        pLibrary->release();
    }

    // 创建纹理
    void MetalRender::createTexture(){
        Image image;
        loadImage(image, "../texture/a.jpg");
        TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
        desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
        desc->setWidth(image.width);
        desc->setHeight(image.hight);
        desc->setUsage(MTL::TextureUsageShaderRead);

        Texture* texture_ = ((Device *)device)->newTexture(desc);
        if (!texture_){
            throw std::runtime_error("create texture error !");
        }
        
        Region region(0, 0, 0, image.width, image.hight, 1);
        texture_->replaceRegion(region, 0, image.data, 4 * image.width);
        texture = (void *)texture_;
        desc->release();
    }

    // 创建采样器
    void MetalRender::createSampler(){
        SamplerDescriptor* samplerDesc = SamplerDescriptor::alloc()->init();
        samplerDesc->setMinFilter(MTL::SamplerMinMagFilterLinear);
        samplerDesc->setMagFilter(MTL::SamplerMinMagFilterLinear);
        sampler = ((Device *)device)->newSamplerState(samplerDesc);
        samplerDesc->release();
    }

    void MetalRender::buildBuffer(){
        std::array<Vec4f, 3> positions = {
            Vec4f(-0.8f,  0.8f, 0.0f, 1.0f), 
            Vec4f(0.0f, -0.8f, 0.0f, 1.0f),
            Vec4f(+0.8f,  0.8f, 0.0f, 1.0f)
        };

        std::array<Vec4f, 3> colors = {
            Vec4f(1.0f, 0.3f, 0.2f, 1.0f), 
            Vec4f(0.8f, 1.0f, 0.0f, 1.0f),
            Vec4f(0.8f, 0.0f, 1.0f, 1.0f)
        };

        size_t positionsDataSize = positions.size() * sizeof(Vec4f);
        size_t colorDataSize = colors.size() * sizeof(Vec4f);

        vertexBuffer = ((Device *)device)->newBuffer(positionsDataSize, MTL::ResourceStorageModeManaged);
        colorBuffer = ((Device *)device)->newBuffer(colorDataSize, MTL::ResourceStorageModeManaged);

        memcpy(((Buffer *)vertexBuffer)->contents(), positions.data(), positionsDataSize);
        memcpy(((Buffer *)colorBuffer)->contents(), colors.data(), colorDataSize);

        ((Buffer *)vertexBuffer)->didModifyRange( NS::Range::Make( 0, ((Buffer *)vertexBuffer)->length()));
        ((Buffer *)colorBuffer)->didModifyRange( NS::Range::Make( 0, ((Buffer *)colorBuffer)->length()));
    }

    void MetalRender::destroyBuffer(){
        ((Buffer *)vertexBuffer)->release();
        ((Buffer *)colorBuffer)->release();
    }

    bool MetalRender::shouldPresentFrame(const MetalFrame& frame){
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

    void MetalRender::draw(const VideoRender& render){
        int32_t windowHeight = 0;
        int32_t windowWidth = 0;
        SDL_GetWindowSizeInPixels(window, &windowWidth, &windowHeight);

        if (windowHeight <= 0 || windowWidth <= 0){
            return;
        }
        // 不使用macOS的桌面需要手动对资源进行控制
        NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
        
        CA::MetalDrawable* drawable = ((CA::MetalLayer*)metalLayer)->nextDrawable();
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
        enc->setVertexBuffer((Buffer *)vertexBuffer, 0, 0);
        enc->setVertexBuffer((Buffer *)colorBuffer, 0, 1);
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
        cmd->addCompletedHandler([frameTexture, pixelBuffer](MTL::CommandBuffer* commandBuffer) {
            // commit 是异步提交；帧资源必须等 command buffer 完成后再释放。
            if (pixelBuffer) CVPixelBufferRelease((CVPixelBufferRef)pixelBuffer);
            if (frameTexture.cvTexture) freeCvTexture(frameTexture.cvTexture);
        });
        cmd->commit();

        rpd->release();
        pool->release();
    }

    

    void MetalRender::init(SDL_Window* window_, VideoRender& videoRenderer){
        window = window_;
        createDevice();
        bindWindow();
        allocCommandQueue();
        buildShader();
        buildBuffer();
        createSampler();
        videoRenderer.initMetalTextureCache(device);
    }

    void MetalRender::destroy(){
        if (sampler){
            ((SamplerState *)sampler)->release();
        }

        if (texture){
            ((Texture *)texture)->release();
        }

        destroyBuffer();

        if (renderPipelineState){
            ((RenderPipelineState *)renderPipelineState)->release();
        }

        if (commandQueue){
            ((CommandQueue *)commandQueue)->release();
        }

        if (metalView){
            SDL_Metal_DestroyView(metalView);
        }
        
        if (device){
            ((Device *)device)->release();
        }
    }
}
