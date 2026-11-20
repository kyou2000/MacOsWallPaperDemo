#include "utils.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Render{
    void Image::freeData(){
        if (!data) return;
        stbi_image_free(data);
        data = nullptr;
    }

    std::vector<char> loadShader(const std::string& shaderPath){
        std::ifstream file(shaderPath);
        if (!file.is_open()){
            throw std::runtime_error("can not load shader !");
        }
        
        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<char> buffer(size);
        file.read(buffer.data(), size);
        file.close();
        return buffer;
    }

    // jpg格式使用
    static inline void addAlpha(void *data, uint32_t hight, uint32_t width){
        const uint32_t dataSize = hight * width;
        uint32_t *img = reinterpret_cast<uint32_t *>(data);
        for (uint32_t i = 0; i < dataSize; i++){
            img[i] = img[i] | 0xff000000;
        }
    }

    int loadImage(Image& image, const std::string& imagePath){
        int32_t w, h, channels;
        unsigned char *data = stbi_load(imagePath.c_str(), &w, &h, nullptr, STBI_rgb_alpha);
        if (!data){
            // throw std::runtime_error("can not load image !");
            printf("can not load image !\n");
            return -1;
        }
        addAlpha(data, h, w);
        image.data = data;
        image.hight = h;
        image.width = w;
        image.channels = 4;
        return 0;
    }
}