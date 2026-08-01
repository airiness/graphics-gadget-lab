#pragma once

#include <cstdint>

namespace gglab
{
	inline constexpr uint32_t MaxBloomPyramidLevels = 8;

	enum class PostProcessDebugTap : uint8_t
	{
		SceneColor = 0,
		BloomPrefilter = 1,
		BloomPyramid = 2,
		BloomResult = 3,
		SceneDepthRaw = 4,
		SceneDepthLinearViewZ = 5,
		GTAORawAO = 6,
		GTAOHalfDepthViewZ = 7,
		GTAOReconstructedNormal = 8,
		GTAOSelectedSurfaceOffset = 9,

		Count = 10
	};

	struct PostProcessDebugSelection
	{
		PostProcessDebugTap m_Tap = PostProcessDebugTap::BloomResult;
		uint32_t m_BloomPyramidLevel = 0;

		bool operator==(const PostProcessDebugSelection&) const noexcept = default;
	};
}
