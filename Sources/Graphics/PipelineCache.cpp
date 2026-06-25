#include "Core/Precompiled.h"
#include "Graphics/PipelineCache.h"
#include "Graphics/RHIPipelineRecipeAdapter.h"
#include "Graphics/RHI/DX12/Cache/DX12PSOCache.h"
#include "Graphics/RHI/DX12/Cache/DX12RootSignatureCache.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/ShaderManager.h"

namespace gglab
{
	namespace
	{
		std::atomic_uint64_t NextPipelineCacheId = 1;

		[[nodiscard]] RootSignatureHandle ResolveRootSignatureHandle(
			DX12RootSignatureCache* rootSignatureCache,
			RootSignatureID rootSignatureId) noexcept
		{
			GGLAB_ASSERT_NOT_NULL(rootSignatureCache);
			DX12RootSignature* rootSignature =
				rootSignatureCache->GetDX12RootSignature(rootSignatureId);
			GGLAB_ASSERT_NOT_NULL(rootSignature);
			return RootSignatureHandle{
				rootSignatureId,
				rootSignature ? rootSignature->Get() : nullptr
			};
		}

		[[nodiscard]] DX12GraphicsPipelineShaderInputs BuildShaderInputs(
			ShaderManager* shaderManager,
			const GraphicsPipelineRecipe& recipe) noexcept
		{
			GGLAB_ASSERT_NOT_NULL(shaderManager);

			DX12GraphicsPipelineShaderInputs inputs{};
			inputs.m_VertexShader = shaderManager->GetBytecode(recipe.m_VSId);
			inputs.m_VertexShaderHash = recipe.m_VSId.IsValid() ?
				shaderManager->GetHash(recipe.m_VSId) :
				ShaderHash128{};

			if (recipe.m_PSId.IsValid())
			{
				inputs.m_PixelShader = shaderManager->GetBytecode(recipe.m_PSId);
				inputs.m_PixelShaderHash = shaderManager->GetHash(recipe.m_PSId);
			}
			if (recipe.m_DSId.IsValid())
			{
				inputs.m_DomainShader = shaderManager->GetBytecode(recipe.m_DSId);
				inputs.m_DomainShaderHash = shaderManager->GetHash(recipe.m_DSId);
			}
			if (recipe.m_HSId.IsValid())
			{
				inputs.m_HullShader = shaderManager->GetBytecode(recipe.m_HSId);
				inputs.m_HullShaderHash = shaderManager->GetHash(recipe.m_HSId);
			}
			if (recipe.m_GSId.IsValid())
			{
				inputs.m_GeometryShader = shaderManager->GetBytecode(recipe.m_GSId);
				inputs.m_GeometryShaderHash = shaderManager->GetHash(recipe.m_GSId);
			}
			return inputs;
		}
	}

	PipelineCache::PipelineCache(const CreateInfo& createInfo) noexcept :
		m_ShaderManager(createInfo.m_ShaderManager),
		m_RootSignatureCache(createInfo.m_RootSignatureCache),
		m_PSOCache(createInfo.m_PSOCache),
		m_CacheId(NextPipelineCacheId.fetch_add(1, std::memory_order_relaxed))
	{
		GGLAB_ASSERT_NOT_NULL(m_ShaderManager);
		GGLAB_ASSERT_NOT_NULL(m_RootSignatureCache);
		GGLAB_ASSERT_NOT_NULL(m_PSOCache);
	}

	DX12PipelineState* PipelineCache::Resolve(
		GraphicsPipelineSlot& slot,
		const GraphicsPipelineRecipe& recipe) noexcept
	{
		const uint64_t shaderRevision = m_ShaderManager->GetRevision();
		const uint64_t psoCacheRevision = m_PSOCache->GetRevision();
		if (slot.m_PipelineState &&
			slot.m_PipelineCacheId == m_CacheId &&
			slot.m_PSOCacheRevision == psoCacheRevision &&
			slot.m_Recipe == recipe &&
			slot.m_ShaderRevision == shaderRevision)
		{
			return slot.m_PipelineState;
		}

		const RHIGraphicsPipelineDesc rhiDesc = BuildRHIGraphicsPipelineDesc(recipe);
		const RootSignatureHandle rootSignature = ResolveRootSignatureHandle(
			m_RootSignatureCache,
			recipe.m_RootSignatureId);
		const DX12GraphicsPipelineShaderInputs shaderInputs =
			BuildShaderInputs(m_ShaderManager, recipe);

		slot.m_PipelineState = m_PSOCache->GetOrCreate(
			rhiDesc,
			rootSignature,
			shaderInputs);
		slot.m_Recipe = recipe;
		slot.m_ShaderRevision = shaderRevision;
		slot.m_PipelineCacheId = m_CacheId;
		slot.m_PSOCacheRevision = m_PSOCache->GetRevision();
		return slot.m_PipelineState;
	}

	DX12PipelineState* PipelineCache::Resolve(
		ComputePipelineSlot& slot,
		const ComputePipelineRecipe& recipe) noexcept
	{
		const uint64_t shaderRevision = m_ShaderManager->GetRevision();
		const uint64_t psoCacheRevision = m_PSOCache->GetRevision();
		if (slot.m_PipelineState &&
			slot.m_PipelineCacheId == m_CacheId &&
			slot.m_PSOCacheRevision == psoCacheRevision &&
			slot.m_Recipe == recipe &&
			slot.m_ShaderRevision == shaderRevision)
		{
			return slot.m_PipelineState;
		}

		ComputePipelineDesc desc = BuildComputeDesc(recipe);
		GGLAB_ASSERT_MSG(desc.Validate(), "ComputePipelineDesc is not valid");

		const ShaderHash128 csHash = recipe.m_CSId.IsValid() ?
			m_ShaderManager->GetHash(recipe.m_CSId) :
			ShaderHash128{};
		const ComputePSOKey key = desc.MakeKey(csHash);

		slot.m_PipelineState = m_PSOCache->GetOrCreate(key, desc);
		slot.m_Recipe = recipe;
		slot.m_ShaderRevision = shaderRevision;
		slot.m_PipelineCacheId = m_CacheId;
		slot.m_PSOCacheRevision = m_PSOCache->GetRevision();
		return slot.m_PipelineState;
	}

	ComputePipelineDesc PipelineCache::BuildComputeDesc(
		const ComputePipelineRecipe& recipe) const noexcept
	{
		ComputePipelineDesc desc{};
		desc.m_RootSignatureId = recipe.m_RootSignatureId;

		auto* rootSignature =
			m_RootSignatureCache->GetDX12RootSignature(recipe.m_RootSignatureId);
		GGLAB_ASSERT_NOT_NULL(rootSignature);
		desc.m_RootSignature = rootSignature ? rootSignature->Get() : nullptr;
		desc.m_ComputeShader = m_ShaderManager->GetBytecode(recipe.m_CSId);
		return desc;
	}
}
