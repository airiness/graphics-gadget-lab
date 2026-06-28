#include "Core/Precompiled.h"
#include "Graphics/Pipeline/PipelineCache.h"
#include "Graphics/Pipeline/RHIPipelineRecipeAdapter.h"
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/Shader/ShaderManager.h"

namespace gglab
{
	PipelineCache::PipelineCache(const CreateInfo& createInfo) noexcept :
		m_ShaderManager(createInfo.m_ShaderManager),
		m_PipelineSystem(createInfo.m_PipelineSystem)
	{
		GGLAB_ASSERT_NOT_NULL(m_ShaderManager);
		GGLAB_ASSERT_NOT_NULL(m_PipelineSystem);
	}

	RHIPipelineHandle PipelineCache::Resolve(
		GraphicsPipelineSlot& slot,
		const GraphicsPipelineRecipe& recipe,
		const RenderPassInfo& renderPassInfo) noexcept
	{
		const uint64_t shaderRevision = m_ShaderManager->GetRevision();
		const uint64_t pipelineSystemRevision = m_PipelineSystem->GetRevision();
		if (slot.m_Pipeline.IsValid() &&
			m_PipelineSystem->IsAlive(slot.m_Pipeline) &&
			slot.m_PipelineSystemRevision == pipelineSystemRevision &&
			slot.m_Recipe == recipe &&
			slot.m_ShaderRevision == shaderRevision)
		{
			RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
			return slot.m_Pipeline;
		}

		RHIGraphicsPipelineCreateInfo createInfo{};
		createInfo.m_Desc = BuildRHIGraphicsPipelineDesc(recipe);
		createInfo.m_VertexShader = m_ShaderManager->GetBytecode(recipe.m_VSId);
		createInfo.m_PixelShader = recipe.m_PSId.IsValid() ? m_ShaderManager->GetBytecode(recipe.m_PSId) : ShaderBytecode{};
		createInfo.m_DomainShader = recipe.m_DSId.IsValid() ? m_ShaderManager->GetBytecode(recipe.m_DSId) : ShaderBytecode{};
		createInfo.m_HullShader = recipe.m_HSId.IsValid() ? m_ShaderManager->GetBytecode(recipe.m_HSId) : ShaderBytecode{};
		createInfo.m_GeometryShader = recipe.m_GSId.IsValid() ? m_ShaderManager->GetBytecode(recipe.m_GSId) : ShaderBytecode{};
		slot.m_Pipeline = m_PipelineSystem->CreateGraphicsPipeline(createInfo);
		slot.m_Recipe = recipe;
		slot.m_ShaderRevision = shaderRevision;
		slot.m_PipelineSystemRevision = pipelineSystemRevision;
		RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
		return slot.m_Pipeline;
	}

	RHIPipelineHandle PipelineCache::Resolve(
		ComputePipelineSlot& slot,
		const ComputePipelineRecipe& recipe,
		const RenderPassInfo& renderPassInfo) noexcept
	{
		const uint64_t shaderRevision = m_ShaderManager->GetRevision();
		const uint64_t pipelineSystemRevision = m_PipelineSystem->GetRevision();
		if (slot.m_Pipeline.IsValid() &&
			m_PipelineSystem->IsAlive(slot.m_Pipeline) &&
			slot.m_PipelineSystemRevision == pipelineSystemRevision &&
			slot.m_Recipe == recipe &&
			slot.m_ShaderRevision == shaderRevision)
		{
			RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
			return slot.m_Pipeline;
		}

		RHIComputePipelineCreateInfo createInfo{};
		createInfo.m_Desc.m_BindingLayout = recipe.m_BindingLayout;
		createInfo.m_Desc.m_ComputeShader = recipe.m_CSId.IsValid() ?
			RHIShaderHandle{ recipe.m_CSId.Value(), 1u } : RHIShaderHandle{};
		createInfo.m_ComputeShader = m_ShaderManager->GetBytecode(recipe.m_CSId);
		slot.m_Pipeline = m_PipelineSystem->CreateComputePipeline(createInfo);
		slot.m_Recipe = recipe;
		slot.m_ShaderRevision = shaderRevision;
		slot.m_PipelineSystemRevision = pipelineSystemRevision;
		RecordPipelineUsage(slot.m_Pipeline, renderPassInfo);
		return slot.m_Pipeline;
	}

	void PipelineCache::GetPipelineUsages(
		RHIPipelineHandle pipeline,
		std::vector<RenderPassInfo>& outUsages) const noexcept
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
		RHIPipelineHandle pipeline,
		const RenderPassInfo& renderPassInfo) noexcept
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
		const auto existing = std::ranges::find_if(usages,
			[&renderPassInfo](const RenderPassInfo& usage)
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
