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
		GTAODenoiseX = 10,
		GTAODenoiseY = 11,
		GTAOFinalAO = 12,

		Count = 13
	};
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::SceneColor) == 0);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::BloomPrefilter) == 1);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::BloomPyramid) == 2);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::BloomResult) == 3);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::SceneDepthRaw) == 4);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::SceneDepthLinearViewZ) == 5);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAORawAO) == 6);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOHalfDepthViewZ) == 7);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOReconstructedNormal) == 8);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOSelectedSurfaceOffset) == 9);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAODenoiseX) == 10);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAODenoiseY) == 11);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::GTAOFinalAO) == 12);
	static_assert(static_cast<uint8_t>(PostProcessDebugTap::Count) == 13);

	struct PostProcessDebugSelection
	{
		PostProcessDebugTap m_Tap = PostProcessDebugTap::BloomResult;
		uint32_t m_BloomPyramidLevel = 0;

		bool operator==(const PostProcessDebugSelection&) const noexcept = default;
	};
}
