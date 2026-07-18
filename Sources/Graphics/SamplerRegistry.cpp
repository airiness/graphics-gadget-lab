#include "Core/Precompiled.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool IsComparisonFilter(RHISamplerFilter filter) noexcept
		{
			return
				filter == RHISamplerFilter::ComparisonMinMagLinearMipPoint ||
				filter == RHISamplerFilter::ComparisonAnisotropic;
		}

		[[nodiscard]] bool IsAnisotropicFilter(RHISamplerFilter filter) noexcept
		{
			return
				filter == RHISamplerFilter::Anisotropic ||
				filter == RHISamplerFilter::ComparisonAnisotropic;
		}

		[[nodiscard]] SamplerKey NormalizeSamplerKey(SamplerKey key) noexcept
		{
			if (!IsComparisonFilter(key.m_Filter))
			{
				key.m_CompareOp = RHICompareOp::Never;
			}

			if (!IsAnisotropicFilter(key.m_Filter))
			{
				key.m_MaxAnisotropy = 1;
			}

			return key;
		}

		[[nodiscard]] SamplerKey MakePresetSamplerKey(SamplerPreset preset) noexcept;
	}

	SamplerRegistry::SamplerRegistry(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device)
	{
		GGLAB_ASSERT_NOT_NULL(m_Device);
	}

	SamplerRegistry::~SamplerRegistry()
	{
		if (!m_Device)
		{
			return;
		}

		for (const SamplerEntry& entry : m_SamplerEntries | std::views::values)
		{
			if (entry.m_Sampler.IsValid())
			{
				m_Device->DestroySampler(entry.m_Sampler);
			}
		}
	}

	void SamplerRegistry::InitializePresetSamplers() noexcept
	{
		if (m_PresetSamplersInitialized)
		{
			return;
		}

		for (uint32_t index = 0; index < utils::EnumCount<SamplerPreset>(); ++index)
		{
			static_assert(utils::EnumCount<SamplerPreset>() <= 32);
			const auto preset = static_cast<SamplerPreset>(index);
			m_PresetSamplers[index] = GetOrCreateSampler(MakePresetSamplerKey(preset));
			GGLAB_ASSERT_MSG(m_PresetSamplers[index].IsValid(), "SamplerRegistry: failed to create preset sampler.");
			if (auto entry = m_SamplerEntries.find(m_PresetSamplers[index]);
				entry != m_SamplerEntries.end())
			{
				entry->second.m_PresetMask |= 1u << index;
			}
		}

		m_PresetSamplersInitialized = true;
	}

	SamplerID SamplerRegistry::GetOrCreateSampler(const SamplerKey& key) noexcept
	{
		const SamplerKey normalizedKey = NormalizeSamplerKey(key);
		if (auto iterator = m_SamplerMap.find(normalizedKey); iterator != m_SamplerMap.end())
		{
			++m_CacheHitCount;
			return iterator->second;
		}
		++m_CacheMissCount;

		const RHISamplerHandle sampler = m_Device->CreateSampler(normalizedKey);
		GGLAB_ASSERT_MSG(sampler.IsValid(), "SamplerRegistry: failed to create sampler.");
		if (!sampler.IsValid())
		{
			return {};
		}

		SamplerID samplerId = m_SamplerIdCounter.Acquire();
		GGLAB_ASSERT_MSG(samplerId.IsValid(), "SamplerRegistry: Invalid sampler id.");
		if (!samplerId.IsValid())
		{
			m_Device->DestroySampler(sampler);
			return {};
		}

		SamplerEntry entry{};
		entry.m_SamplerId = samplerId;
		entry.m_Key = normalizedKey;
		entry.m_Sampler = sampler;

		const auto [entryIterator, inserted] = m_SamplerEntries.emplace(samplerId, entry);
		GGLAB_ASSERT_MSG(inserted, "SamplerRegistry: failed to insert sampler entry.");
		if (!inserted)
		{
			m_Device->DestroySampler(sampler);
			return {};
		}

		const auto [mapIterator, mapInserted] = m_SamplerMap.emplace(normalizedKey, samplerId);
		GGLAB_ASSERT_MSG(mapInserted, "SamplerRegistry: failed to insert sampler into map.");
		if (!mapInserted)
		{
			m_SamplerEntries.erase(entryIterator);
			// CreateSampler may return the cached RHI handle that is already owned by
			// the existing registry entry. Do not destroy that shared handle here.
			return mapIterator->second;
		}

		return samplerId;
	}

	SamplerID SamplerRegistry::GetPresetSamplerId(SamplerPreset preset) const noexcept
	{
		GGLAB_ASSERT_MSG(m_PresetSamplersInitialized, "Preset samplers not initialized. Call InitializePresetSamplers() first.");

		const auto index = utils::ToIndexChecked(preset);
		GGLAB_ASSERT_MSG(index < m_PresetSamplers.size(), "Invalid preset sampler index.");

		const auto samplerId = m_PresetSamplers[index];
		GGLAB_ASSERT_MSG(samplerId.IsValid(), "Invalid sampler id for preset sampler.");

		return samplerId;
	}

	uint32_t SamplerRegistry::GetSamplerIndex(SamplerPreset preset) const noexcept
	{
		return GetSamplerIndex(GetPresetSamplerId(preset));
	}

	uint32_t SamplerRegistry::GetSamplerIndex(const SamplerID& samplerId) const noexcept
	{
		const auto& entry = GetEntry(samplerId);
		const RHIDescriptorHandle descriptor = m_Device->GetSamplerDescriptor(entry.m_Sampler);
		GGLAB_ASSERT_MSG(descriptor.IsValid(), "SamplerRegistry: invalid RHI sampler descriptor.");
		GGLAB_ASSERT_MSG(descriptor.m_HeapType == RHIDescriptorHeapType::Sampler, "SamplerRegistry: sampler descriptor is not in sampler heap.");
		return descriptor.m_Index;
	}

	SamplerKey SamplerRegistry::GetSamplerKey(SamplerID samplerId) const noexcept
	{
		const auto& entry = GetEntry(samplerId);
		return entry.m_Key;
	}

	uint32_t SamplerRegistry::ResolveSamplerIndex(SamplerID samplerId, SamplerPreset fallbackPreset) const noexcept
	{
		if (samplerId.IsValid())
		{
			return GetSamplerIndex(samplerId);
		}

		return GetSamplerIndex(fallbackPreset);
	}

	SamplerRegistryStatistics SamplerRegistry::GetStatistics() const noexcept
	{
		const uint32_t presetSamplerCount = static_cast<uint32_t>(std::ranges::count_if(
			m_SamplerEntries | std::views::values,
			[](const SamplerEntry& entry) noexcept { return entry.m_PresetMask != 0; }));
		return {
			.m_UniqueSamplerCount = static_cast<uint32_t>(m_SamplerEntries.size()),
			.m_PresetSamplerCount = presetSamplerCount,
			.m_CustomSamplerCount = static_cast<uint32_t>(m_SamplerEntries.size()) -
				presetSamplerCount,
			.m_PresetBindingCount = m_PresetSamplersInitialized ?
				static_cast<uint32_t>(m_PresetSamplers.size()) : 0,
			.m_CacheHitCount = m_CacheHitCount,
			.m_CacheMissCount = m_CacheMissCount,
		};
	}

	std::vector<SamplerRegistryReadInfo> SamplerRegistry::GetReadInfos() const
	{
		std::vector<SamplerRegistryReadInfo> infos;
		infos.reserve(m_SamplerEntries.size());
		for (const SamplerEntry& entry : m_SamplerEntries | std::views::values)
		{
			const RHIDescriptorHandle descriptor =
				m_Device->GetSamplerDescriptor(entry.m_Sampler);
			GGLAB_ASSERT_MSG(
				descriptor.IsValid() &&
					descriptor.m_HeapType == RHIDescriptorHeapType::Sampler,
				"SamplerRegistry diagnostics encountered an invalid sampler descriptor.");
			infos.push_back({
				.m_Id = entry.m_SamplerId,
				.m_Key = entry.m_Key,
				.m_Sampler = entry.m_Sampler,
				.m_DescriptorIndex = descriptor.m_Index,
				.m_PresetMask = entry.m_PresetMask,
			});
		}
		std::ranges::sort(
			infos,
			{},
			&SamplerRegistryReadInfo::m_Id);
		return infos;
	}

	const SamplerRegistry::SamplerEntry& SamplerRegistry::GetEntry(SamplerID samplerId) const noexcept
	{
		GGLAB_ASSERT_MSG(samplerId.IsValid(), "SamplerRegistry: invalid sampler id.");

		const auto iterator = m_SamplerEntries.find(samplerId);

		GGLAB_ASSERT_MSG(iterator != m_SamplerEntries.end(), "SamplerRegistry: sampler id not found.");

		const auto& entry = iterator->second;

		GGLAB_ASSERT_MSG(entry.m_SamplerId == samplerId, "SamplerRegistry: sampler entry id mismatch.");

		GGLAB_ASSERT_MSG(entry.m_Sampler.IsValid(), "SamplerRegistry: invalid RHI sampler handle in sampler entry.");

		return entry;
	}

	namespace
	{
	SamplerKey MakePresetSamplerKey(SamplerPreset preset) noexcept
	{
		SamplerKey key{};

		switch (preset)
		{
		case SamplerPreset::PointClamp:
		{
			key.m_Filter = RHISamplerFilter::MinMagMipPoint;
			key.m_AddressU = RHITextureAddressMode::Clamp;
			key.m_AddressV = RHITextureAddressMode::Clamp;
			key.m_AddressW = RHITextureAddressMode::Clamp;
			break;
		}
		case SamplerPreset::PointWrap:
		{
			key.m_Filter = RHISamplerFilter::MinMagMipPoint;
			key.m_AddressU = RHITextureAddressMode::Wrap;
			key.m_AddressV = RHITextureAddressMode::Wrap;
			key.m_AddressW = RHITextureAddressMode::Wrap;
			break;
		}
		case SamplerPreset::LinearClamp:
		{
			key.m_Filter = RHISamplerFilter::MinMagMipLinear;
			key.m_AddressU = RHITextureAddressMode::Clamp;
			key.m_AddressV = RHITextureAddressMode::Clamp;
			key.m_AddressW = RHITextureAddressMode::Clamp;
			break;
		}
		case SamplerPreset::LinearWrap:
		{
			key.m_Filter = RHISamplerFilter::MinMagMipLinear;
			key.m_AddressU = RHITextureAddressMode::Wrap;
			key.m_AddressV = RHITextureAddressMode::Wrap;
			key.m_AddressW = RHITextureAddressMode::Wrap;
			break;
		}
		case SamplerPreset::LinearWrapUClampV:
		{
			key.m_Filter = RHISamplerFilter::MinMagMipLinear;
			key.m_AddressU = RHITextureAddressMode::Wrap;
			key.m_AddressV = RHITextureAddressMode::Clamp;
			key.m_AddressW = RHITextureAddressMode::Clamp;
			break;
		}
		case SamplerPreset::AnisotropicClamp:
		{
			key.m_Filter = RHISamplerFilter::Anisotropic;
			key.m_AddressU = RHITextureAddressMode::Clamp;
			key.m_AddressV = RHITextureAddressMode::Clamp;
			key.m_AddressW = RHITextureAddressMode::Clamp;
			key.m_MaxAnisotropy = 16;
			break;
		}
		case SamplerPreset::AnisotropicWrap:
		{
			key.m_Filter = RHISamplerFilter::Anisotropic;
			key.m_AddressU = RHITextureAddressMode::Wrap;
			key.m_AddressV = RHITextureAddressMode::Wrap;
			key.m_AddressW = RHITextureAddressMode::Wrap;
			key.m_MaxAnisotropy = 16;
			break;
		}
		case SamplerPreset::ShadowCmpLinearClamp:
		{
			key.m_Filter = RHISamplerFilter::ComparisonMinMagLinearMipPoint;
			key.m_AddressU = RHITextureAddressMode::Clamp;
			key.m_AddressV = RHITextureAddressMode::Clamp;
			key.m_AddressW = RHITextureAddressMode::Clamp;
			key.m_CompareOp = RHICompareOp::LessEqual;
			key.m_MaxAnisotropy = 1;
			break;
		}
		default:
			GGLAB_UNREACHABLE("Unknown SamplerPreset.");
			break;
		}

		return NormalizeSamplerKey(key);
	}
	}
}
