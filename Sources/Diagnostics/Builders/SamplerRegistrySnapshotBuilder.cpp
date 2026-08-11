#include "Diagnostics/Builders/SamplerRegistrySnapshotBuilder.h"
#include "Diagnostics/Snapshots/SamplerRegistrySnapshot.h"
#include "Graphics/SamplerRegistry.h"

namespace gglab
{
	void BuildSamplerRegistrySnapshot(
		const SamplerRegistry& registry, SamplerRegistrySnapshot& snapshot) noexcept
	{
		snapshot = {};
		const SamplerRegistryStatistics statistics = registry.GetStatistics();
		snapshot.m_UniqueSamplerCount = statistics.m_UniqueSamplerCount;
		snapshot.m_PresetSamplerCount = statistics.m_PresetSamplerCount;
		snapshot.m_CustomSamplerCount = statistics.m_CustomSamplerCount;
		snapshot.m_PresetBindingCount = statistics.m_PresetBindingCount;
		snapshot.m_CacheHitCount = statistics.m_CacheHitCount;
		snapshot.m_CacheMissCount = statistics.m_CacheMissCount;

		const std::vector<SamplerRegistryReadInfo> infos = registry.GetReadInfos();
		snapshot.m_Entries.reserve(infos.size());
		for (const SamplerRegistryReadInfo& info : infos)
		{
			snapshot.m_Entries.push_back({
				.m_Id = info.m_Id,
				.m_Key = info.m_Key,
				.m_Sampler = info.m_Sampler,
				.m_DescriptorIndex = info.m_DescriptorIndex,
				.m_PresetMask = info.m_PresetMask,
				});
		}
	}
}
