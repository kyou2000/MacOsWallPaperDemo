#include "application.h"
// #include "spdlog/logger.h"
// #include "spdlog/sinks/basic_file_sink.h"
#include <iostream>
#include <string>

using namespace Render;
int main(int argc, char* argv[]){
    // type为0是图片，为1是视频
    if (argc < 3){
        return 1;
    }
    
    std::string curfilePath = std::string(argv[0]);
    std::string root = curfilePath.substr(0, curfilePath.find_last_of('/')) + "/";
    int type = std::stoi(argv[2]);
    Application::run1(argv[1], type, root);
    return 0;
}
