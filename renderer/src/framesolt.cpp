#include "framesolt.h"

namespace Render{
    int FrameSolt::pushSolt(void *pixel, double pts, bool startsNewTimeline){
        // FrameSolt 拥有队列状态，因此线程同步只在这里做；
        // 上层 VideoRender 不再重复加锁。
        std::lock_guard<std::mutex> lock(mutex);
        if (count >= static_cast<int>(MAXSIZE) || !pixel) return -1;

        // 固定语义：head 指向待读取位置，tail 指向下一个写入位置。
        pixels[tail] = pixel;
        times[tail] = pts;
        newTimelines[tail] = startsNewTimeline;
        tail = (tail + 1) % MAXSIZE;
        count++;
        return 0;
    }

    MetalFrame FrameSolt::popSolt(){
        // 出队与入队共用同一把锁，保证 head/tail/count 的一致性。
        std::lock_guard<std::mutex> lock(mutex);
        MetalFrame frame{};
        if (count <= 0) return frame;
        uint32_t tmp = head;
        head = (head + 1) % MAXSIZE;
        count--;
        frame.pixel = pixels[tmp];
        frame.pts = times[tmp];
        frame.startsNewTimeline = newTimelines[tmp];
        return frame;
    }
}
