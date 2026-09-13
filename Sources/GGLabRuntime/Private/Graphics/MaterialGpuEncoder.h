#pragma once
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"

namespace gglab
{
	class AssetManager;
	class RenderSamplerAccess;

	class MaterialGpuEncoder
	{
	public:
		[[nodiscard]] static MaterialGPU Encode(const MaterialProperties& material,
			const AssetManager& assetManager, const RenderSamplerAccess& samplerRegistry) noexcept;
	};
}
