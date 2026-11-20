#pragma once
#include <thread>
#include <atomic>
#include <string>
#include "framesolt.h"

namespace Render{
    struct MetalTextureBind{
        void *cvTexture = nullptr;
        void *mtlTexture = nullptr;
    };

    class VideoRender{
        public:
            VideoRender();
            ~VideoRender();
            int loadVideo(const char* path);
            void startDecodeThread();
            void initMetalTextureCache(void *device);
            MetalTextureBind mapFrame(void *pixelBuffer);
            MetalFrame takeFrameFromSlot();
            void clean();
            void stopDecodeThread();

        private:
            bool reloadReader();
            bool decode();
            void insertFrameToSolt(void *pixel, double pts, bool startsNewTimeline);
            void clearFrameSlot();

        private:
            void *metalRenderer = nullptr;
            void *metalRenderOutput = nullptr;
            void *textureCache = nullptr;
            FrameSolt solt;
            std::string videoPath;
            std::atomic<bool> shouldStop{false};
            std::atomic<bool> shouldSwitch{false};
            std::atomic<bool> nextStartsNewTimeline{false};
            std::atomic<bool> isRunning{false};
            std::thread decodeThread;
    };

    void freeCvTexture(void *cvTexture);
}
