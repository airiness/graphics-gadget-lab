#include "Graphics/RHI/DX12/Utility/DX12BindingLayoutUtils.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

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
			case RHIBindingType::BindlessResourceTable:
			default:
				return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			}
		}

		[[nodiscard]] bool CanUseRootDescriptor(const RHIBindingSlotDesc& slot) noexcept
		{
			return slot.m_Count == 1 && (slot.m_Type == RHIBindingType::ConstantBuffer ||
				slot.m_Type == RHIBindingType::ReadOnlyStorageBuffer ||
				slot.m_Type == RHIBindingType::ReadWriteStorageBuffer);
		}
	}

	DX12RootParameterPlan BuildDX12RootParameterPlan(
		std::span<const D3D12_ROOT_PARAMETER1> parameters) noexcept
	{
		DX12RootParameterPlan plan{};
		GGLAB_ASSERT(parameters.size() <= RHIBindingLayoutDesc::MaxSlots);
		const size_t count = std::min(parameters.size(), plan.m_BufferBindingTypes.size());
		for (size_t index = 0; index < count; ++index)
		{
			const auto& parameter = parameters[index];
			switch (parameter.ParameterType)
			{
			case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
				plan.m_InlineConstantSizesInBytes[index] = parameter.Constants.Num32BitValues *
					static_cast<uint32_t>(sizeof(uint32_t));
				break;
			case D3D12_ROOT_PARAMETER_TYPE_CBV:
				plan.m_BufferBindingTypes[index] = RHIBindingType::ConstantBuffer;
				break;
			case D3D12_ROOT_PARAMETER_TYPE_SRV:
				plan.m_BufferBindingTypes[index] = RHIBindingType::ReadOnlyStorageBuffer;
				break;
			case D3D12_ROOT_PARAMETER_TYPE_UAV:
				plan.m_BufferBindingTypes[index] = RHIBindingType::ReadWriteStorageBuffer;
				break;
			default:
				break;
			}
		}
		return plan;
	}

	void BuildDX12RootSignatureDesc(
		const RHIBindingLayoutDesc& rhiDesc, DX12BindingLayoutBuildResult& outDesc) noexcept
	{
		outDesc = {};
		if (rhiDesc.m_SlotCount > RHIBindingLayoutDesc::MaxSlots)
		{
			outDesc.m_Error = DX12BindingLayoutError::TooManySlots;
			return;
		}
		for (uint32_t index = 0; index < rhiDesc.m_SlotCount; ++index)
		{
			if (rhiDesc.m_Slots[index].m_Type == RHIBindingType::AccelerationStructure)
			{
				// Device enablement does not establish AS resource or command-binding support.
				outDesc.m_Error = DX12BindingLayoutError::AccelerationStructureBindingsUnavailable;
				return;
			}
		}

		D3D12_ROOT_SIGNATURE_FLAGS flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		for (uint32_t i = 0; i < rhiDesc.m_SlotCount; ++i)
		{
			const auto& slot = rhiDesc.m_Slots[i];
			if (slot.m_Type == RHIBindingType::Unknown)
			{
				continue;
			}

			if (IsBindlessBindingType(slot.m_Type))
			{
				flags |= slot.m_Type == RHIBindingType::BindlessResourceTable ?
					D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED :
					D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
				continue;
			}

			GGLAB_ASSERT(outDesc.m_RootParameterCount < RHIBindingLayoutDesc::MaxSlots);
			auto& rootParameter = outDesc.m_RootParameters[outDesc.m_RootParameterCount++];
			const auto visibility = ToDX12ShaderVisibility(slot.m_Visibility);

			if (slot.m_Type == RHIBindingType::InlineConstants)
			{
				GGLAB_ASSERT(slot.m_SizeInBytes % sizeof(uint32_t) == 0);
				rootParameter.InitAsConstants(slot.m_SizeInBytes / sizeof(uint32_t), slot.m_Binding,
					slot.m_Space, visibility);
				continue;
			}

			if (CanUseRootDescriptor(slot))
			{
				switch (slot.m_Type)
				{
				case RHIBindingType::ConstantBuffer:
					rootParameter.InitAsConstantBufferView(
						slot.m_Binding, slot.m_Space, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, visibility);
					break;
				case RHIBindingType::ReadOnlyStorageBuffer:
					rootParameter.InitAsShaderResourceView(
						slot.m_Binding, slot.m_Space, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, visibility);
					break;
				case RHIBindingType::ReadWriteStorageBuffer:
					rootParameter.InitAsUnorderedAccessView(
						slot.m_Binding, slot.m_Space, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, visibility);
					break;
				default:
					GGLAB_UNREACHABLE("Unexpected root descriptor binding type.");
				}
				continue;
			}

			GGLAB_ASSERT(outDesc.m_DescriptorRangeCount < RHIBindingLayoutDesc::MaxSlots);
			auto& range = outDesc.m_DescriptorRanges[outDesc.m_DescriptorRangeCount++];
			range.Init(ToDX12DescriptorRangeType(slot.m_Type),
				slot.m_Count == 0 ? UINT_MAX : slot.m_Count, slot.m_Binding, slot.m_Space,
				D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
			rootParameter.InitAsDescriptorTable(1, &range, visibility);
		}

		outDesc.m_Desc.Init_1_1(
			outDesc.m_RootParameterCount, outDesc.m_RootParameters.data(), 0, nullptr, flags);
	}
}
