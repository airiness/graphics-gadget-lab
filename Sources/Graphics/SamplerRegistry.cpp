#include "Core/Precompiled.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"

namespace gglab
{
	SamplerRegistry::SamplerRegistry(const CreateInfo& createInfo) noexcept :
		m_DescriptorManager(createInfo.m_DescriptorManager)
	{
		GGLAB_ASSERT_NOT_NULL(m_DescriptorManager);
	}

	void SamplerRegistry::InitializePresetSamplers() noexcept
	{
		if (m_PresetSamplersInitialized)
		{
			return;
		}

		for (uint32_t index = 0; index < utils::EnumCount<SamplerPreset>(); ++index)
		{
			const auto preset = static_cast<SamplerPreset>(index);
			const auto samplerDesc = MakePresetSamplerDesc(preset);

			m_PresetSamplers[index] = GetOrCreateSampler(samplerDesc);

			GGLAB_ASSERT_MSG(m_PresetSamplers[index].IsValid(), "SamplerRegistry: failed to create preset sampler.");
		}

		m_PresetSamplersInitialized = true;
	}

	SamplerID SamplerRegistry::GetOrCreateSampler(const SamplerKey& key) noexcept
	{
		if (auto iterator = m_SamplerMap.find(key); iterator != m_SamplerMap.end())
		{
			return iterator->second;
		}

		const auto samplerDesc = MakeSamplerDesc(key);

		const auto descriptorId = m_DescriptorManager->CreateBindlessSampler(samplerDesc);
		GGLAB_ASSERT_MSG(descriptorId.IsValid(), "SamplerRegistry: failed to create sampler.");

		SamplerID samplerId = m_SamplerIdCounter.Acquire();
		GGLAB_ASSERT_MSG(samplerId.IsValid(), "SamplerRegistry: Invalid sampler id.");

		SamplerEntry entry{};
		entry.m_SamplerId = samplerId;
		entry.m_Key = key;
		entry.m_DescriptorId = descriptorId;

		const auto [entryIterator, inserted] = m_SamplerEntries.emplace(samplerId, entry);
		GGLAB_ASSERT_MSG(inserted, "SamplerRegistry: failed to insert sampler entry.");

		const auto [mapIterator, mapInserted] = m_SamplerMap.emplace(key, samplerId);
		GGLAB_ASSERT_MSG(mapInserted, "SamplerRegistry: failed to insert sampler into map.");

		return samplerId;
	}

	SamplerID SamplerRegistry::GetOrCreateSampler(const D3D12_SAMPLER_DESC& samplerDesc) noexcept
	{
		return GetOrCreateSampler(MakeSamplerKey(samplerDesc));
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
		return m_DescriptorManager->BindlessSamplerIdToGlobalIndex(entry.m_DescriptorId);
	}

	D3D12_GPU_DESCRIPTOR_HANDLE SamplerRegistry::GetSamplerGpuHandle(SamplerPreset preset) const noexcept
	{
		return GetSamplerGpuHandle(GetPresetSamplerId(preset));
	}

	D3D12_GPU_DESCRIPTOR_HANDLE SamplerRegistry::GetSamplerGpuHandle(const SamplerID& samplerId) const noexcept
	{
		const auto& entry = GetEntry(samplerId);
		return m_DescriptorManager->GetBindlessSamplerGpuHandle(entry.m_DescriptorId);
	}

	const SamplerKey& SamplerRegistry::GetSamplerKey(const SamplerID& samplerId) const noexcept
	{
		const auto& entry = GetEntry(samplerId);
		return entry.m_Key;
	}

	uint32_t SamplerRegistry::ResolveSamplerIndex(const SamplerID& samplerId, SamplerPreset fallbackPreset) const noexcept
	{
		if (samplerId.IsValid())
		{
			return GetSamplerIndex(samplerId);
		}

		return GetSamplerIndex(fallbackPreset);
	}

	SamplerKey SamplerRegistry::MakeSamplerKey(const D3D12_SAMPLER_DESC& samplerDesc) noexcept
	{
		SamplerKey key{};

		key.m_Filter = samplerDesc.Filter;

		key.m_AddressU = samplerDesc.AddressU;
		key.m_AddressV = samplerDesc.AddressV;
		key.m_AddressW = samplerDesc.AddressW;

		key.m_MipLODBias = samplerDesc.MipLODBias;
		key.m_MaxAnisotropy = samplerDesc.MaxAnisotropy;

		key.m_ComparisonFunc = samplerDesc.ComparisonFunc;

		key.m_BorderColor[0] = samplerDesc.BorderColor[0];
		key.m_BorderColor[1] = samplerDesc.BorderColor[1];
		key.m_BorderColor[2] = samplerDesc.BorderColor[2];
		key.m_BorderColor[3] = samplerDesc.BorderColor[3];

		key.m_MinLOD = samplerDesc.MinLOD;
		key.m_MaxLOD = samplerDesc.MaxLOD;

		return key;
	}

	D3D12_SAMPLER_DESC SamplerRegistry::MakeSamplerDesc(const SamplerKey& key) noexcept
	{
		D3D12_SAMPLER_DESC desc{};

		desc.Filter = key.m_Filter;

		desc.AddressU = key.m_AddressU;
		desc.AddressV = key.m_AddressV;
		desc.AddressW = key.m_AddressW;

		desc.MipLODBias = key.m_MipLODBias;
		desc.MaxAnisotropy = key.m_MaxAnisotropy;

		desc.ComparisonFunc = key.m_ComparisonFunc;

		desc.BorderColor[0] = key.m_BorderColor[0];
		desc.BorderColor[1] = key.m_BorderColor[1];
		desc.BorderColor[2] = key.m_BorderColor[2];
		desc.BorderColor[3] = key.m_BorderColor[3];

		desc.MinLOD = key.m_MinLOD;
		desc.MaxLOD = key.m_MaxLOD;

		return desc;
	}

	const SamplerRegistry::SamplerEntry& SamplerRegistry::GetEntry(const SamplerID& samplerId) const noexcept
	{
		GGLAB_ASSERT_MSG(samplerId.IsValid(), "SamplerRegistry: invalid sampler id.");

		const auto iterator = m_SamplerEntries.find(samplerId);

		GGLAB_ASSERT_MSG(iterator != m_SamplerEntries.end(), "SamplerRegistry: sampler id not found.");

		const auto& entry = iterator->second;

		GGLAB_ASSERT_MSG(entry.m_SamplerId == samplerId, "SamplerRegistry: sampler entry id mismatch.");

		GGLAB_ASSERT_MSG(entry.m_DescriptorId.IsValid(), "SamplerRegistry: invalid descriptor id in sampler entry.");

		return entry;
	}

	SamplerRegistry::SamplerEntry& SamplerRegistry::GetEntry(const SamplerID& samplerId) noexcept
	{
		return const_cast<SamplerEntry&>(std::as_const(*this).GetEntry(samplerId));
	}

	D3D12_SAMPLER_DESC SamplerRegistry::MakeBaseSamplerDesc() noexcept
	{
		D3D12_SAMPLER_DESC desc{};

		desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

		desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

		desc.MipLODBias = 0.0f;
		desc.MaxAnisotropy = 1;

		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;

		desc.BorderColor[0] = 0.0f;
		desc.BorderColor[1] = 0.0f;
		desc.BorderColor[2] = 0.0f;
		desc.BorderColor[3] = 0.0f;

		desc.MinLOD = 0.0f;
		desc.MaxLOD = D3D12_FLOAT32_MAX;

		return desc;
	}

	D3D12_SAMPLER_DESC SamplerRegistry::MakePresetSamplerDesc(SamplerPreset preset) noexcept
	{
		D3D12_SAMPLER_DESC desc = MakeBaseSamplerDesc();

		switch (preset)
		{
		case SamplerPreset::PointClamp:
		{
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		}
		case SamplerPreset::PointWrap:
		{
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		}
		case SamplerPreset::LinearClamp:
		{
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			break;
		}
		case SamplerPreset::LinearWrap:
		{
			desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			break;
		}
		case SamplerPreset::AnisotropicClamp:
		{
			desc.Filter = D3D12_FILTER_ANISOTROPIC;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.MaxAnisotropy = 16;
			break;
		}
		case SamplerPreset::AnisotropicWrap:
		{
			desc.Filter = D3D12_FILTER_ANISOTROPIC;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			desc.MaxAnisotropy = 16;
			break;
		}
		case SamplerPreset::ShadowCmpLinearClamp:
		{
			desc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
			desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
			desc.MaxAnisotropy = 1;
			break;
		}
		default:
			GGLAB_UNREACHABLE("Unknown SamplerPreset.");
			break;
		}

		auto isComparisonFilter = [](D3D12_FILTER filter) noexcept
			{
				return 
					filter == D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT ||
					filter == D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR ||
					filter == D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT ||
					filter == D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR ||
					filter == D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT ||
					filter == D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR ||
					filter == D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT ||
					filter == D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR ||
					filter == D3D12_FILTER_COMPARISON_ANISOTROPIC;
			};

		if (!isComparisonFilter(desc.Filter))
		{
			desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		}

		if (desc.Filter != D3D12_FILTER_ANISOTROPIC &&
			desc.Filter != D3D12_FILTER_COMPARISON_ANISOTROPIC)
		{
			desc.MaxAnisotropy = 1;
		}

		return desc;
	}
}