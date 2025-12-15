#include "Precompiled.h"
#include "RenderPassRecipeRegistry.h"
#include "FNV1a.h"

namespace gglab
{
	RenderPassRecipeRegistry::RenderPassRecipeRegistry(ShaderManager* shaderManager) noexcept :
		m_ShaderManager(shaderManager)
	{
		GGLAB_ASSERT_MSG(shaderManager != nullptr, "ShaderManager must not be null");
	}

	const CachedGraphics& RenderPassRecipeRegistry::GetOrCreateGraphics(std::string_view passId, const GraphicsKeyInputs& input, const GraphicsBuilder& builder) noexcept
	{
		const size_t hash = KeyHash<GraphicsKeyInputs>{}(input);
		StringId passIdKey(passId);

		{
			std::shared_lock lock(m_Mutex);
			if (const auto it = m_GraphicsMap.find(passIdKey); it != m_GraphicsMap.end())
			{
				if (it->second.m_Hash == hash)
				{
					return it->second.m_Cached;
				}
			}
		}

		// Can not find, create new
		{
			std::unique_lock lock(m_Mutex);
			auto& entry = m_GraphicsMap[passIdKey];
			if (entry.m_Hash != hash)
			{
				GraphicsPipelineDesc desc{};
				builder(desc, input, m_ShaderManager);
				GGLAB_ASSERT_MSG(desc.Validate(), "GraphicsPipelineDesc is not valid");

				const ShaderHash128 vsHash = input.m_VSId.IsValid() ? m_ShaderManager->GetHash(input.m_VSId) : ShaderHash128{};
				const ShaderHash128 psHash = input.m_PSId.IsValid() ? m_ShaderManager->GetHash(input.m_PSId) : ShaderHash128{};
				const ShaderHash128 dsHash = input.m_DSId.IsValid() ? m_ShaderManager->GetHash(input.m_DSId) : ShaderHash128{};
				const ShaderHash128 hsHash = input.m_HSId.IsValid() ? m_ShaderManager->GetHash(input.m_HSId) : ShaderHash128{};
				const ShaderHash128 gsHash = input.m_GSId.IsValid() ? m_ShaderManager->GetHash(input.m_GSId) : ShaderHash128{};

				GraphicsPSOKey key = desc.MakeKey(vsHash, psHash, dsHash, hsHash, gsHash);
				entry.m_Cached = { key, std::move(desc) };
				entry.m_Hash = hash;
				entry.m_Inputs = input;
			}
			return entry.m_Cached;
		}
	}

	const CachedCompute& RenderPassRecipeRegistry::GetOrCreateCompute(std::string_view passId, const ComputeKeyInputs& input, const ComputeBuilder& builder) noexcept
	{
		const size_t hash = KeyHash<ComputeKeyInputs>{}(input);
		StringId passIdKey(passId);
	
		{
			std::shared_lock lock(m_Mutex);
			if (const auto it = m_ComputeMap.find(passIdKey); it != m_ComputeMap.end())
			{
				if (it->second.m_Hash == hash)
				{
					return it->second.m_Cached;
				}
			}
		}
		// Can not find, create new
		{
			std::unique_lock lock(m_Mutex);
			auto& entry = m_ComputeMap[passIdKey];
			if (entry.m_Hash != hash)
			{
				ComputePipelineDesc desc{};
				builder(desc, input, m_ShaderManager);
				GGLAB_ASSERT_MSG(desc.Validate(), "ComputePipelineDesc is not valid");

				const ShaderHash128 csHash = input.m_CSId.Value() ? m_ShaderManager->GetHash(input.m_CSId) : ShaderHash128{};
				ComputePSOKey key = desc.MakeKey(csHash);

				entry.m_Cached = { key, std::move(desc) };
				entry.m_Hash = hash;
				entry.m_Inputs = input;
			}
			return entry.m_Cached;
		}
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
		return m_GraphicsMap.erase(StringId(passId)) > 0;
	}

	bool RenderPassRecipeRegistry::EraseCompute(std::string_view passId) noexcept
	{
		std::unique_lock lock(m_Mutex);
		return m_ComputeMap.erase(StringId(passId)) > 0;
	}

	size_t RenderPassRecipeRegistry::InvalidateByShader(const std::vector<ShaderId>& shaderIds) noexcept
	{
		if (shaderIds.empty())
		{
			return 0;
		}

		std::unique_lock lock(m_Mutex);
		auto hitGraphics = [&](const GraphicsKeyInputs& input) -> bool
			{
				for (ShaderId id : shaderIds)
				{
					if (!id.IsValid())
					{
						continue;
					}

					if(input.m_VSId == id ||
					   input.m_PSId == id ||
					   input.m_DSId == id ||
					   input.m_HSId == id ||
					   input.m_GSId == id)
					{
						return true;
					}
				}
				return false;
			};

		auto hitCompute = [&](const ComputeKeyInputs& input) -> bool
			{
				for (ShaderId id : shaderIds)
				{
					if (!id.IsValid())
					{
						continue;
					}
					if (input.m_CSId == id)
					{
						return true;
					}
				}
				return false;
			};

		size_t removedCount = 0;
		for (auto iter = m_GraphicsMap.begin(); iter != m_GraphicsMap.end(); )
		{
			if (hitGraphics(iter->second.m_Inputs))
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
			if (hitCompute(it->second.m_Inputs))
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