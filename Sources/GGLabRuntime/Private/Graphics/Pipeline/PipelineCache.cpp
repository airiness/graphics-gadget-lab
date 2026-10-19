#include "Graphics/Pipeline/PipelineCache.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/Shader/ShaderManager.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <vector>

namespace gglab
{
	PipelineCache::PipelineCache(const CreateInfo& createInfo) noexcept :
		m_ShaderManager(createInfo.m_ShaderManager), m_PipelineSystem(createInfo.m_PipelineSystem)
	{
		GGLAB_ASSERT_NOT_NULL(m_ShaderManager);
		GGLAB_ASSERT_NOT_NULL(m_PipelineSystem);
	}

	RHIBindingLayoutHandle PipelineCache::CounterLayout(RHIBindingLayoutHandle original) noexcept
	{
		std::lock_guard lock(m_CounterLayoutMutex);
		if (m_CounterLayoutRevision != m_PipelineSystem->GetRevision())
		{
			m_CounterLayouts.clear();
			m_CounterLayoutRevision = m_PipelineSystem->GetRevision();
		}
		if (const auto found = m_CounterLayouts.find(original); found != m_CounterLayouts.end())
		{
			return found->second;
		}
		RHIBindingLayoutDesc desc;
		if (!m_PipelineSystem->GetBindingLayoutDesc(original, desc) || desc.m_SlotCount >= desc.MaxSlots)
		{
			return {};
		}
		for (uint32_t index = 0; index < desc.m_SlotCount; ++index)
		{
			const auto& slot = desc.m_Slots[index];
			// The reserved register must not overlap any element of an existing UAV range.
			if (slot.m_Space == 0 && slot.m_Binding <= nonfinite::CounterBinding &&
				(slot.m_Type == RHIBindingType::ReadWriteStorageBuffer ||
					slot.m_Type == RHIBindingType::StorageTexture) &&
				(slot.m_Count == 0 || nonfinite::CounterBinding - slot.m_Binding < slot.m_Count))
			{
				return {};
			}
		}
		desc.m_DebugName = "NonFiniteCounters.Instrumented";
		desc.m_Slots[desc.m_SlotCount++] = {
			.m_Type = RHIBindingType::ReadWriteStorageBuffer,
			.m_Visibility = RHIShaderStage::Pixel | RHIShaderStage::Compute,
			.m_Binding = nonfinite::CounterBinding,
		};
		const auto result = m_PipelineSystem->CreateBindingLayout(desc);
		if (result.IsValid())
		{
			m_CounterLayouts.emplace(original, result);
		}
		return result;
	}

	RHIPipelineHandle PipelineCache::Resolve(GraphicsPipelineSlot& slot,
		const GraphicsPhysicalPipelineKey& physicalKey,
		const RenderPassInfo& renderPassInfo) noexcept
	{
		const std::array shaderIds{
			physicalKey.m_VSId,
			physicalKey.m_PSId,
			physicalKey.m_DSId,
			physicalKey.m_HSId,
			physicalKey.m_GSId,
		};
		std::array<ShaderPipelineSnapshot, shaderIds.size()> shaderSnapshots{};
		m_ShaderManager->CapturePipelineSnapshots(shaderIds, shaderSnapshots);
		std::array<ShaderPipelineDependencyIdentity, shaderIds.size()> shaderDependencies{};
		std::ranges::transform(shaderSnapshots, shaderDependencies.begin(),
			&ShaderPipelineSnapshot::m_Dependency);
		const uint64_t pipelineSystemRevision = m_PipelineSystem->GetRevision();
		const bool canReusePhysicalPipeline =
			slot.m_Pipeline.IsValid() && m_PipelineSystem->IsAlive(slot.m_Pipeline) &&
			slot.m_PipelineSystemRevision == pipelineSystemRevision &&
			slot.m_PhysicalKey == physicalKey &&
			slot.m_ShaderDependencies &&
			*slot.m_ShaderDependencies == shaderDependencies;
		if (canReusePhysicalPipeline)
		{
			RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
			return slot.m_Pipeline;
		}

		RHIGraphicsPipelineCreateInfo createInfo{};
		createInfo.m_Desc = BuildRHIGraphicsPipelineDesc(physicalKey, shaderSnapshots[0].m_Bytecode.m_Format);
		if (std::ranges::any_of(shaderIds, [&](ShaderID shader) { return m_ShaderManager->IsNonFiniteCounterShader(shader); }))
		{
			createInfo.m_Desc.m_BindingLayout = CounterLayout(createInfo.m_Desc.m_BindingLayout);
			if (!createInfo.m_Desc.m_BindingLayout.IsValid())
			{
				return {};
			}
		}
		createInfo.m_VertexShader = shaderSnapshots[0].m_Bytecode;
		createInfo.m_PixelShader = shaderSnapshots[1].m_Bytecode;
		createInfo.m_DomainShader = shaderSnapshots[2].m_Bytecode;
		createInfo.m_HullShader = shaderSnapshots[3].m_Bytecode;
		createInfo.m_GeometryShader = shaderSnapshots[4].m_Bytecode;
		slot.m_Pipeline = m_PipelineSystem->CreateGraphicsPipeline(createInfo);
		slot.m_PhysicalKey = physicalKey;
		slot.m_ShaderDependencies =
			std::make_shared<const decltype(shaderDependencies)>(shaderDependencies);
		slot.m_PipelineSystemRevision = pipelineSystemRevision;
		RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
		return slot.m_Pipeline;
	}

	RHIPipelineHandle PipelineCache::Resolve(ComputePipelineSlot& slot,
		const ComputePipelineRecipe& recipe, const RenderPassInfo& renderPassInfo) noexcept
	{
		const std::array shaderIds{ recipe.m_CSId };
		std::array<ShaderPipelineSnapshot, 1> shaderSnapshots{};
		m_ShaderManager->CapturePipelineSnapshots(shaderIds, shaderSnapshots);
		const ShaderPipelineDependencyIdentity shaderDependency =
			shaderSnapshots[0].m_Dependency;
		const uint64_t pipelineSystemRevision = m_PipelineSystem->GetRevision();
		if (slot.m_Pipeline.IsValid() && m_PipelineSystem->IsAlive(slot.m_Pipeline) &&
			slot.m_PipelineSystemRevision == pipelineSystemRevision && slot.m_Recipe == recipe &&
			slot.m_ShaderDependency == shaderDependency)
		{
			RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
			return slot.m_Pipeline;
		}

		RHIComputePipelineCreateInfo createInfo{};
		createInfo.m_Desc.m_BindingLayout = m_ShaderManager->IsNonFiniteCounterShader(recipe.m_CSId) ? CounterLayout(recipe.m_BindingLayout) : recipe.m_BindingLayout;
		if (!createInfo.m_Desc.m_BindingLayout.IsValid())
		{
			return {};
		}
		createInfo.m_Desc.m_ComputeShader = recipe.m_CSId.IsValid() ?
			RHIShaderHandle{ recipe.m_CSId.Value(), 1u } :
			RHIShaderHandle{};
		createInfo.m_ComputeShader = shaderSnapshots[0].m_Bytecode;
		slot.m_Pipeline = m_PipelineSystem->CreateComputePipeline(createInfo);
		slot.m_Recipe = recipe;
		slot.m_ShaderDependency = shaderDependency;
		slot.m_PipelineSystemRevision = pipelineSystemRevision;
		RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
		return slot.m_Pipeline;
	}

	void PipelineCache::GetPipelineUsages(
		RHIPipelineHandle pipeline, std::vector<RenderPassInfo>& outUsages) const noexcept
	{
		outUsages.clear();
		std::shared_lock lock(m_UsageMutex);
		if (m_UsagePipelineSystemRevision != m_PipelineSystem->GetRevision())
		{
			return;
		}
		if (const auto iter = m_PipelineUsages.find(pipeline); iter != m_PipelineUsages.end())
		{
			outUsages = iter->second;
		}
	}

	void PipelineCache::RecordPipelineUsage(
		RHIPipelineHandle pipeline, const RenderPassInfo& renderPassInfo) noexcept
	{
		if (!pipeline.IsValid() || renderPassInfo.m_TypeName.empty())
		{
			return;
		}
		const uint64_t revision = m_PipelineSystem->GetRevision();
		std::unique_lock lock(m_UsageMutex);
		if (m_UsagePipelineSystemRevision != revision)
		{
			m_PipelineUsages.clear();
			m_UsagePipelineSystemRevision = revision;
		}
		auto& usages = m_PipelineUsages[pipeline];
		const auto existing =
			std::ranges::find_if(usages, [&renderPassInfo](const RenderPassInfo& usage)
				{
					return usage.m_TypeName == renderPassInfo.m_TypeName;
				});
		if (existing == usages.end())
		{
			usages.push_back(renderPassInfo);
		}
		else
		{
			*existing = renderPassInfo;
		}
	}
}
