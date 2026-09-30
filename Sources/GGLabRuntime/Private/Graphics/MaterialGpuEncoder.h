#pragma once
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/MaterialTypes.h"

namespace gglab
{
	class RenderTextureAssetAccess;
	class RenderSamplerAccess;

	struct MaterialUVTransformRows
	{
		Vector4 m_U;
		Vector4 m_V;
	};

	[[nodiscard]] MaterialUVTransformRows EncodeMaterialUVTransform(
		const MaterialTextureBinding& binding) noexcept;

	class MaterialGpuEncoder
	{
	public:
		[[nodiscard]] static MaterialGPU Encode(const MaterialProperties& material,
			const RenderTextureAssetAccess& textureAssets, const RenderSamplerAccess& samplerRegistry) noexcept;
	};
}
