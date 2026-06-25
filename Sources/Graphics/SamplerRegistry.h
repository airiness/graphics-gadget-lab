#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHISampler.h"
#include "Core/Hash/FNV1a.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	class RHIDevice;

	enum class SamplerPreset : uint8_t
	{
		PointClamp,
		PointWrap,

		LinearClamp,
		LinearWrap,

		AnisotropicClamp,
		AnisotropicWrap,

		ShadowCmpLinearClamp,

		Count
	};

	using SamplerKey = RHISamplerDesc;
	using SamplerKeyHash = KeyHash<SamplerKey>;

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
		};

	public:
		explicit SamplerRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(SamplerRegistry);
		~SamplerRegistry();

		void InitializePresetSamplers() noexcept;

		SamplerID GetOrCreateSampler(const SamplerKey& key) noexcept;
		SamplerID GetPresetSamplerId(SamplerPreset preset) const noexcept;

		uint32_t GetSamplerIndex(SamplerPreset preset) const noexcept;
		uint32_t GetSamplerIndex(const SamplerID& samplerId) const noexcept;

		const SamplerKey& GetSamplerKey(const SamplerID& samplerId) const noexcept;

		uint32_t ResolveSamplerIndex(const SamplerID& samplerId, SamplerPreset fallbackPreset) const noexcept;

	private:
		const SamplerEntry& GetEntry(const SamplerID& samplerId) const noexcept;
		SamplerEntry& GetEntry(const SamplerID& samplerId) noexcept;

	private:
		RHIDevice* m_Device = nullptr;

		SamplerIDCounter m_SamplerIdCounter{};

		std::unordered_map<SamplerKey, SamplerID, SamplerKeyHash> m_SamplerMap;
		std::unordered_map<SamplerID, SamplerEntry> m_SamplerEntries;

		std::array<SamplerID, utils::EnumCount<SamplerPreset>()> m_PresetSamplers{};
		bool m_PresetSamplersInitialized = false;
	};
}
