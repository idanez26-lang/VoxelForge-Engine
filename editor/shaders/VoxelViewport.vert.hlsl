struct VSInput
{
    float3 Position : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float4 Color : TEXCOORD2;
};

struct VSOutput
{
    float4 Position : SV_Position;
    float3 Normal : TEXCOORD0;
    float4 Color : TEXCOORD1;
};

cbuffer Camera : register(b0, space1)
{
    float4 ViewProjectionRow0;
    float4 ViewProjectionRow1;
    float4 ViewProjectionRow2;
    float4 ViewProjectionRow3;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    const float4 position = float4(input.Position, 1.0f);
    output.Position = float4(
        dot(ViewProjectionRow0, position),
        dot(ViewProjectionRow1, position),
        dot(ViewProjectionRow2, position),
        dot(ViewProjectionRow3, position));
    output.Normal = input.Normal;
    output.Color = input.Color;
    return output;
}
