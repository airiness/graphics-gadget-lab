#pragma once

#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t MaxBloomPyramidLevels = 8;

	enum class PostProcessDebugTap : uint8_t
	{
		SceneColor,
		BloomPrefilter,
		BloomPyramid,
		BloomResult,

		Count
	};

	struct PostProcessDebugSelection
	{
		PostProcessDebugTap m_Tap = PostProcessDebugTap::BloomResult;
		uint32_t m_BloomPyramidLevel = 0;

		bool operator==(const PostProcessDebugSelection&) const noexcept = default;
	};
}
