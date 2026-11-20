#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <vector>


namespace UI{
    struct VideoFrame{
        int height;
        int width;
        std::vector<uint8_t> data;
        double pts;
    };

    // 帧缓冲队列
    class FarmeBuffer{
        static constexpr uint32_t maxSize = 3;
        public:
            FarmeBuffer() = default;

            int push(const VideoFrame& videoFrame){
                std::lock_guard<std::mutex> lock(mtx);
                if (count >= maxSize) return -1;
                frames[tail] = videoFrame;
                tail = (tail + 1) % maxSize;
                count++;
                return 0;
            }

            VideoFrame pop(){
                std::lock_guard<std::mutex> lock(mtx);
                VideoFrame frame{};
                if (count <= 0) return frame;
                frame = frames[head];
                return frame;
            }

            bool isEmpty(){
                std::lock_guard<std::mutex> lock(mtx);
                return count <= 0;
            }

            void release(){
                std::lock_guard<std::mutex> lock(mtx);
                if (count <= 0) return;
                head = (head + 1) % maxSize;
                count--;
            }

        private:
            std::mutex mtx;
            VideoFrame frames[maxSize];
            int count = 0;
            int head = 0;
            int tail = 0;
    };

    class VideoDecode{
        public:
            VideoDecode() = default;
            ~VideoDecode() = default;

            void startDecodeThread(const std::string& path);
            VideoFrame takeFrame();
            void stopDecodeThread();
            static void unrefFrame(void *frame);
            void release();

        private:
            void decode(const std::string& path);
            void pushFrameTobBuffer(const VideoFrame& videoFrame);

        private:
            std::thread decodeThread;
            std::atomic<bool> running{false};
            std::atomic<bool> decodeFinished{true};
            FarmeBuffer buffer;
    };
}
