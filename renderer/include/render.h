#pragma once

#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_metal.h"
#include "videorender.h"

#include <chrono>
#include <cstdint>

namespace Render{
    static constexpr uint32_t maximumDrawableCount = 3;
    class MetalRender{
        public:
            MetalRender() = default;

            ~MetalRender()=default;

            void draw(const VideoRender& render);

            void destroy();

            void init(SDL_Window* window_, VideoRender& videoRenderer);

        private:
            void createDevice();
            void bindWindow();
            void allocCommandQueue();
            void buildShader();
            void buildBuffer();
            void destroyBuffer();
            void createTexture();
            void createSampler();
            bool shouldPresentFrame(const MetalFrame& frame);
        
        private:
            void *device = nullptr;
            void *metalLayer = nullptr;
            void *commandQueue = nullptr;
            void *renderPipelineState = nullptr;
            void *vertexBuffer = nullptr;
            void *colorBuffer = nullptr;
            void *metalView = nullptr;
            void *sampler = nullptr;
            void *texture = nullptr;
            SDL_Window *window = nullptr;
            std::chrono::steady_clock::time_point firstFrameWallTime{};
            double firstFramePts = 0.0;
            bool presentationClockInitialized = false;
    };
}
