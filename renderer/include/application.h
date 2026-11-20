#pragma once
#include <string>

namespace Render{
    class Application{
        public:
            static void run(); // 视频播放测试
            static void run1(const std::string& file, int type, const std::string& execPath);
    };
}