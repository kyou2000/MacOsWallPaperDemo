#include "loadconfig.h"
#include "uihelper.h"
#include "../3rdparty/json.hpp"
#include <fstream>
#include <iostream>

namespace UI{
    static inline int loadJsonFile(std::string& out, const std::string& cfgPath){
        std::ifstream file(cfgPath, std::ios::binary | std::ios::ate);
        if (!file) {
            return -1;
        }

        std::streamsize size = file.tellg();
        if (size <= 0) {
            return -1;
        }
        file.seekg(0, std::ios::beg);

        out.resize(size);
        file.read(out.data(), size); 
        if (!file) {
            return -1;
        }
        return 0;
    }

    int Config::loadConfig(const std::string& cfgPath){
        std::string data;
        loadJsonFile(data, cfgPath);
        nlohmann::json json = nlohmann::json::parse(data);
        if (json.contains("filePath")){
            filePath = json["filePath"].get<std::string>();
#ifdef ENABLE_LOGGING
            UI::uiLog("filePath = " + filePath);
#endif
        } else {
            return -1;
        }
        
        if (json.contains("isFirst")){
            isFirst = json["isFirst"].get<bool>();
        } else {
            return -1;
        }
        
        return 0;
    }

    int Config::writeBack(const std::string& cfgPath){
        nlohmann::json json = {
            { "filePath", filePath },
            { "isFirst", isFirst }
        };

        std::string jsonStr = json.dump();

        std::ofstream ofs(cfgPath);
        if (!ofs){
            return -1;
        }

        ofs.write(jsonStr.c_str(), jsonStr.size());
        if (!ofs){
            return -1;
        }
        return 0;
    }
}