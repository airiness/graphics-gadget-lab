#ifndef GGLAB_SHADER_GRAPH_PREVIEW_PROGRAM_HLSLI
#define GGLAB_SHADER_GRAPH_PREVIEW_PROGRAM_HLSLI

static const uint ShaderGraphPreviewViewModeCombined = 0;
static const uint ShaderGraphPreviewViewModeBaseColor = 1;
static const uint ShaderGraphPreviewViewModeEmissive = 2;
static const uint ShaderGraphPreviewViewModeMetallic = 3;
static const uint ShaderGraphPreviewViewModeRoughness = 4;
static const uint ShaderGraphPreviewViewModeOpacity = 5;

struct ShaderGraphPreviewPassParameters
{
    uint ViewIndex;
    uint ViewMode;
    float Metal;
    float Roughness;

    float3 Tint;
    uint TextureIndex;

    uint SamplerIndex;
    uint3 Padding;
};

ConstantBuffer<ShaderGraphPreviewPassParameters> g_Preview : register(b2);

#endif
