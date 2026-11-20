#include "uiutils.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace UI{
    // jpg格式使用
    static inline void addAlpha(void *data, uint32_t hight, uint32_t width){
        const uint32_t dataSize = hight * width;
        uint32_t *img = reinterpret_cast<uint32_t *>(data);
        for (uint32_t i = 0; i < dataSize; i++){
            img[i] = img[i] | 0xff000000;
        }
    }

    int loadImage(ImageData& image, const std::string& path){
        int32_t w, h, channels;
        unsigned char *data_ = stbi_load(path.c_str(), &w, &h, nullptr, STBI_rgb_alpha);
        if (!data_){
            return -1;
        }
        
        addAlpha(data_, h, w);
        image.data = data_;
        image.width = w;
        image.height = h;
        return 0;
    }

    void freeImage(ImageData& image){
        if (!image.data) return;
        stbi_image_free(image.data);
        image.data = nullptr;
        image.width = 0;
        image.height = 0;
    }
}
