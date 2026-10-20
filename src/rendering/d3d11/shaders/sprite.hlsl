// 스프라이트 쿼드: 정점 버퍼 없이 SV_VertexID로 triangle strip 4정점 생성
cbuffer SpriteCB : register(b0)
{
    float4 dstRect;  // NDC x0, y0, x1, y1
    float4 srcRect;  // UV  u0, v0, u1, v1
    float4 tint;     // premultiplied 곱셈 색
};

Texture2D spriteTex : register(t0);
SamplerState pointSampler : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 t = float2(id & 1, id >> 1);
    VSOut o;
    o.pos = float4(lerp(dstRect.x, dstRect.z, t.x), lerp(dstRect.y, dstRect.w, t.y), 0.0, 1.0);
    o.uv = lerp(srcRect.xy, srcRect.zw, t);
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    // 텍스처는 premultiplied BGRA. tint도 premultiplied 기준으로 곱한다.
    return spriteTex.Sample(pointSampler, i.uv) * tint;
}
