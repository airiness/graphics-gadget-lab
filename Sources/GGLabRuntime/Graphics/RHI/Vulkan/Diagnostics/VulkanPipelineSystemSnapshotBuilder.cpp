#include "Graphics/RHI/Vulkan/Diagnostics/VulkanPipelineSystemSnapshotBuilder.h"

#include "Diagnostics/Snapshots/RHIPipelineSystemSnapshot.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineState.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/Shader/ShaderManager.h"

#include <algorithm>
#include <string>
#include <utility>

namespace gglab
{
	namespace
	{
		const char* VulkanDescriptorTypeText(VkDescriptorType type) noexcept
		{
			switch (type)
			{
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
				return "Dynamic uniform buffer";
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				return "Storage buffer";
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
				return "Sampled image";
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
				return "Storage image";
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				return "Sampler";
			default:
				return "Descriptor";
			}
		}

		RHIShaderSnapshot MakeShaderSnapshot(
			RHIShaderHandle handle, ShaderHash128 hash, const ShaderManager* shaderManager) noexcept
		{
			RHIShaderSnapshot result{};
			result.m_Handle = handle;
			result.m_Hash = hash;
			result.m_Present = handle.IsValid() || hash.m_LowBits != 0 || hash.m_HighBits != 0;
			if (handle.IsValid() && shaderManager)
			{
				result.m_DebugName = shaderManager->GetDebugName(ShaderID{ handle.Index() });
			}
			return result;
		}
	}

	void BuildVulkanPipelineSystemSnapshot(const VulkanPipelineSystem& system,
		const PipelineCache* pipelineCache, RHIPipelineSystemSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		outSnapshot.m_BackendName = "Vulkan";
		outSnapshot.m_Cache.m_PipelineSystemRevision = system.GetRevision();
		const ShaderManager* shaderManager =
			pipelineCache ? pipelineCache->GetShaderManager() : nullptr;

		outSnapshot.m_BindingLayouts.reserve(system.m_BindingLayouts.size());
		for (uint32_t layoutIndex = 0; layoutIndex < system.m_BindingLayouts.size(); ++layoutIndex)
		{
			const auto& binding = system.m_BindingLayouts[layoutIndex];
			RHIBindingLayoutSnapshot layout{};
			layout.m_Handle =
				RHIBindingLayoutHandle(layoutIndex, system.m_BindingLayoutGeneration);
			layout.m_Alive = binding.m_Layout && binding.m_Layout->IsValid();
			layout.m_DebugName = binding.m_DebugName;
			layout.m_BackendLayoutId = layoutIndex;
			layout.m_Slots.reserve(binding.m_SlotCount);

			uint32_t fixedParameter = 0;
			const VulkanBindingLayoutPlan* plan =
				binding.m_Layout ? &binding.m_Layout->GetPlan() : nullptr;
			for (uint32_t slotIndex = 0; slotIndex < binding.m_SlotCount; ++slotIndex)
			{
				const RHIBindingSlotDesc& source = binding.m_Slots[slotIndex];
				RHIBindingSlotSnapshot slot{};
				slot.m_Slot = slotIndex;
				slot.m_Type = source.m_Type;
				slot.m_Visibility = source.m_Visibility;
				slot.m_Binding = source.m_Binding;
				slot.m_Space = source.m_Space;
				slot.m_Count = source.m_Count;
				slot.m_SizeInBytes = source.m_SizeInBytes;
				slot.m_DebugName = binding.m_SlotDebugNames[slotIndex];

				if (IsBindlessBindingType(source.m_Type))
				{
					slot.m_BackendMapping = RHIBindingBackendMapping::BindlessTable;
					slot.m_BackendBindingType = source.m_Type == RHIBindingType::BindlessResourceTable
						? "Set 1 resource array"
						: "Set 1 sampler array";
					layout.m_BindlessResources |=
						source.m_Type == RHIBindingType::BindlessResourceTable;
					layout.m_BindlessSamplers |=
						source.m_Type == RHIBindingType::BindlessSamplerTable;
				}
				else if (source.m_Type != RHIBindingType::Unknown)
				{
					slot.m_BackendMapping = RHIBindingBackendMapping::FixedBinding;
					slot.m_BackendParameterIndex = static_cast<int32_t>(fixedParameter);
					if (plan)
					{
						const auto native = std::ranges::find_if(
							plan->m_Set0Bindings.begin(),
							plan->m_Set0Bindings.begin() + plan->m_Set0BindingCount,
							[fixedParameter](const VulkanSet0BindingPlan& candidate)
							{ return candidate.m_LogicalParameterIndex == fixedParameter; });
						if (native != plan->m_Set0Bindings.begin() + plan->m_Set0BindingCount)
						{
							slot.m_BackendBindingType = std::string("Set 0 binding ") +
								std::to_string(native->m_Binding) + " " +
								VulkanDescriptorTypeText(native->m_DescriptorType);
						}
					}
					++fixedParameter;
				}
				layout.m_Slots.push_back(std::move(slot));
			}
			layout.m_BackendParameterCount = fixedParameter;
			outSnapshot.m_BindingLayouts.push_back(std::move(layout));
		}

		outSnapshot.m_Pipelines.reserve(system.m_Pipelines.size());
		for (uint32_t index = 0; index < system.m_Pipelines.size(); ++index)
		{
			const auto& binding = system.m_Pipelines[index];
			RHIPipelineSnapshot pipeline{};
			pipeline.m_Handle = RHIPipelineHandle(index, system.m_PipelineGeneration);
			pipeline.m_Alive = binding.m_Pipeline && binding.m_Pipeline->Get() != VK_NULL_HANDLE;
			pipeline.m_BackendPipelineId = index;
			if (pipelineCache)
			{
				pipelineCache->GetPipelineUsages(pipeline.m_Handle, pipeline.m_RenderPasses);
			}

			if (binding.m_Type == VulkanPipelineSystem::PipelineType::Graphics)
			{
				++outSnapshot.m_Cache.m_RegisteredGraphicsPipelines;
				pipeline.m_Type = RHIPipelineSnapshotType::Graphics;
				const auto& desc = binding.m_GraphicsDesc;
				pipeline.m_BindingLayout = desc.m_BindingLayout;
				pipeline.m_BackendLayoutId = desc.m_BindingLayout.Index();
				pipeline.m_VertexShader =
					MakeShaderSnapshot(desc.m_VertexShader, binding.m_ShaderHashes[0], shaderManager);
				pipeline.m_PixelShader =
					MakeShaderSnapshot(desc.m_PixelShader, binding.m_ShaderHashes[1], shaderManager);
				pipeline.m_DomainShader =
					MakeShaderSnapshot(desc.m_DomainShader, binding.m_ShaderHashes[2], shaderManager);
				pipeline.m_HullShader =
					MakeShaderSnapshot(desc.m_HullShader, binding.m_ShaderHashes[3], shaderManager);
				pipeline.m_GeometryShader =
					MakeShaderSnapshot(desc.m_GeometryShader, binding.m_ShaderHashes[4], shaderManager);
				const uint32_t attributeCount = std::min(
					desc.m_VertexInput.m_AttributeCount, RHIVertexInputLayoutDesc::MaxAttributes);
				pipeline.m_VertexAttributes.reserve(attributeCount);
				for (uint32_t attributeIndex = 0; attributeIndex < attributeCount; ++attributeIndex)
				{
					const auto& attribute = desc.m_VertexInput.m_Attributes[attributeIndex];
					pipeline.m_VertexAttributes.push_back({
						.m_SemanticName = binding.m_SemanticNames[attributeIndex],
						.m_SemanticIndex = attribute.m_SemanticIndex,
						.m_Format = attribute.m_Format,
						.m_InputSlot = attribute.m_InputSlot,
						.m_AlignedByteOffset = attribute.m_AlignedByteOffset,
						});
				}
				const uint32_t vertexBufferCount = std::min(desc.m_VertexInput.m_VertexBufferCount,
					RHIVertexInputLayoutDesc::MaxVertexBuffers);
				pipeline.m_VertexBuffers.assign(desc.m_VertexInput.m_VertexBuffers.begin(),
					desc.m_VertexInput.m_VertexBuffers.begin() + vertexBufferCount);
				pipeline.m_TopologyType = desc.m_TopologyType;
				pipeline.m_PrimitiveTopology = desc.m_PrimitiveTopology;
				pipeline.m_Rasterizer = desc.m_Rasterizer;
				pipeline.m_DepthStencil = desc.m_DepthStencil;
				pipeline.m_Blend = desc.m_Blend;
				pipeline.m_RenderTargetFormats.assign(desc.m_RenderTargetFormats.begin(),
					desc.m_RenderTargetFormats.begin() + std::min(
						desc.m_RenderTargetCount, RHIGraphicsPipelineDesc::MaxRenderTargets));
				pipeline.m_DepthStencilFormat = desc.m_DepthStencilFormat;
				pipeline.m_SampleCount = desc.m_SampleCount;
				pipeline.m_SampleQuality = desc.m_SampleQuality;
			}
			else
			{
				++outSnapshot.m_Cache.m_RegisteredComputePipelines;
				pipeline.m_Type = RHIPipelineSnapshotType::Compute;
				pipeline.m_BindingLayout = binding.m_ComputeDesc.m_BindingLayout;
				pipeline.m_BackendLayoutId = binding.m_ComputeDesc.m_BindingLayout.Index();
				pipeline.m_ComputeShader = MakeShaderSnapshot(binding.m_ComputeDesc.m_ComputeShader,
					binding.m_ShaderHashes[0], shaderManager);
			}
			outSnapshot.m_Pipelines.push_back(std::move(pipeline));
		}

		outSnapshot.m_Cache.m_BackendGraphicsPipelines =
			outSnapshot.m_Cache.m_RegisteredGraphicsPipelines;
		outSnapshot.m_Cache.m_BackendComputePipelines =
			outSnapshot.m_Cache.m_RegisteredComputePipelines;
		outSnapshot.m_Cache.m_BackendBindingLayouts =
			static_cast<uint32_t>(outSnapshot.m_BindingLayouts.size());
	}
}
