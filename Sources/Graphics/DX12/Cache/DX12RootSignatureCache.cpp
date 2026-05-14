#include "Core/Precompiled.h"
#include "Graphics/DX12/Cache/DX12RootSignatureCache.h"
#include "Graphics/DX12/DX12RootSignature.h"

namespace gglab
{
	DX12RootSignatureCache::DX12RootSignatureCache(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
		GGLAB_ASSERT_MSG(dx12Device != nullptr, "DX12Device must not be null");
	}

	RootSignatureHandle DX12RootSignatureCache::GetOrCreate(const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept
	{
		const RootSignatureKey key = MakeKey(desc);

		{
			std::shared_lock lock(m_Mutex);
			if (const auto iter = m_RootSignatureMap.find(key); iter != m_RootSignatureMap.end())
			{
				const RootSignatureID id = iter->second;
				if (id.IsValid() && id.Value() < m_RootSignatures.size())
				{
					return RootSignatureHandle{ id, m_RootSignatures[id.Value()]->Get() };
				}
			}
		}

		auto rootSignature = std::make_unique<DX12RootSignature>(m_DX12Device, desc);
		if (rootSignature->Get() == nullptr)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12RootSignatureCache::GetOrCreate: Failed to create root signature");
			return {};
		}

		{
			std::unique_lock lock(m_Mutex);
			if (const auto iter = m_RootSignatureMap.find(key); iter != m_RootSignatureMap.end())
			{
				const RootSignatureID id = iter->second;
				if (id.IsValid() && id.Value() < m_RootSignatures.size())
				{
					return RootSignatureHandle{ id, m_RootSignatures[id.Value()]->Get() };
				}
			}

			const RootSignatureID id{ static_cast<uint32_t>(m_RootSignatures.size()) };
			m_RootSignatures.push_back(std::move(rootSignature));
			m_RootSignatureMap[key] = id;
			return RootSignatureHandle{ id, m_RootSignatures.back()->Get() };
		}
	}

	DX12RootSignature* DX12RootSignatureCache::GetDX12RootSignature(RootSignatureID id) const noexcept
	{
		std::shared_lock lock(m_Mutex);
		if (id.IsValid() && id.Value() < m_RootSignatures.size())
		{
			return m_RootSignatures[id.Value()].get();
		}
		return nullptr;
	}

	void DX12RootSignatureCache::Clear() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_RootSignatureMap.clear();
		m_RootSignatures.clear();
	}

	RootSignatureKey DX12RootSignatureCache::MakeKey(const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept
	{
		RootSignatureKey key{};

		key.m_LowBits = FNV1a64::OffsetBasis;
		key.m_HighBits = 0x9ae16a3b2f90404full;

		const auto mix = [&](auto value) noexcept
			{
				FNV1a64::MixValue(key.m_LowBits, value);
				FNV1a64::MixValue(key.m_HighBits, value);
			};

		// hash elements of desc
		mix(desc.Version);

		const auto& desc1_1 = desc.Desc_1_1;
		mix(desc1_1.Flags);
		mix(desc1_1.NumParameters);
		for (uint32_t i = 0; i < desc1_1.NumParameters; ++i)
		{
			const auto& param = desc1_1.pParameters[i];
			mix(param.ParameterType);
			mix(param.ShaderVisibility);
			switch (param.ParameterType)
			{
			case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
			{
				const auto& constants = param.Constants;
				mix(constants.Num32BitValues);
				mix(constants.RegisterSpace);
				mix(constants.ShaderRegister);
				break;
			}
			case D3D12_ROOT_PARAMETER_TYPE_CBV:
			case D3D12_ROOT_PARAMETER_TYPE_SRV:
			case D3D12_ROOT_PARAMETER_TYPE_UAV:
			{
				const auto& descriptor = param.Descriptor;
				mix(descriptor.Flags);
				mix(descriptor.RegisterSpace);
				mix(descriptor.ShaderRegister);
				break;
			}
			case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
			{
				mix(param.DescriptorTable.NumDescriptorRanges);
				for (uint32_t r = 0; r < param.DescriptorTable.NumDescriptorRanges; ++r)
				{
					const auto& range = param.DescriptorTable.pDescriptorRanges[r];
					mix(range.RangeType);
					mix(range.NumDescriptors);
					mix(range.BaseShaderRegister);
					mix(range.RegisterSpace);
					mix(range.OffsetInDescriptorsFromTableStart);
					mix(range.Flags);
				}
				break;
			}
			default:
				// unknown type
				break;
			}
		}

		mix(desc1_1.NumStaticSamplers);
		for (uint32_t i = 0; i < desc1_1.NumStaticSamplers; ++i)
		{
			const auto& sampler = desc1_1.pStaticSamplers[i];
			mix(sampler.AddressU);
			mix(sampler.AddressV);
			mix(sampler.AddressW);
			mix(sampler.BorderColor);
			mix(sampler.ComparisonFunc);
			mix(sampler.Filter);
			mix(sampler.MaxAnisotropy);
			mix(sampler.MaxLOD);
			mix(sampler.MinLOD);
			mix(sampler.MipLODBias);
			mix(sampler.RegisterSpace);
			mix(sampler.ShaderRegister);
			mix(sampler.ShaderVisibility);
		}

		return key;
	}
}