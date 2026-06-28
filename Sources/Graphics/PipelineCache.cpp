#include "Core/Precompiled.h"
#include "Graphics/PipelineCache.h"
#include "Graphics/RHIPipelineRecipeAdapter.h"
#include "Graphics/RHI/RHIPipelineSystem.h"
#include "Graphics/ShaderManager.h"

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
		const GraphicsPipelineRecipe& recipe) noexcept
	{
		const uint64_t shaderRevision = m_ShaderManager->GetRevision();
		const uint64_t pipelineSystemRevision = m_PipelineSystem->GetRevision();
		if (slot.m_Pipeline.IsValid() &&
			m_PipelineSystem->IsAlive(slot.m_Pipeline) &&
			slot.m_PipelineSystemRevision == pipelineSystemRevision &&
			slot.m_Recipe == recipe &&
			slot.m_ShaderRevision == shaderRevision)
		{
			return slot.m_Pipeline;
		}

		RHIGraphicsPipelineCreateInfo createInfo{};
		createInfo.m_Desc = BuildRHIGraphicsPipelineDesc(recipe);
		createInfo.m_VertexShader = m_ShaderManager->GetRHIShaderBytecode(recipe.m_VSId);
		createInfo.m_PixelShader = m_ShaderManager->GetRHIShaderBytecode(recipe.m_PSId);
		createInfo.m_DomainShader = m_ShaderManager->GetRHIShaderBytecode(recipe.m_DSId);
		createInfo.m_HullShader = m_ShaderManager->GetRHIShaderBytecode(recipe.m_HSId);
		createInfo.m_GeometryShader = m_ShaderManager->GetRHIShaderBytecode(recipe.m_GSId);
		slot.m_Pipeline = m_PipelineSystem->CreateGraphicsPipeline(createInfo);
		slot.m_Recipe = recipe;
		slot.m_ShaderRevision = shaderRevision;
		slot.m_PipelineSystemRevision = pipelineSystemRevision;
		return slot.m_Pipeline;
	}

	RHIPipelineHandle PipelineCache::Resolve(
		ComputePipelineSlot& slot,
		const ComputePipelineRecipe& recipe) noexcept
	{
		const uint64_t shaderRevision = m_ShaderManager->GetRevision();
		const uint64_t pipelineSystemRevision = m_PipelineSystem->GetRevision();
		if (slot.m_Pipeline.IsValid() &&
			m_PipelineSystem->IsAlive(slot.m_Pipeline) &&
			slot.m_PipelineSystemRevision == pipelineSystemRevision &&
			slot.m_Recipe == recipe &&
			slot.m_ShaderRevision == shaderRevision)
		{
			return slot.m_Pipeline;
		}

		RHIComputePipelineCreateInfo createInfo{};
		createInfo.m_Desc.m_BindingLayout = recipe.m_BindingLayout;
		createInfo.m_Desc.m_ComputeShader = recipe.m_CSId.IsValid() ?
			RHIShaderHandle{ recipe.m_CSId.Value(), 1u } : RHIShaderHandle{};
		createInfo.m_ComputeShader = m_ShaderManager->GetRHIShaderBytecode(recipe.m_CSId);
		slot.m_Pipeline = m_PipelineSystem->CreateComputePipeline(createInfo);
		slot.m_Recipe = recipe;
		slot.m_ShaderRevision = shaderRevision;
		slot.m_PipelineSystemRevision = pipelineSystemRevision;
		return slot.m_Pipeline;
	}
}
