#include <SDL3/SDL.h>
#include <SDL3/SDL_messagebox.h>
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <mach-o/dyld.h>
#include <limits.h>
#include <libgen.h>
#include "childprocess.h"
#include "videodecode.h"
#include "loadconfig.h"
#include "uiutils.h"
#ifdef ENABLE_LOGGING
    #include "uihelper.h"
#endif

#define WALLPAPER_STATIC 0
#define WALLPAPER_DYNAMIC 1

namespace fs = std::filesystem;
using namespace UI;

namespace {
static bool isWallPaperRunning = false; // 标记当前壁纸是否在渲染
static std::chrono::steady_clock::time_point firstFrameWallTime{};
static double firstFramePts = 0.0;
static bool isFirstFrame = true;
static std::string curFile; // 用户当前选择的文件
static std::string preFile;
// 1视频，0图片
static int type;
static std::atomic<bool> tag = {false};
static std::thread task;

struct Button {
    SDL_FRect bounds{};
    SDL_Color normal{};
    SDL_Color hover{};
    SDL_Color symbol{};
    bool hovered = false;
    bool isClose = false;
    bool showSymbol = false;
    const char* label = "";
};

Button MakeOpenFileButton(){
    Button button{{16.0f, 48.0f, 82.0f, 28.0f},
                  SDL_Color{255, 255, 255, 34}, SDL_Color{255, 255, 255, 64},
                  SDL_Color{24, 26, 32, 245}};
    button.label = "打开文件";
    return button;
}

Button MakeCloseButton(float){
    Button button{{20.0f, 18.0f, 12.0f, 12.0f},
                  SDL_Color{255, 96, 92, 235}, SDL_Color{255, 96, 92, 255},
                  SDL_Color{77, 22, 21, 245}};
    button.isClose = true;
    return button;
}

Button MakeMinimizeButton(float){
    Button button{{38.0f, 18.0f, 12.0f, 12.0f},
                  SDL_Color{255, 190, 74, 235}, SDL_Color{255, 190, 74, 255},
                  SDL_Color{82, 56, 14, 245}, false};
    button.showSymbol = true;
    return button;
}

Button MakeFullscreenButton(float){
    Button button{{56.0f, 18.0f, 12.0f, 12.0f},
                  SDL_Color{120, 122, 128, 235}, SDL_Color{120, 122, 128, 235},
                  SDL_Color{56, 58, 62, 245}, false};
    button.showSymbol = true;
    return button;
}

Button MakeActionButton(float windowWidth, const SDL_FRect& previewBounds,
                        float offsetX, const char* label, SDL_Color hover){
    Button button{{previewBounds.x + offsetX, previewBounds.y + previewBounds.h + 34.0f,
                   126.0f, 42.0f}, SDL_Color{255, 255, 255, 34}, hover,
                  SDL_Color{24, 26, 32, 245}};
    button.label = label;
    return button;
}

void DrawPreview(SDL_Renderer* renderer, const SDL_FRect& bounds){
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 38, 42, 52, 255);
    SDL_RenderFillRect(renderer, &bounds);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 44);
    SDL_RenderRect(renderer, &bounds);

    SDL_FRect placeholder{
        bounds.x + bounds.w * 0.28f,
        bounds.y + bounds.h * 0.22f,
        bounds.w * 0.44f,
        bounds.h * 0.56f
    };
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
    SDL_RenderRect(renderer, &placeholder);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 58);
    SDL_RenderLine(renderer, placeholder.x, placeholder.y,
                   placeholder.x + placeholder.w, placeholder.y + placeholder.h);
    SDL_RenderLine(renderer, placeholder.x + placeholder.w, placeholder.y,
                   placeholder.x, placeholder.y + placeholder.h);
}

void UpdatePreviewTexture(SDL_Renderer* renderer, SDL_Texture** texture,
                          int* textureWidth, int* textureHeight,
                          const VideoFrame& frame){
    if (frame.data.empty() || frame.width <= 0 || frame.height <= 0){
        return;
    }
    if (*texture && (*textureWidth != frame.width || *textureHeight != frame.height)){
        SDL_DestroyTexture(*texture);
        *texture = nullptr;
    }

    if (!*texture){
        *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     frame.width, frame.height);
        if (!*texture){
            std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
            return;
        }
        *textureWidth = frame.width;
        *textureHeight = frame.height;
    }

    SDL_UpdateTexture(*texture, nullptr, frame.data.data(),
                      frame.width * 4);
}

void UpdatePreviewTexture(SDL_Renderer* renderer, SDL_Texture** texture,
                          int* textureWidth, int* textureHeight,
                          const ImageData& image){
    if (!image.data || image.width <= 0 || image.height <= 0){
        return;
    }
    if (*texture && (*textureWidth != image.width || *textureHeight != image.height)){
        SDL_DestroyTexture(*texture);
        *texture = nullptr;
    }

    if (!*texture){
        *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     image.width, image.height);
        if (!*texture){
            std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
            return;
        }
        *textureWidth = image.width;
        *textureHeight = image.height;
    }

    SDL_UpdateTexture(*texture, nullptr, image.data,
                      image.width * 4);
}

void DestroyPreviewTexture(SDL_Texture** texture, int* textureWidth,
                           int* textureHeight){
    if (*texture){
        SDL_DestroyTexture(*texture);
        *texture = nullptr;
    }
    *textureWidth = 0;
    *textureHeight = 0;
}

void DrawPreview(SDL_Renderer* renderer, const SDL_FRect& bounds,
                 SDL_Texture* texture, int textureWidth, int textureHeight){
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 38, 42, 52, 255);
    SDL_RenderFillRect(renderer, &bounds);

    if (texture){
        const float scale = std::min(bounds.w / textureWidth,
                                     bounds.h / textureHeight);
        const SDL_FRect frameBounds{
            bounds.x + (bounds.w - textureWidth * scale) * 0.5f,
            bounds.y + (bounds.h - textureHeight * scale) * 0.5f,
            textureWidth * scale,
            textureHeight * scale
        };
        SDL_RenderTexture(renderer, texture, nullptr, &frameBounds);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 44);
        SDL_RenderRect(renderer, &bounds);
        return;
    }

    SDL_FRect placeholder{
        bounds.x + bounds.w * 0.28f,
        bounds.y + bounds.h * 0.22f,
        bounds.w * 0.44f,
        bounds.h * 0.56f
    };
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 100);
    SDL_RenderRect(renderer, &placeholder);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 58);
    SDL_RenderLine(renderer, placeholder.x, placeholder.y,
                   placeholder.x + placeholder.w, placeholder.y + placeholder.h);
    SDL_RenderLine(renderer, placeholder.x + placeholder.w, placeholder.y,
                   placeholder.x, placeholder.y + placeholder.h);
}

void DrawRectOutline(SDL_Renderer* renderer, const SDL_FRect& bounds,
                     float thickness){
    const SDL_FRect top{bounds.x, bounds.y, bounds.w, thickness};
    const SDL_FRect bottom{bounds.x, bounds.y + bounds.h - thickness,
                           bounds.w, thickness};
    const SDL_FRect left{bounds.x, bounds.y, thickness, bounds.h};
    const SDL_FRect right{bounds.x + bounds.w - thickness, bounds.y,
                          thickness, bounds.h};
    SDL_RenderFillRect(renderer, &top);
    SDL_RenderFillRect(renderer, &bottom);
    SDL_RenderFillRect(renderer, &left);
    SDL_RenderFillRect(renderer, &right);
}

// 增加提示窗口
void DrawMessageWindow(const std::string& msg, SDL_Window* window){
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "提示",
        msg.c_str(),
        window   // 父窗口，没有可传 NULL
    );
}

void DrawChineseLabel(SDL_Renderer* renderer, const char* text,
                      const SDL_FRect& bounds, SDL_Color color){
    // 1. 把 UTF-8 字符串转成 CoreText 可识别的 CFStringRef。
    CFStringRef textString = CFStringCreateWithCString(
        kCFAllocatorDefault, text, kCFStringEncodingUTF8);
    if (!textString){
        return;
    }

    // 2. 指定 PingFang SC 字体，并按 13pt 创建文字样式。
    CFStringRef fontName = CFStringCreateWithCString(
        kCFAllocatorDefault, "PingFang SC", kCFStringEncodingUTF8);
    CTFontRef font = CTFontCreateWithName(fontName, 13.0, nullptr);
    CFRelease(fontName);

    // 3. 构建带字体属性的富文本对象。
    CFMutableAttributedStringRef attributedString =
        CFAttributedStringCreateMutable(kCFAllocatorDefault, 0);
    CFAttributedStringReplaceString(attributedString, CFRangeMake(0, 0),
                                    textString);
    CFAttributedStringSetAttribute(
        attributedString, CFRangeMake(0, CFStringGetLength(textString)),
        kCTFontAttributeName, font);
    CFRelease(textString);
    CFRelease(font);

    CTLineRef line = CTLineCreateWithAttributedString(attributedString);
    CFRelease(attributedString);
    if (!line){
        return;
    }

    CGFloat ascent = 0.0;
    CGFloat descent = 0.0;
    CGFloat leading = 0.0;
    // 4. 测量这一行文字的宽度和上伸/下伸/行距。
    const CGFloat textWidth = CTLineGetTypographicBounds(
        line, &ascent, &descent, &leading);
    if (textWidth <= 0.0 || bounds.w <= 0.0 || bounds.h <= 0.0){
        CFRelease(line);
        return;
    }

    const int surfaceWidth = static_cast<int>(std::ceil(textWidth));
    const int surfaceHeight = static_cast<int>(
        std::ceil(ascent + descent + leading));
    // 5. 按测量结果创建一块每像素 4 字节的内存位图。
    const size_t bytesPerRow = static_cast<size_t>(surfaceWidth) * 4;
    std::vector<uint8_t> bitmap(bytesPerRow * surfaceHeight, 0);

    // 6. 用这块内存创建 RGB 位图上下文，后续 CoreText 直接绘制到里面。
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmapContext = CGBitmapContextCreate(
        bitmap.data(), surfaceWidth, surfaceHeight, 8, bytesPerRow,
        colorSpace,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(colorSpace);
    if (!bitmapContext){
        CFRelease(line);
        return;
    }

    // 7. 设置文字颜色；由于 bitmap 默认就是 top-left 坐标，
    //    不需要再做 Y 轴翻转。
    CGContextSetRGBFillColor(bitmapContext, color.r / 255.0,
                             color.g / 255.0, color.b / 255.0,
                             color.a / 255.0);
    // 8. 从左下角基线处绘制一行文字。
    CTLineDraw(line, bitmapContext);
    CGContextFlush(bitmapContext);

    // 9. 把 CoreText 产生的 ARGB 位图包装成 SDL Surface。
    SDL_Surface* textSurface = SDL_CreateSurfaceFrom(
        surfaceWidth, surfaceHeight, SDL_PIXELFORMAT_ARGB8888,
        bitmap.data(), static_cast<int>(bytesPerRow));
    if (textSurface){
        SDL_SetSurfaceBlendMode(textSurface, SDL_BLENDMODE_BLEND);
        SDL_Texture* textTexture =
            SDL_CreateTextureFromSurface(renderer, textSurface);
        SDL_DestroySurface(textSurface);
        if (textTexture){
            // 10. 把 Surface 上传成 GPU 纹理，并保留透明通道。
            SDL_SetTextureBlendMode(textTexture, SDL_BLENDMODE_BLEND);
            constexpr float kLabelTextOpticalOffset = -5.0f;
            // 11. 计算文字在按钮中的水平居中、垂直居中并向上微调的位置。
            const SDL_FRect destination{
                bounds.x + (bounds.w - static_cast<float>(textWidth)) * 0.5f,
                bounds.y + (bounds.h - surfaceHeight) * 0.5f + kLabelTextOpticalOffset,
                static_cast<float>(surfaceWidth),
                static_cast<float>(surfaceHeight)
            };
            // 12. 把文字纹理绘制到当前 SDL 渲染目标上。
            SDL_RenderTexture(renderer, textTexture, nullptr, &destination);
            SDL_DestroyTexture(textTexture);
        }
    }

    // 13. 释放 CoreText/CoreGraphics 资源，避免内存泄漏。
    CFRelease(bitmapContext);
    CFRelease(line);
}

bool Contains(const SDL_FRect& bounds, SDL_FPoint point){
    return point.x >= bounds.x && point.x < bounds.x + bounds.w &&
           point.y >= bounds.y && point.y < bounds.y + bounds.h;
}

SDL_HitTestResult WindowHitTest(SDL_Window* window,
                                const SDL_Point* point,
                                void* userData){
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &width, &height);

    const float x = static_cast<float>(point->x);
    const float y = static_cast<float>(point->y);
    const float windowWidth = static_cast<float>(width);
    const SDL_FPoint windowPoint{x, y};
    const Button openFileButton = MakeOpenFileButton();
    const Button closeButton = MakeCloseButton(windowWidth);
    const Button minimizeButton = MakeMinimizeButton(windowWidth);
    const Button fullscreenButton = MakeFullscreenButton(windowWidth);
    if (Contains(closeButton.bounds, windowPoint) ||
        Contains(minimizeButton.bounds, windowPoint) ||
        Contains(fullscreenButton.bounds, windowPoint) ||
        Contains(openFileButton.bounds, windowPoint)){
        return SDL_HITTEST_NORMAL;
    }

    if (y < 44.0f){
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

void DrawButton(SDL_Renderer* renderer, const Button& button, bool hovered){
    const SDL_Color& color = hovered ? button.hover : button.normal;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &button.bounds);

    if (!hovered)
        return;

    const float centerX = button.bounds.x + button.bounds.w * 0.5f;
    const float centerY = button.bounds.y + button.bounds.h * 0.5f;
    SDL_SetRenderDrawColor(renderer, button.symbol.r, button.symbol.g,
                           button.symbol.b, button.symbol.a);
    if (button.isClose){
        constexpr float symbolSize = 3.4f;
        SDL_RenderLine(renderer, centerX - symbolSize, centerY - symbolSize,
                       centerX + symbolSize, centerY + symbolSize);
        SDL_RenderLine(renderer, centerX - symbolSize, centerY + symbolSize,
                       centerX + symbolSize, centerY - symbolSize);
    }
    else if (button.showSymbol){
        if (std::strcmp(button.label, "fullscreen") == 0){
            constexpr float triangleRadius = 3.4f;
            SDL_RenderLine(renderer, centerX - triangleRadius,
                           centerY + triangleRadius * 0.7f,
                           centerX + triangleRadius,
                           centerY + triangleRadius * 0.7f);
            SDL_RenderLine(renderer, centerX - triangleRadius,
                           centerY + triangleRadius * 0.7f, centerX,
                           centerY - triangleRadius * 0.9f);
            SDL_RenderLine(renderer, centerX + triangleRadius,
                           centerY + triangleRadius * 0.7f, centerX,
                           centerY - triangleRadius * 0.9f);
        } else {
            const SDL_FRect symbol{centerX - 3.4f, centerY - 1.1f, 6.8f, 2.2f};
            SDL_RenderFillRect(renderer, &symbol);
        }
    }
}

void DrawLabel(SDL_Renderer* renderer, const Button& button){
    const SDL_Color color = button.hovered ? SDL_Color{24, 26, 32, 245}
                                           : SDL_Color{236, 238, 244, 230};
    const SDL_FRect textBounds{
        button.bounds.x,
        button.bounds.y + (button.bounds.h - 27.0f) * 0.5f,
        button.bounds.w,
        27.0f
    };
    DrawChineseLabel(renderer, button.label, textBounds, color);
}

bool shouldPresentFrame(const VideoFrame& frame){
    const auto curTimes = std::chrono::steady_clock::now();
    if (isFirstFrame){
        firstFrameWallTime = curTimes;
        firstFramePts = frame.pts;
        isFirstFrame = false;
    }

    const double pts = frame.pts;
    const auto frameTimeOffset = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(pts - firstFramePts)
    );
    const auto targetWallTime = firstFrameWallTime + frameTimeOffset;
    if (targetWallTime > curTimes) {
        std::this_thread::sleep_until(targetWallTime);
    }
    return true;
}

struct FileSelectionState {
    bool requested = false;
    std::string selectedFile;
    std::mutex fileMtx;
};

// 文件选择窗口回调函数
void SDLCALL HandleSelectedFile(void* userData, const char* const* fileList, int){
    auto* selection = static_cast<FileSelectionState*>(userData);
    if (!fileList || !fileList[0]){
        return;
    }

    std::lock_guard<std::mutex> lock(selection->fileMtx);
    selection->selectedFile = std::move(fileList[0]);
    selection->requested = true;
}

static std::string getRealExecPath(){
    char buffer[PATH_MAX];
    uint32_t pathSize = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &pathSize) != 0) return ".";
    char resolved[PATH_MAX];
    if (!realpath(buffer, resolved)) return ".";

    fs::path p = fs::canonical(buffer);
    while (p.has_parent_path()) {
        if (p.extension() == ".app")
            return p.parent_path();
        p = p.parent_path();
    }
    return ".";
}

static bool fileIsExist(const std::string& path){
    return fs::exists(path);
}
}

int main(int argc, char* argv[]){
    // 定位子进程程序位置
    std::string root = getRealExecPath();
    std::string childPath = root + "/WallPaperRenderer";
    std::string cfgPath = root + "/../config/wallpaper_cfg.json";

#ifdef ENABLE_LOGGING
    // 初始化日志
    UI::initLoger();
    UI::uiLog("starting !");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)){
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("WallPaper", 1024, 680,
                                          SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window){
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowHitTest(window, WindowHitTest, nullptr);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer){
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    bool minimizeRequested = false;
    bool closeRequested = false;
    bool leftDown = false;
    bool clicked = false;
    VideoFrame frame;
    SDL_FPoint mousePosition{512.0f, 340.0f};
    FileSelectionState fileSelection;
    ImageData buffer; // 静态图片数据

    // 加载配置
    Config cfg;
    if (!fileIsExist(cfgPath)){
        cfg.filePath = "";
        cfg.isFirst = true;
        cfg.writeBack(cfgPath);
    }
#ifdef ENABLE_LOGGING
    UI::uiLog(cfgPath);
#endif
    if (cfg.loadConfig(cfgPath) < 0){
        DrawMessageWindow("配置文件加载异常!", window);
    }

    // 前端预览解码
    curFile = cfg.filePath;
#ifdef ENABLE_LOGGING
    UI::uiLog(curFile);
#endif
    bool wpRenderStart = false;
    preFile = curFile;
    type = getFileType(curFile);
    VideoDecode viewDecode;
    if (curFile != "" && type >= 0){
        if (type == WALLPAPER_DYNAMIC){
            viewDecode.startDecodeThread(curFile);
            wpRenderStart = true;
        } else if(type == WALLPAPER_STATIC){
            if (loadImage(buffer, curFile) < 0){
                DrawMessageWindow("文件加载异常!", window);
            }
        } else {
            DrawMessageWindow("文件加载异常!", window);
        }
    } else {
        if (!cfg.isFirst){
            DrawMessageWindow("文件加载异常!", window);
        }
    }
    
    SDL_Texture* previewTexture = nullptr;
    int previewTextureWidth = 0;
    int previewTextureHeight = 0;
    
    ChildProcess wallpPaperbackend; // 后端渲染进程
    // 刚开始启动程序的时候根据历史记录的进行启动渲染
    if (wpRenderStart){
        std::vector<std::string> args = { curFile, std::to_string(type) };
        if (wallpPaperbackend.lunch(childPath, args) == 0){
            isWallPaperRunning = true;
        } else {
            DrawMessageWindow("壁纸启动失败!", window);
#ifdef ENABLE_LOGGING
            uiError("wallpaper render start error !");
#endif
        }
        
    } else {
        if (!cfg.isFirst){
            DrawMessageWindow("当前没有可支持的文件!", window);
        }
    }
    

    while (running){
        SDL_Event event;
        while (SDL_PollEvent(&event)){
            switch (event.type){
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                leftDown = event.button.button == SDL_BUTTON_LEFT;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT){
                    leftDown = false;
                    clicked = true;
                    mousePosition = SDL_FPoint{event.button.x, event.button.y};
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                mousePosition = SDL_FPoint{event.motion.x, event.motion.y};
                break;

            default:
                break;
            }
        }

        if (type == WALLPAPER_DYNAMIC){
            frame = viewDecode.takeFrame();
            if (!frame.data.empty()){
                shouldPresentFrame(frame); // 限制播放速度
                UpdatePreviewTexture(renderer, &previewTexture,
                                    &previewTextureWidth, &previewTextureHeight,
                                    frame);
            }
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window, &width, &height);
        SDL_SetRenderScale(renderer,
                           SDL_GetWindowPixelDensity(window),
                           SDL_GetWindowPixelDensity(window));
        Button openFileButton = MakeOpenFileButton();
        const Button closeButton = MakeCloseButton(static_cast<float>(width));
        const Button minimizeButton = MakeMinimizeButton(static_cast<float>(width));
        const Button fullscreenButton = MakeFullscreenButton(static_cast<float>(width));
        const float previewWidth = static_cast<float>(width) * 0.68f;
        const float previewHeight = static_cast<float>(height) * 0.52f;
        const SDL_FRect previewBounds{
            static_cast<float>(width - previewWidth) * 0.5f,
            112.0f,
            previewWidth,
            previewHeight
        };
        Button startButton = MakeActionButton(
            static_cast<float>(width), previewBounds,
            previewWidth * 0.5f - 126.0f - 12.0f, "设置为当前壁纸",
            SDL_Color{88, 182, 121, 245});
        Button stopButton = MakeActionButton(
            static_cast<float>(width), previewBounds,
            previewWidth * 0.5f + 12.0f, "停止壁纸播放",
            SDL_Color{255, 96, 92, 245});
        startButton.hovered = Contains(startButton.bounds, mousePosition);
        stopButton.hovered = Contains(stopButton.bounds, mousePosition);
        openFileButton.hovered = Contains(openFileButton.bounds, mousePosition);
        const bool closeHover = Contains(closeButton.bounds, mousePosition);
        const bool minimizeHover = Contains(minimizeButton.bounds, mousePosition);

        SDL_SetRenderDrawColor(renderer, 38, 42, 52, 255);
        SDL_RenderClear(renderer);

        const SDL_FRect titleBarBounds{0.0f, 0.0f,
                                       static_cast<float>(width), 44.0f};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 18, 20, 26, 255);
        SDL_RenderFillRect(renderer, &titleBarBounds);

        DrawPreview(renderer, previewBounds, previewTexture,
                    previewTextureWidth, previewTextureHeight);
        DrawButton(renderer, closeButton, closeHover);
        DrawButton(renderer, minimizeButton, minimizeHover);
        DrawButton(renderer, fullscreenButton, false);
        DrawButton(renderer, openFileButton, openFileButton.hovered);
        DrawLabel(renderer, openFileButton);
        DrawButton(renderer, startButton, startButton.hovered);
        DrawLabel(renderer, startButton);
        DrawButton(renderer, stopButton, stopButton.hovered);
        DrawLabel(renderer, stopButton);

        SDL_RenderPresent(renderer);
        if (type == WALLPAPER_DYNAMIC){
            viewDecode.release();
        }

        // 监控文件发生改变
        {
            std::lock_guard<std::mutex> lock(fileSelection.fileMtx);
            if (fileSelection.requested){
                fileSelection.requested = false;
                preFile = curFile;
                curFile = fileSelection.selectedFile;
               
                // 确定文件是静态图还是视频
                type = getFileType(curFile);
                if (type < 0 && !fileIsExist(curFile)){
                    DrawMessageWindow("文件加载异常 !", window);
                }
                DestroyPreviewTexture(&previewTexture, &previewTextureWidth,
                                      &previewTextureHeight);
                if (type == WALLPAPER_DYNAMIC){
                    // 
                } else if (type == WALLPAPER_STATIC){
                    if (!task.joinable() && !tag.load()){
                        task = std::thread([&buffer, path = curFile]{
                            freeImage(buffer);
                            loadImage(buffer, path);
                            tag.store(true);
                        });
                    }
                } 
                // 清理之前的预览播放解码
                viewDecode.stopDecodeThread();
                isFirstFrame = true;
                if (type == WALLPAPER_DYNAMIC){
                    viewDecode.startDecodeThread(curFile);
                }
            }
        }

        if (tag.load()){
            // 更新图片预览
            task.join();
            tag.store(false);
            if (type == WALLPAPER_STATIC){
                UpdatePreviewTexture(renderer, &previewTexture,
                                     &previewTextureWidth, &previewTextureHeight,
                                     buffer);
            }
        }
        
        // 执行鼠标点击事件
        if (clicked){
            if (closeHover){
                closeRequested = true;
            }
            else if (minimizeHover){
                minimizeRequested = true;
            } else if (openFileButton.hovered){
                SDL_DialogFileFilter filters[] = {
                    {"Video", "mp4;mov;m4v"},
                    {"Image", "png;jpg;jpeg"},
                };
                SDL_ShowOpenFileDialog(HandleSelectedFile, &fileSelection,
                                       window, filters, 2, nullptr, false);
                
            } else if (stopButton.hovered){
                // 关闭壁纸渲染
                if (isWallPaperRunning){
                    wallpPaperbackend.stop(DEFAULT_WAIT_TIMES);
                    isWallPaperRunning = false;
                }
                
            } else if(startButton.hovered){
                // 设置当前预览为壁纸
                // 停止当前的壁纸渲染
                bool needSwtich = (preFile != curFile || !isWallPaperRunning)
                            && curFile != "";
                if (needSwtich){
                    if (isWallPaperRunning){
                        wallpPaperbackend.stop(DEFAULT_WAIT_TIMES);
                        isWallPaperRunning = false;
                    }
                    
                    // 启动新的壁纸渲染进程
                    std::vector<std::string> args = { curFile, std::to_string(type) };
                    if (wallpPaperbackend.lunch(childPath, args) == 0){
                        isWallPaperRunning = true;
                        preFile = curFile;
                    } else {
                        DrawMessageWindow("壁纸启动失败!", window);
                    }
                } else {
                    if (preFile != curFile){
                        DrawMessageWindow("当前不可设置为壁纸!", window);
                    }
                }
            }
            clicked = false;
        }

        if (minimizeRequested){
            SDL_MinimizeWindow(window);
            minimizeRequested = false;
        }
        if (closeRequested){
            running = false;
            wallpPaperbackend.stop(DEFAULT_WAIT_TIMES);
            isWallPaperRunning = false;
        }

        SDL_Delay(16);
    }

    wallpPaperbackend.stop(DEFAULT_WAIT_TIMES);
    isWallPaperRunning = false;
    viewDecode.stopDecodeThread();
    if (task.joinable()){
        task.join();
    }
    tag.store(false);
    if (previewTexture){
        SDL_DestroyTexture(previewTexture);
    }

    freeImage(buffer);

    cfg.filePath = curFile;
    cfg.isFirst = false;
    cfg.writeBack(cfgPath);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
