#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace UI{
struct Extent{
    int32_t hight;
    int32_t width;
};

struct ImageData{
    int32_t width = 0;
    int32_t height = 0;
    unsigned char *data = nullptr;
};

static inline float turnToReal(float realLen, float ratio, float start){
    return start + realLen * ratio;
}

// 根据后缀区分文件格式
// 返回-1路径不支持，返回1视频，返回0图片
static inline int getFileType(const std::string_view& path){
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');

    //  处理路径中有.bashrc这个情况
    if (dot == std::string_view::npos || 
        (slash != std::string_view::npos && dot <= slash)) {
        // printf("no . state \n");
        return -1;
    }

    std::string_view ssa = path.substr(dot + 1);

    if (ssa.compare(std::string("mp4")) == 0){
        return 1;
    }
    return 0;
}

int loadImage(ImageData& image, const std::string& path);

void freeImage(ImageData& image);
}
