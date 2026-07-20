struct PSInput
{
    float4 Position : SV_Position;
    float3 Normal : TEXCOORD0;
    float4 Color : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target0
{
    const float3 lightDirection = normalize(float3(0.35f, 0.80f, 0.45f));
    const float diffuse = max(dot(normalize(input.Normal), lightDirection), 0.0f);
    const float lighting = 0.30f + 0.70f * diffuse;
    const float3 litColor = max(saturate(input.Color.rgb * lighting), 0.025f);
    return float4(litColor, saturate(input.Color.a));
}
