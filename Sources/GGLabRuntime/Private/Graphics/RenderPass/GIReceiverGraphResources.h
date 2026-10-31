#pragma once
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

#include <array>
#include <cstddef>

namespace gglab
{
	inline constexpr const char* GIReceiverResourcesName = "GI.Receiver";
	// Attachment order matches PassGIReceiver's SV_Target declarations.
	enum class GIReceiverSurface : size_t
	{
		FaceNormal,
		VertexNormal,
		ShadingNormal,
		DiffuseWeight,
		BaseColor,
		ReflectionRoughness,
		ClearcoatNormalRoughness,
		HitAlbedo,
		Count,
	};
	inline constexpr std::array GIReceiverTextureNames
	{
		"GI.Receiver.FaceNormal", "GI.Receiver.VertexNormal", "GI.Receiver.ShadingNormal",
		"GI.Receiver.DiffuseWeight", "GI.Receiver.BaseColor", "GI.Receiver.ReflectionRoughness",
		"GI.Receiver.ClearcoatNormalRoughness", "GI.Receiver.HitAlbedo",
	};
	static_assert(GIReceiverTextureNames.size() == static_cast<size_t>(GIReceiverSurface::Count));

	// Runtime-private record shared by receiver, reference and diagnostic consumers.
	// Depth reconstructs the canonical position. The paired raster interpolant
	// resolves quantized, grazing primary coverage without changing secondary rays.
	struct RGGIReceiverResources
	{
		std::array<RGTextureId, GIReceiverTextureNames.size()> m_Textures{};
		RGTextureId m_Position;

		[[nodiscard]] RGTextureId Get(GIReceiverSurface surface) const noexcept
		{
			return m_Textures[static_cast<size_t>(surface)];
		}
	};
}
