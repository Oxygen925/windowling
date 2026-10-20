// 3D 모델 (P5): 정점 스키닝 + base color 텍스처 + half-Lambert 1광원. 출력은 premultiplied alpha.
// 행렬은 열 우선(glTF 와 같은 배치)으로 올리고 mul(M, v) 로 쓴다 (HLSL cbuffer 기본 column_major).
cbuffer Frame : register(b0) {
    float4x4 viewProj;
    float4 lightDir;   // xyz: 빛이 향하는 방향 (정규화)
    float4 baseColor;
    float4 params;     // x: alphaCutoff, y: alphaMode(0 opaque, 1 mask, 2 blend), z: hasTexture
};
cbuffer Bones : register(b1) {
    float4x4 bones[128];
};

Texture2D baseTex : register(t0);
SamplerState samp : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};
struct VSOut {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(VSIn i) {
    float4x4 skin = bones[i.joints.x] * i.weights.x + bones[i.joints.y] * i.weights.y +
                    bones[i.joints.z] * i.weights.z + bones[i.joints.w] * i.weights.w;
    float4 world = mul(skin, float4(i.pos, 1.0));
    VSOut o;
    o.pos = mul(viewProj, world);
    o.normal = mul((float3x3)skin, i.normal);
    o.uv = i.uv;
    return o;
}

float4 PSMain(VSOut i, bool front : SV_IsFrontFace) : SV_Target {
    float4 c = baseColor;
    if (params.z > 0.5) c *= baseTex.Sample(samp, i.uv);
    if (params.y > 0.5 && params.y < 1.5 && c.a < params.x) discard;
    if (params.y < 0.5) c.a = 1.0;
    float3 n = normalize(i.normal);
    if (!front) n = -n;  // 양면 재질의 뒷면
    float ndl = dot(n, -lightDir.xyz);
    float h = ndl * 0.5 + 0.5;                 // half-Lambert
    float3 lit = c.rgb * (0.45 + 0.55 * h * h);
    return float4(lit * c.a, c.a);             // premultiplied
}
