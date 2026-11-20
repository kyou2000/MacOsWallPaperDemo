#pragma once
#include <string>

namespace UI{
    class Config{
        public:
            Config() = default;
            ~Config() = default;

            int loadConfig(const std::string& cfgPath);
            int writeBack(const std::string& cfgPath);

        public:
            bool isFirst = true;
            std::string filePath;
    };
}
