#include <Common/Common.hlsli>
#include <Common/FullscreenTriangle.hlsli>
#include <Common/Cubemap.hlsli>
#include <Common/MaterialSampling.hlsli>
#include <Common/ApplicationBinding.hlsli>

cbuffer IBLCubemapPreviewPassConstants : register(b2)
{
	uint g_DisplayLayout;
	uint g_CubemapTextureIndex;
	uint g_CubemapSamplerIndex;
	uint g_SampleMip;
};

TextureSamplerBindingData GetCubemapBinding()
{
	return MakeTextureSamplerBinding(uint2(g_CubemapTextureIndex, g_CubemapSamplerIndex));
}

FullscreenTriangleVSOutput VSMain(uint vid : SV_VertexID)
{
	return FullscreenTriangleVS(vid);
}

static const uint IBL_PREVIEW_LAYOUT_GRID_2X3 = 0;
static const uint IBL_PREVIEW_LAYOUT_CROSS = 1;

bool TryGetGrid2x3Face(float2 uv, out uint face, out float2 faceUv)
{
	float2 atlasCoord = saturate(uv) * float2(4.0, 3.0);
	uint2 tile = min((uint2) atlasCoord, uint2(3u, 2u));

	if (tile.x >= 3u || tile.y >= 2u)
	{
		face = 0u;
		faceUv = 0.0.xx;
		return false;
	}

	face = tile.y * 3u + tile.x;
	faceUv = frac(atlasCoord);
	return true;
}

bool TryGetCrossFace(float2 uv, out uint face, out float2 faceUv)
{
	float2 atlasCoord = saturate(uv) * float2(4.0, 3.0);
	uint2 tile = min((uint2) atlasCoord, uint2(3u, 2u));

	face = 0u;
	faceUv = frac(atlasCoord);

	if (tile.y == 0u && tile.x == 1u)
	{
		face = CUBEMAP_FACE_POSITIVE_Y;
		return true;
	}
	if (tile.y == 1u)
	{
		switch (tile.x)
		{
			case 0u:
				face = CUBEMAP_FACE_NEGATIVE_X;
				return true;
			case 1u:
				face = CUBEMAP_FACE_POSITIVE_Z;
				return true;
			case 2u:
				face = CUBEMAP_FACE_POSITIVE_X;
				return true;
			case 3u:
				face = CUBEMAP_FACE_NEGATIVE_Z;
				return true;
		}
	}
	if (tile.y == 2u && tile.x == 1u)
	{
		face = CUBEMAP_FACE_NEGATIVE_Y;
		return true;
	}

	return false;
}

float4 PSMain(FullscreenTriangleVSOutput IN) : SV_Target0
{
	TextureSamplerBindingData binding = GetCubemapBinding();

	uint face = 0u;
	float2 faceUv = 0.0.xx;
	bool isValidFace = false;

	if (g_DisplayLayout == IBL_PREVIEW_LAYOUT_CROSS)
	{
		isValidFace = TryGetCrossFace(IN.UV, face, faceUv);
	}
	else
	{
		isValidFace = TryGetGrid2x3Face(IN.UV, face, faceUv);
	}

	if (!isValidFace)
	{
		return float4(0.0, 0.0, 0.0, 1.0);
	}

	float3 dir = CubemapFaceUvToDirection(face, faceUv);
	float3 hdr = SampleTextureCubeLevel(binding, dir, float(g_SampleMip)).rgb * g_Scene.IBLResource.EnvironmentIntensity;
	float3 color = LinearToSRGB(ACESFitted(hdr));
	return float4(color, 1.0);
}
