#pragma once
#include <Lighting/GIRayScene.hlsli>

// Four surfaces, 40 bytes/pixel: FP32 position/error and face/match, plus exact
// uint instance/primitive IDs. These are all primary fields consumed by the two
// estimators; primary Albedo/VertexNormal are never used for transport. Matching
// still evaluates/validates the complete GIHit before these fields are cached.
bool GIReceiverCacheVisits(uint2 pixel, uint2 size, uint4 indices,
	uint begin, uint count, uint ready)
{
	if (count != 0)
	{
		if (begin == 0)
		{
			// Full definition on the first submitted preparation, including background
			// and unprocessed pixels. No sampling reads a partially prepared cache.
			GetRWTexture2DFloat4(indices.x)[pixel] = 0.0.xxxx;
			GetRWTexture2DFloat4(indices.y)[pixel] = 0.0.xxxx;
			GetRWTexture2DUint(indices.z)[pixel] = 0u;
			GetRWTexture2DUint(indices.w)[pixel] = 0u;
		}
		const uint index = pixel.x + size.x * pixel.y;
		return index >= begin && index - begin < count;
	}
	return ready != 0;
}

void GIStoreReceiverCache(uint2 pixel, uint4 indices, uint match, GIHit receiver)
{
	if (!GIReceiverMatched(match))
	{
		receiver = (GIHit)(0);
	}
	GetRWTexture2DFloat4(indices.x)[pixel] = float4(receiver.Position, receiver.OriginError);
	GetRWTexture2DFloat4(indices.y)[pixel] = float4(receiver.FaceNormal, (float)(match));
	GetRWTexture2DUint(indices.z)[pixel] = receiver.Instance;
	GetRWTexture2DUint(indices.w)[pixel] = receiver.Primitive;
}

uint GILoadReceiverCache(uint2 pixel, uint4 indices, out GIHit receiver)
{
	const float4 position = GetTexture2DFloat4(indices.x).Load(int3(pixel, 0));
	const float4 face = GetTexture2DFloat4(indices.y).Load(int3(pixel, 0));
	receiver = (GIHit)(0);
	receiver.Position = position.xyz;
	receiver.OriginError = position.w;
	receiver.FaceNormal = face.xyz;
	receiver.Instance = GetTexture2DUint(indices.z).Load(int3(pixel, 0));
	receiver.Primitive = GetTexture2DUint(indices.w).Load(int3(pixel, 0));
	return (uint)(face.w);
}
