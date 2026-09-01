#pragma once
#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "Graphics/SamplerTypes.h"

#include <vector>

namespace gglab
{
	struct SamplerRegistrySnapshot
	{
		struct Entry
		{
			SamplerID m_Id{};
			SamplerKey m_Key{};
			RHISamplerHandle m_Sampler{};
			uint32_t m_DescriptorIndex = 0;
			uint32_t m_PresetMask = 0;
		};

		uint32_t m_UniqueSamplerCount = 0;
		uint32_t m_PresetSamplerCount = 0;
		uint32_t m_CustomSamplerCount = 0;
		uint32_t m_PresetBindingCount = 0;
		uint64_t m_CacheHitCount = 0;
		uint64_t m_CacheMissCount = 0;
		std::vector<Entry> m_Entries;
	};

	template <> struct SnapshotTraits<SamplerRegistrySnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.SamplerRegistrySnapshot");
	};
}
