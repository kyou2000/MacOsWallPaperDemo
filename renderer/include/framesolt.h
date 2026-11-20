#pragma once
#include <cstdint>
#include <mutex>

namespace Render{
    struct MetalFrame{
        void *pixel;
        double pts;
        bool startsNewTimeline = false;
    };

    class FrameSolt{
        static constexpr uint32_t MAXSIZE = 3;
        public:
            FrameSolt() = default;

            int pushSolt(void *pixel, double pts, bool startsNewTimeline);
            MetalFrame popSolt();

        private:
            // head/tail 分别指向待读位置和下一个写入位置；
            // 空满状态由 count 统一判断。
            uint32_t head = 0;
            uint32_t tail = 0;
            int count = 0;
            void *pixels[MAXSIZE];
            double times[MAXSIZE];
            bool newTimelines[MAXSIZE];
            std::mutex mutex;
    };
}
