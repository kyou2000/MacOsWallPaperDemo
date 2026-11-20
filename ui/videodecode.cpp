#include "videodecode.h"
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace UI{
    // ffmpeg初始化， cpu解码不需要初始化设备
    // 预览只播放一次即可，直接整体作为线程中整体运行的函数即可
    void VideoDecode::decode(const std::string& path){
        AVFormatContext *fmtCtx = nullptr;
        if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0) return;
        if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
            avformat_close_input(&fmtCtx);
            return;
        }

        int videoIndex = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoIndex < 0) {
            avformat_close_input(&fmtCtx);
            return;
        }
        // 获取到视频流
        AVStream* videoStream = fmtCtx->streams[videoIndex];

        AVCodecParameters *codecpar = fmtCtx->streams[videoIndex]->codecpar;
        const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            avformat_close_input(&fmtCtx);
            return;
        }

        AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codecCtx, codecpar);
        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&codecCtx);
            avformat_close_input(&fmtCtx);
            return;
        } 

        SwsContext *swsCtx = sws_getContext(
            codecCtx->width, codecCtx->height, codecCtx->pix_fmt,
            codecCtx->width, codecCtx->height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        );

        AVFrame *frame = av_frame_alloc();
        AVFrame *rgbaFrame = av_frame_alloc();
        AVPacket *pkt = av_packet_alloc();

        rgbaFrame->format = AV_PIX_FMT_RGBA;
        rgbaFrame->width  = codecCtx->width;
        rgbaFrame->height = codecCtx->height;
        int ret = av_frame_get_buffer(rgbaFrame, 1);
        if (ret < 0){
            printf("[LOG] get buffer error !\n");
            return;
        }

        int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, codecCtx->width, codecCtx->height, 1);
        // 创建临时buffer来存储转换后的图像
        std::vector<uint8_t> buf(bufSize);
        av_image_fill_arrays(rgbaFrame->data, rgbaFrame->linesize, buf.data(),
                            AV_PIX_FMT_RGBA, codecCtx->width, codecCtx->height, 1);

        // 循环解码
        // 获取帧基准转换，之后每帧的时间由这个基准进行计算
        double videoBaseTime = av_q2d(videoStream->time_base);
        while (running.load() && av_read_frame(fmtCtx, pkt) >= 0){
            if (pkt->stream_index == videoIndex) {
                if (avcodec_send_packet(codecCtx, pkt) == 0){
                    while (avcodec_receive_frame(codecCtx, frame) == 0){
                        // 图像转换
                        sws_scale(swsCtx,
                                (const uint8_t *const *)frame->data, frame->linesize,
                                0, codecCtx->height,
                                rgbaFrame->data, rgbaFrame->linesize);

                        VideoFrame vf;
                        vf.width = codecCtx->width;
                        vf.height = codecCtx->height;
                        vf.data.assign(buf.begin(), buf.end());
                        vf.pts = videoBaseTime * frame->pts; // 这里必须进行计时方式的转换
                        pushFrameTobBuffer(vf);
                        // rgbaframe入队列，原本的frame直接unref
                        av_frame_unref(frame);
                    } 
                }
                av_packet_unref(pkt);
            }
        }

        // flush decoder
        avcodec_send_packet(codecCtx, nullptr);
        while (avcodec_receive_frame(codecCtx, frame) == 0){
            av_frame_unref(frame);
        }

        av_packet_free(&pkt);
        av_frame_free(&frame);
        av_frame_free(&rgbaFrame);
        sws_freeContext(swsCtx);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
    }

    void VideoDecode::pushFrameTobBuffer(const VideoFrame& videoFrame){
        // test
        // AVFrame *av_frame = static_cast<AVFrame *>(videoFrame.frame);
        // if (buffer.push(videoFrame) < 0){
        //     av_frame_unref(av_frame);
        // }
        while (buffer.push(videoFrame) < 0 && running.load()){
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    VideoFrame VideoDecode::takeFrame(){
        if (!running.load()) return {};
        if (decodeFinished.load()) return {};
        return buffer.pop();
    }

    void VideoDecode::startDecodeThread(const std::string& path){
        if (decodeThread.joinable()) return;
        running.store(true);
        decodeThread = std::thread([this, path]{
            decodeFinished.store(false);
            this->decode(path);
            decodeFinished.store(true);
        });
    }

    void VideoDecode::stopDecodeThread(){
        running.store(false);
        if (decodeThread.joinable()){
            decodeThread.join();
        }
        decodeFinished.store(true);
    }

    void VideoDecode::unrefFrame(void *frame){
        AVFrame *av_frame = static_cast<AVFrame *>(frame);
        av_frame_unref(av_frame);
    }

    void VideoDecode::release(){
        buffer.release();
    }
}
