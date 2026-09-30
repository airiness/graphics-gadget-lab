#include "Graphics/RenderMaterialFrameCache.h"
#include "Graphics/MaterialGpuEncoder.h"
#include "GGLabRuntime/Graphics/RenderTextureAssetAccess.h"

#include <cstring>

namespace gglab
{
	RenderMaterialFrameCache::RenderMaterialFrameCache(MaterialTable& materialTable,
		RenderTextureAssetAccess& textureAssets, const RenderSamplerAccess& samplers) noexcept :
		m_MaterialTable(materialTable), m_TextureAssets(textureAssets), m_Samplers(samplers)
	{
	}

	RenderMaterialFrameCache::Resolution RenderMaterialFrameCache::Resolve(
		RenderMaterialKey key, const MaterialProperties& material) noexcept
	{
		const auto iter = m_Records.find(key);
		if (iter != m_Records.end())
		{
			const Record& record = iter->second;
			if (record.m_Source == &material ||
				!m_ComparedSources.emplace(MaterialSourceIdentity{ key, &material }).second)
			{
				return record.m_Resolution;
			}
		}

		m_TextureAssets.MarkTextureUsed(material.m_BaseColorBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_EmissiveBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_MetallicRoughnessBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_NormalBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_OcclusionBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_ClearcoatBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_ClearcoatRoughnessBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_ClearcoatNormalBinding.m_TextureId);
		m_TextureAssets.MarkTextureUsed(material.m_AnisotropyBinding.m_TextureId);
		const MaterialGPU gpu = MaterialGpuEncoder::Encode(material, m_TextureAssets, m_Samplers);

		if (iter == m_Records.end())
		{
			const Resolution resolution{
				.m_Index = m_MaterialTable.Upsert(key, gpu),
				.m_Flags = material.m_Flags,
				.m_AlphaMode = material.m_AlphaMode,
			};
			m_Records.emplace(key, Record{ gpu, resolution, &material });
			return resolution;
		}

		// Preserve the first material for this key, but check every distinct source
		// once so key reuse cannot silently alias different material contents.
		const Record& record = iter->second;
		Resolution resolution = record.m_Resolution;
		resolution.m_KeyCollision = std::memcmp(&record.m_Gpu, &gpu, sizeof(MaterialGPU)) != 0 ||
			record.m_Resolution.m_Flags != material.m_Flags ||
			record.m_Resolution.m_AlphaMode != material.m_AlphaMode;
		return resolution;
	}
}
