#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Hash/KeyHash.h"
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/MaterialTypes.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"

#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace gglab
{
	class RenderSamplerAccess;
	class RenderTextureAssetAccess;

	// Lives for one scene build. Material addresses and properties remain stable
	// during that build; the next build resolves current texture residency again.
	class RenderMaterialFrameCache final
	{
	public:
		using MaterialTable = PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>;

		struct Resolution
		{
			uint32_t m_Index = MaterialTable::InvalidSlot;
			MaterialFlags m_Flags = MaterialFlags::None;
			AlphaMode m_AlphaMode = AlphaMode::Opaque;
			bool m_KeyCollision = false;
		};

		RenderMaterialFrameCache(MaterialTable& materialTable,
			RenderTextureAssetAccess& textureAssets, const RenderSamplerAccess& samplers) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RenderMaterialFrameCache);

		[[nodiscard]] Resolution Resolve(
			RenderMaterialKey key, const MaterialProperties& material) noexcept;

	private:
		struct Record
		{
			MaterialGPU m_Gpu{};
			Resolution m_Resolution{};
			const MaterialProperties* m_Source = nullptr;
		};

		struct MaterialSourceIdentity
		{
			RenderMaterialKey m_Key{};
			const MaterialProperties* m_Source = nullptr;

			[[nodiscard]] auto AsTuple() const noexcept
			{
				return std::tie(m_Key.m_Domain, m_Key.m_Value, m_Source);
			}
			friend bool operator==(
				const MaterialSourceIdentity&, const MaterialSourceIdentity&) = default;
		};

		MaterialTable& m_MaterialTable;
		RenderTextureAssetAccess& m_TextureAssets;
		const RenderSamplerAccess& m_Samplers;
		std::unordered_map<RenderMaterialKey, Record> m_Records;
		// Only alternate objects sharing a key need an additional identity lookup.
		std::unordered_set<MaterialSourceIdentity, KeyHash<MaterialSourceIdentity>> m_ComparedSources;
	};
}
