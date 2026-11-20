#pragma once
#ifdef ENABLE_LOGGING
#include "spdlog/logger.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <string>
#endif

namespace UI{
#ifdef ENABLE_LOGGING
    // 后续需要使用的时候都从这个头文件中的函数进行调用
    // auto loger = spdlog::basic_logger_mt("file_logger", "uiapp.log", true);
    inline std::shared_ptr<spdlog::logger> loger;

    static inline void initLoger(){
        loger = spdlog::basic_logger_mt("file_logger", "/Users/banyuchen/metal_test/uiapp.log", true);
    }

    static inline void uiLog(const std::string& msg){
        loger->info(msg);
    }

    static inline void uiError(const std::string& msg){
        loger->error(msg);
    }
#endif
}