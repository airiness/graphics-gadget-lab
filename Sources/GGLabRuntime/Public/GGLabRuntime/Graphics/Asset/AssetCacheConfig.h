#pragma once
#include <cstdint>

namespace gglab
{
	struct TextureArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 512ull * 1024ull * 1024ull;
	};

	struct ModelImportArtifactCacheConfig
	{
		uint64_t m_BudgetBytes = 512ull * 1024ull * 1024ull;
	};
}
