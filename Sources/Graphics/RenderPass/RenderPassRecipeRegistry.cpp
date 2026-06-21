#include "Core/Precompiled.h"
#include "Graphics/RenderPass/RenderPassRecipeRegistry.h"
#include "Graphics/DX12/Cache/DX12RootSignatureCache.h"
#include "Graphics/DX12/Cache/InputLayoutLibrary.h"
#include "Graphics/DX12/DX12RootSignature.h"

namespace gglab
{
	RenderPassRecipeRegistry::RenderPassRecipeRegistry(const CreateInfo& createInfo) noexcept :
		m_ShaderManager(createInfo.m_ShaderManager),
		m_RootSignatureCache(createInfo.m_RootSignatureCache)
	{
		GGLAB_ASSERT_NOT_NULL(m_ShaderManager);
		GGLAB_ASSERT_NOT_NULL(m_RootSignatureCache);
	}

	const CachedGraphics& RenderPassRecipeRegistry::GetOrCreateGraphics(
		std::string_view passId,
		const GraphicsPipelineRecipe& recipe) noexcept
	{
		const GraphicsShaderGenerations shaderGenerations = GetShaderGenerations(recipe);
		const StringID passIdKey(passId);

		{
			std::shared_lock lock(m_Mutex);
			if (const auto it = m_GraphicsMap.find(passIdKey); it != m_GraphicsMap.end())
			{
				if (it->second.m_Recipe == recipe &&
					it->second.m_ShaderGenerations == shaderGenerations)
				{
					return it->second.m_Cached;
				}
			}
		}

		// Can not find, create new
		{
			std::unique_lock lock(m_Mutex);
			auto [it, inserted] = m_GraphicsMap.try_emplace(passIdKey);
			auto& entry = it->second;
			if (inserted ||
				entry.m_Recipe != recipe ||
				entry.m_ShaderGenerations != shaderGenerations)
			{
				GraphicsPipelineDesc desc = BuildGraphicsDesc(recipe);
				GGLAB_ASSERT_MSG(desc.Validate(), "GraphicsPipelineDesc is not valid");

				const ShaderHash128 vsHash = recipe.m_VSId.IsValid() ? m_ShaderManager->GetHash(recipe.m_VSId) : ShaderHash128{};
				const ShaderHash128 psHash = recipe.m_PSId.IsValid() ? m_ShaderManager->GetHash(recipe.m_PSId) : ShaderHash128{};
				const ShaderHash128 dsHash = recipe.m_DSId.IsValid() ? m_ShaderManager->GetHash(recipe.m_DSId) : ShaderHash128{};
				const ShaderHash128 hsHash = recipe.m_HSId.IsValid() ? m_ShaderManager->GetHash(recipe.m_HSId) : ShaderHash128{};
				const ShaderHash128 gsHash = recipe.m_GSId.IsValid() ? m_ShaderManager->GetHash(recipe.m_GSId) : ShaderHash128{};

				GraphicsPSOKey key = desc.MakeKey(vsHash, psHash, dsHash, hsHash, gsHash);
				entry.m_Cached = { key, std::move(desc) };
				entry.m_Recipe = recipe;
				entry.m_ShaderGenerations = shaderGenerations;
			}
			return entry.m_Cached;
		}
	}

	const CachedCompute& RenderPassRecipeRegistry::GetOrCreateCompute(
		std::string_view passId,
		const ComputePipelineRecipe& recipe) noexcept
	{
		const uint64_t shaderGeneration = m_ShaderManager->GetGeneration(recipe.m_CSId);
		const StringID passIdKey(passId);
	
		{
			std::shared_lock lock(m_Mutex);
			if (const auto it = m_ComputeMap.find(passIdKey); it != m_ComputeMap.end())
			{
				if (it->second.m_Recipe == recipe &&
					it->second.m_ShaderGeneration == shaderGeneration)
				{
					return it->second.m_Cached;
				}
			}
		}
		// Can not find, create new
		{
			std::unique_lock lock(m_Mutex);
			auto [it, inserted] = m_ComputeMap.try_emplace(passIdKey);
			auto& entry = it->second;
			if (inserted ||
				entry.m_Recipe != recipe ||
				entry.m_ShaderGeneration != shaderGeneration)
			{
				ComputePipelineDesc desc = BuildComputeDesc(recipe);
				GGLAB_ASSERT_MSG(desc.Validate(), "ComputePipelineDesc is not valid");

				const ShaderHash128 csHash = recipe.m_CSId.IsValid() ?
					m_ShaderManager->GetHash(recipe.m_CSId) :
					ShaderHash128{};
				ComputePSOKey key = desc.MakeKey(csHash);

				entry.m_Cached = { key, std::move(desc) };
				entry.m_Recipe = recipe;
				entry.m_ShaderGeneration = shaderGeneration;
			}
			return entry.m_Cached;
		}
	}

	RenderPassRecipeRegistry::GraphicsShaderGenerations
		RenderPassRecipeRegistry::GetShaderGenerations(
			const GraphicsPipelineRecipe& recipe) const noexcept
	{
		return GraphicsShaderGenerations{
			.m_VS = m_ShaderManager->GetGeneration(recipe.m_VSId),
			.m_PS = m_ShaderManager->GetGeneration(recipe.m_PSId),
			.m_DS = m_ShaderManager->GetGeneration(recipe.m_DSId),
			.m_HS = m_ShaderManager->GetGeneration(recipe.m_HSId),
			.m_GS = m_ShaderManager->GetGeneration(recipe.m_GSId),
		};
	}

	GraphicsPipelineDesc RenderPassRecipeRegistry::BuildGraphicsDesc(
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
		desc.m_RasterizerDesc = ApplyRasterizerPreset(recipe.m_RasterizerPreset);
		desc.m_RasterizerDesc.DepthBias = recipe.m_DepthBias;
		desc.m_RasterizerDesc.DepthBiasClamp = recipe.m_DepthBiasClamp;
		desc.m_RasterizerDesc.SlopeScaledDepthBias =
			recipe.m_SlopeScaledDepthBias;
		desc.m_DepthDesc = ApplyDepthPreset(recipe.m_DepthPreset);
		desc.m_BlendDesc = ApplyBlendPreset(recipe.m_BlendPreset);
		return desc;
	}

	ComputePipelineDesc RenderPassRecipeRegistry::BuildComputeDesc(
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

	void RenderPassRecipeRegistry::Clear() noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_GraphicsMap.clear();
		m_ComputeMap.clear();
	}

	bool RenderPassRecipeRegistry::EraseGraphics(std::string_view passId) noexcept
	{
		std::unique_lock lock(m_Mutex);
		return m_GraphicsMap.erase(StringID(passId)) > 0;
	}

	bool RenderPassRecipeRegistry::EraseCompute(std::string_view passId) noexcept
	{
		std::unique_lock lock(m_Mutex);
		return m_ComputeMap.erase(StringID(passId)) > 0;
	}

	size_t RenderPassRecipeRegistry::InvalidateByShader(const std::vector<ShaderID>& shaderIds) noexcept
	{
		if (shaderIds.empty())
		{
			return 0;
		}

		std::unique_lock lock(m_Mutex);
		auto hitGraphics = [&](const GraphicsPipelineRecipe& recipe) -> bool
			{
				for (ShaderID id : shaderIds)
				{
					if (!id.IsValid())
					{
						continue;
					}

					if(recipe.m_VSId == id ||
					   recipe.m_PSId == id ||
					   recipe.m_DSId == id ||
					   recipe.m_HSId == id ||
					   recipe.m_GSId == id)
					{
						return true;
					}
				}
				return false;
			};

		auto hitCompute = [&](const ComputePipelineRecipe& recipe) -> bool
			{
				for (ShaderID id : shaderIds)
				{
					if (!id.IsValid())
					{
						continue;
					}
					if (recipe.m_CSId == id)
					{
						return true;
					}
				}
				return false;
			};

		size_t removedCount = 0;
		for (auto iter = m_GraphicsMap.begin(); iter != m_GraphicsMap.end(); )
		{
			if (hitGraphics(iter->second.m_Recipe))
			{
				iter = m_GraphicsMap.erase(iter);
				++removedCount;
			}
			else
			{
				++iter;
			}
		}

		for (auto it = m_ComputeMap.begin(); it != m_ComputeMap.end(); )
		{
			if (hitCompute(it->second.m_Recipe))
			{
				it = m_ComputeMap.erase(it);
				++removedCount;
			}
			else
			{
				++it;
			}
		}

		return removedCount;
	}
}
