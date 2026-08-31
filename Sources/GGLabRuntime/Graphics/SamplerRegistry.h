#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHISampler.h"
#include "Graphics/SamplerTypes.h"
#include "GGLabRuntime/Core/Hash/KeyHash.h"
#include "GGLabFoundation/Base/TypeUtils.h"

#include <vector>

namespace gglab
{
	class RHIDevice;

	using SamplerKeyHash = KeyHash<SamplerKey>;

	struct SamplerRegistryReadInfo
	{
		SamplerID m_Id{};
		SamplerKey m_Key{};
		RHISamplerHandle m_Sampler{};
		uint32_t m_DescriptorIndex = 0;
		uint32_t m_PresetMask = 0;
	};

	struct SamplerRegistryStatistics
	{
		uint32_t m_UniqueSamplerCount = 0;
		uint32_t m_PresetSamplerCount = 0;
		uint32_t m_CustomSamplerCount = 0;
		uint32_t m_PresetBindingCount = 0;
		uint64_t m_CacheHitCount = 0;
		uint64_t m_CacheMissCount = 0;
	};

	class SamplerRegistry
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
		};

	private:
		struct SamplerEntry
		{
			SamplerID m_SamplerId{};
			SamplerKey m_Key{};
			RHISamplerHandle m_Sampler{};
			uint32_t m_PresetMask = 0;
		};

	public:
		explicit SamplerRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(SamplerRegistry);
		~SamplerRegistry();

		SamplerID GetOrCreateSampler(const SamplerKey& key) noexcept;
		SamplerID GetPresetSamplerId(SamplerPreset preset) const noexcept;

		uint32_t GetSamplerIndex(SamplerPreset preset) const noexcept;
		uint32_t GetSamplerIndex(const SamplerID& samplerId) const noexcept;

		uint32_t ResolveSamplerIndex(
			SamplerID samplerId, SamplerPreset fallbackPreset) const noexcept;
		[[nodiscard]] SamplerRegistryStatistics GetStatistics() const noexcept;
		[[nodiscard]] std::vector<SamplerRegistryReadInfo> GetReadInfos() const;

	private:
		void CreatePresetSamplers() noexcept;
		const SamplerEntry& GetEntry(SamplerID samplerId) const noexcept;

	private:
		RHIDevice* m_Device = nullptr;

		SamplerIDCounter m_SamplerIdCounter{};

		std::unordered_map<SamplerKey, SamplerID, SamplerKeyHash> m_SamplerMap;
		std::unordered_map<SamplerID, SamplerEntry> m_SamplerEntries;

		std::array<SamplerID, utils::EnumCount<SamplerPreset>()> m_PresetSamplers{};
		uint64_t m_CacheHitCount = 0;
		uint64_t m_CacheMissCount = 0;
	};
}
