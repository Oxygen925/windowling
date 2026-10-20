// E8 3D 테스트: 정점 컬러 큐브 (색은 premultiplied alpha)
cbuffer CubeCB : register(b0)
{
    float4x4 mvp;
};

struct VSIn
{
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct VSOut
{
    float4 pos : SV_Position;
    float4 color : COLOR;
};

VSOut VSMain(VSIn v)
{
    VSOut o;
    o.pos = mul(float4(v.pos, 1.0), mvp);
    o.color = v.color;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return i.color;
}
