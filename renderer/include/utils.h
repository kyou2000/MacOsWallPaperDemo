#pragma once

#include <vector>
#include <fstream>
#include <string>
#include <cstdint>
#include <stdexcept>

namespace Render{
    struct Image{
        public:
            Image() = default;
            ~Image(){
                freeData();
            }
            
            unsigned char *data;
            int32_t width;
            int32_t hight;
            int32_t channels;

            size_t getDataSize(){
                return width * hight * channels;
            }

        private:
            void freeData();
    };

    std::vector<char> loadShader(const std::string& shaderPath);

    int loadImage(Image& image, const std::string& imagePath);

    static inline void printImageInfo(const Image& image){
        printf("width = %d, hight = %d, channels = %d\n", image.width, image.hight, image.channels);
        
    }
}