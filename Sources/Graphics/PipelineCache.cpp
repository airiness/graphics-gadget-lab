#include "Core/Precompiled.h"
#include "Graphics/PipelineCache.h"
#include "Graphics/RHI/DX12/Cache/DX12PSOCache.h"
#include "Graphics/RHI/DX12/Cache/DX12RootSignatureCache.h"
#include "Graphics/RHI/DX12/Cache/InputLayoutLibrary.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/ShaderManager.h"

namespace gglab
{
	namespace
	{
		std::atomic_uint64_t NextPipelineCacheId = 1;
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

		GraphicsPipelineDesc desc = BuildGraphicsDesc(recipe);
		GGLAB_ASSERT_MSG(desc.Validate(), "GraphicsPipelineDesc is not valid");

		const ShaderHash128 vsHash = recipe.m_VSId.IsValid() ?
			m_ShaderManager->GetHash(recipe.m_VSId) :
			ShaderHash128{};
		const ShaderHash128 psHash = recipe.m_PSId.IsValid() ?
			m_ShaderManager->GetHash(recipe.m_PSId) :
			ShaderHash128{};
		const ShaderHash128 dsHash = recipe.m_DSId.IsValid() ?
			m_ShaderManager->GetHash(recipe.m_DSId) :
			ShaderHash128{};
		const ShaderHash128 hsHash = recipe.m_HSId.IsValid() ?
			m_ShaderManager->GetHash(recipe.m_HSId) :
			ShaderHash128{};
		const ShaderHash128 gsHash = recipe.m_GSId.IsValid() ?
			m_ShaderManager->GetHash(recipe.m_GSId) :
			ShaderHash128{};

		const GraphicsPSOKey key = desc.MakeKey(
			vsHash,
			psHash,
			dsHash,
			hsHash,
			gsHash);
		slot.m_PipelineState = m_PSOCache->GetOrCreate(key, desc);
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

	GraphicsPipelineDesc PipelineCache::BuildGraphicsDesc(
		const GraphicsPipelineRecipe& recipe) const noexcept
	{
		GraphicsPipelineDesc desc{};
		desc.m_RootSignatureId = recipe.m_RootSignatureId;

		auto* rootSignature =
			m_RootSignatureCache->GetDX12RootSignature(recipe.m_RootSignatureId);
		GGLAB_ASSERT_NOT_NULL(rootSignature);
		desc.m_RootSignature = rootSignature ? rootSignature->Get() : nullptr;

		desc.m_InputLayoutId = recipe.m_InputLayoutId;
		desc.m_InputLayoutDesc = InputLayoutLibrary::Get(recipe.m_InputLayoutId);
		desc.m_VertexShader = m_ShaderManager->GetBytecode(recipe.m_VSId);
		desc.m_PixelShader = recipe.m_PSId.IsValid() ?
			m_ShaderManager->GetBytecode(recipe.m_PSId) :
			D3D12_SHADER_BYTECODE{};
		desc.m_DomainShader = recipe.m_DSId.IsValid() ?
			m_ShaderManager->GetBytecode(recipe.m_DSId) :
			D3D12_SHADER_BYTECODE{};
		desc.m_HullShader = recipe.m_HSId.IsValid() ?
			m_ShaderManager->GetBytecode(recipe.m_HSId) :
			D3D12_SHADER_BYTECODE{};
		desc.m_GeometryShader = recipe.m_GSId.IsValid() ?
			m_ShaderManager->GetBytecode(recipe.m_GSId) :
			D3D12_SHADER_BYTECODE{};
		desc.m_Formats = recipe.m_Formats;
		desc.m_Topology = recipe.m_Topology;
		desc.m_SampleMask = recipe.m_SampleMask;
		desc.m_RasterizerDesc =
			ApplyRasterizerPreset(recipe.m_RasterizerPreset);
		desc.m_RasterizerDesc.DepthBias = recipe.m_DepthBias;
		desc.m_RasterizerDesc.DepthBiasClamp = recipe.m_DepthBiasClamp;
		desc.m_RasterizerDesc.SlopeScaledDepthBias =
			recipe.m_SlopeScaledDepthBias;
		desc.m_DepthDesc = ApplyDepthPreset(recipe.m_DepthPreset);
		desc.m_BlendDesc = ApplyBlendPreset(recipe.m_BlendPreset);
		return desc;
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
