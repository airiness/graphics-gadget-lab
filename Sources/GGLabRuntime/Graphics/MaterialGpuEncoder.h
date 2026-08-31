#pragma once
#include "Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"

namespace gglab
{
	class AssetManager;
	class SamplerRegistry;

	class MaterialGpuEncoder
	{
	public:
		[[nodiscard]] static MaterialGPU Encode(const MaterialProperties& material,
			const AssetManager& assetManager, const SamplerRegistry& samplerRegistry) noexcept;
	};
}
