#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];   // 裁剪空间位置
    float2 uv;          // 插值后的 uv
};

vertex VertexOut vertexMain(uint vertexId [[vertex_id]]){
    // 这里只需要返回固定坐标即可
    // 全屏三角形
    VertexOut out;
    float2 vertexBuffer[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    float2 pos = vertexBuffer[vertexId];
    out.position = float4(pos, 0.0, 1.0);
    // out.uv = vertexBuffer[vertexId] * 0.5 + 0.5;
    // 视频帧要翻过来
    out.uv = float2(pos.x * 0.5 + 0.5, 1.0 - (pos.y * 0.5 + 0.5));
    
    return out;
}

fragment float4 fragmentMain(
    VertexOut in [[stage_in]],
    texture2d<float> bg [[ texture(0) ]],
    sampler sam [[ sampler(0) ]]
) {
    // 使用 Metal 的 texture2d::sample API 采样视频纹理。
    return bg.sample(sam, in.uv);
}
