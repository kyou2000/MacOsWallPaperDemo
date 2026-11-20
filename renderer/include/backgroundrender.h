#pragma once
#include <cstdint>
#include <chrono>
#include <string>
namespace Render{
    struct MetalFrame;
    struct VideoRender;

    enum class ShaderMode : uint8_t{
        ModePic,
        ModeVideo
    };

    class BackgroundRender{
        static constexpr uint32_t backgroundMaximumDrawableCount = 3;
        public:
            BackgroundRender() = default;
            ~BackgroundRender() = default;
            void setFilepath(const std::string& file);
            int createDevice();
            int bindWindow();
            int allocCommandQueue();
            int buildShader();
            int createTexture();
            void createSampler();
            void draw();
            void drawVideoFrame(const VideoRender& render);
            int init(void *metalLayer, ShaderMode mod, VideoRender& videoRender);
            void destroy();
            void setExecPath(const std::string& execPath);
        private:
            bool shouldPresentFrame(const MetalFrame& frame);
        private:
            std::string execPath;
            std::string filePath;
            void *metalLayer = nullptr;
            void *device = nullptr;
            void *commandQueue = nullptr;
            void *renderPipelineState = nullptr;
            void *texture = nullptr;
            void *sampler = nullptr;
            std::chrono::steady_clock::time_point firstFrameWallTime{};
            double firstFramePts = 0.0;
            bool presentationClockInitialized = false;
            ShaderMode mod;
    };
}