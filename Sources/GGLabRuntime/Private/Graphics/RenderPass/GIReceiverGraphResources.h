#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

#include <array>

namespace gglab
{
	inline constexpr const char* GIReceiverResourcesName = "GI.Receiver";
	inline constexpr std::array GIReceiverTextureNames
	{
		"GI.Receiver.FaceNormal", "GI.Receiver.VertexNormal", "GI.Receiver.ShadingNormal",
		"GI.Receiver.DiffuseWeight", "GI.Receiver.BaseColor", "GI.Receiver.ReflectionRoughness",
		"GI.Receiver.ClearcoatNormalRoughness", "GI.Receiver.HitAlbedo",
	};

	// Runtime-private record shared by receiver, reference and diagnostic consumers.
	// Position is reconstructed from the paired opaque depth; view direction from the camera.
	struct RGGIReceiverResources
	{
		std::array<RGTextureId, GIReceiverTextureNames.size()> m_Textures{};
	};
}
