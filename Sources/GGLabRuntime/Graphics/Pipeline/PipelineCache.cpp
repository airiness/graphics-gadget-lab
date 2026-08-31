#include "Graphics/Pipeline/PipelineCache.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/Shader/ShaderManager.h"

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
		createInfo.m_Desc = BuildRHIGraphicsPipelineDesc(physicalKey);
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
		createInfo.m_Desc.m_BindingLayout = recipe.m_BindingLayout;
		createInfo.m_Desc.m_ComputeShader = recipe.m_CSId.IsValid()
			? RHIShaderHandle{ recipe.m_CSId.Value(), 1u }
		: RHIShaderHandle{};
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
				{ return usage.m_TypeName == renderPassInfo.m_TypeName; });
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
