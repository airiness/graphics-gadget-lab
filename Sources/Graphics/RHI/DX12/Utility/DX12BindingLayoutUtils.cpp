#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Utility/DX12BindingLayoutUtils.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] D3D12_SHADER_VISIBILITY ToDX12ShaderVisibility(
			RHIShaderStage visibility) noexcept
		{
			if (visibility == RHIShaderStage::Vertex)
			{
				return D3D12_SHADER_VISIBILITY_VERTEX;
			}
			if (visibility == RHIShaderStage::Pixel)
			{
				return D3D12_SHADER_VISIBILITY_PIXEL;
			}
			return D3D12_SHADER_VISIBILITY_ALL;
		}

		[[nodiscard]] D3D12_DESCRIPTOR_RANGE_TYPE ToDX12DescriptorRangeType(
			RHIBindingType type) noexcept
		{
			switch (type)
			{
			case RHIBindingType::ConstantBuffer:
				return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
			case RHIBindingType::ReadWriteStorageBuffer:
			case RHIBindingType::StorageTexture:
				return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			case RHIBindingType::Sampler:
			case RHIBindingType::BindlessSamplerTable:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
			case RHIBindingType::ReadOnlyStorageBuffer:
			case RHIBindingType::SampledTexture:
			case RHIBindingType::BindlessSampledTextureTable:
			default:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			}
		}

		[[nodiscard]] bool IsBindlessSlot(RHIBindingType type) noexcept
		{
			return type == RHIBindingType::BindlessSampledTextureTable ||
				type == RHIBindingType::BindlessSamplerTable;
		}

		[[nodiscard]] bool CanUseRootDescriptor(const RHIBindingSlotDesc& slot) noexcept
		{
			return slot.m_Count == 1 &&
				(slot.m_Type == RHIBindingType::ConstantBuffer ||
					slot.m_Type == RHIBindingType::ReadOnlyStorageBuffer ||
					slot.m_Type == RHIBindingType::ReadWriteStorageBuffer);
		}
	}

	void BuildDX12RootSignatureDesc(
		const RHIBindingLayoutDesc& rhiDesc,
		DX12BindingLayoutBuildResult& outDesc) noexcept
	{
		outDesc = {};

		D3D12_ROOT_SIGNATURE_FLAGS flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		for (uint32_t i = 0; i < rhiDesc.m_SlotCount; ++i)
		{
			const auto& slot = rhiDesc.m_Slots[i];
			if (slot.m_Type == RHIBindingType::Unknown)
			{
				continue;
			}

			if (slot.m_Type == RHIBindingType::BindlessSampledTextureTable)
			{
				flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
				continue;
			}
			if (slot.m_Type == RHIBindingType::BindlessSamplerTable)
			{
				flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
				continue;
			}

			GGLAB_ASSERT(outDesc.m_RootParameterCount < RHIBindingLayoutDesc::MaxSlots);
			auto& rootParameter = outDesc.m_RootParameters[outDesc.m_RootParameterCount++];
			const auto visibility = ToDX12ShaderVisibility(slot.m_Visibility);

			if (slot.m_Type == RHIBindingType::PushConstants)
			{
				GGLAB_ASSERT(slot.m_SizeInBytes % sizeof(uint32_t) == 0);
				rootParameter.InitAsConstants(
					slot.m_SizeInBytes / sizeof(uint32_t),
					slot.m_Binding,
					slot.m_Space,
					visibility);
				continue;
			}

			if (CanUseRootDescriptor(slot))
			{
				switch (slot.m_Type)
				{
				case RHIBindingType::ConstantBuffer:
					rootParameter.InitAsConstantBufferView(
						slot.m_Binding,
						slot.m_Space,
						D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
						visibility);
					break;
				case RHIBindingType::ReadOnlyStorageBuffer:
					rootParameter.InitAsShaderResourceView(
						slot.m_Binding,
						slot.m_Space,
						D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
						visibility);
					break;
				case RHIBindingType::ReadWriteStorageBuffer:
					rootParameter.InitAsUnorderedAccessView(
						slot.m_Binding,
						slot.m_Space,
						D3D12_ROOT_DESCRIPTOR_FLAG_NONE,
						visibility);
					break;
				default:
					GGLAB_UNREACHABLE("Unexpected root descriptor binding type.");
				}
				continue;
			}

			GGLAB_ASSERT(outDesc.m_DescriptorRangeCount < RHIBindingLayoutDesc::MaxSlots);
			auto& range = outDesc.m_DescriptorRanges[outDesc.m_DescriptorRangeCount++];
			range.Init(
				ToDX12DescriptorRangeType(slot.m_Type),
				slot.m_Count == 0 ? UINT_MAX : slot.m_Count,
				slot.m_Binding,
				slot.m_Space,
				D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
			rootParameter.InitAsDescriptorTable(1, &range, visibility);
		}

		outDesc.m_Desc.Init_1_1(
			outDesc.m_RootParameterCount,
			outDesc.m_RootParameters.data(),
			0,
			nullptr,
			flags);
	}
}
