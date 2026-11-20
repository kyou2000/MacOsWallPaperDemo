#include "application.h"
#include "render.h"

namespace Render{
    void Application::run(){
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return;
        }

        SDL_Window* window = SDL_CreateWindow(
            "SDL3 Window",
            800,
            600,
            SDL_WINDOW_RESIZABLE
        );

        if (!window) {
            SDL_Log("CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return;
        }

        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

        // 视频解码器
        VideoRender videoRender;
        videoRender.loadVideo("../video/test.mp4");

        MetalRender render;
        render.init(window, videoRender);

        // 先创建 CoreVideo/Metal 纹理缓存，再启动异步解码；
        // 这样解码出的 CVPixelBuffer 可以立即进入渲染链路。
        videoRender.startDecodeThread();

        bool running = true;
        SDL_Event event;

        while (running) {
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
            }

            render.draw(videoRender);
        }

        render.destroy(); // 这步要先做
        videoRender.stopDecodeThread();
        SDL_DestroyWindow(window);
        SDL_Quit();
        videoRender.clean();
    }
}
