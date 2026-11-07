#pragma once
#include "Graphics/GIShaderConstants.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"

#include <algorithm>
#include <cstdint>

namespace gglab
{
	struct GIObservationTile
	{
		uint32_t m_X = 0;
		uint32_t m_Y = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_BeginSweep = false;
		bool m_EndSweep = false;
	};

	using gi_shader::GIQueryGroupWidth;
	using gi_shader::GIQueryGroupHeight;
	inline constexpr uint64_t GIQueryWorkPerDispatch = 1ull << 23;

	// Resolve-only frames still visit every pixel. Round down to whole workgroups,
	// retaining at least one group even when one row exceeds the dispatch budget.
	[[nodiscard]] inline uint32_t GIQueryRowsPerDispatch(uint32_t width,
		uint32_t samples, uint32_t queriesPerSample, uint32_t receiverQueriesPerPixel = 0) noexcept
	{
		const uint64_t rowWork = uint64_t(width) *
			std::max(1ull, uint64_t(samples) * queriesPerSample + receiverQueriesPerPixel);
		return std::max(GIQueryGroupHeight,
			static_cast<uint32_t>(GIQueryWorkPerDispatch / rowWork) /
				GIQueryGroupHeight * GIQueryGroupHeight);
	}

	struct GIQueryDispatchInfo
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Samples = 0;
		uint32_t m_QueriesPerSample = 0;
		uint32_t m_ReceiverQueriesPerPixel = 0;
	};

	// Always visit the full viewport, including preparation-only and resolve-only frames.
	// The caller owns its constant layout and recording/publication gate, not this loop.
	template <typename SetConstants>
	[[nodiscard]] bool RecordGIQueryDispatches(RHIComputeCommandContext& commands,
		const GIQueryDispatchInfo& info, SetConstants&& setConstants) noexcept
	{
		if (info.m_Width == 0 || info.m_Height == 0)
		{
			commands.RejectRecording();
			return false;
		}
		if (commands.HasRecordingError())
		{
			return false;
		}
		const auto rows = GIQueryRowsPerDispatch(info.m_Width, info.m_Samples,
			info.m_QueriesPerSample, info.m_ReceiverQueriesPerPixel);
		const auto groupsX = (info.m_Width + GIQueryGroupWidth - 1) / GIQueryGroupWidth;
		for (uint32_t row = 0; row < info.m_Height;)
		{
			if (!setConstants(row) || commands.HasRecordingError())
			{
				return false;
			}
			const auto count = std::min(rows, info.m_Height - row);
			commands.Dispatch(groupsX, (count + GIQueryGroupHeight - 1) / GIQueryGroupHeight, 1);
			// Dispatch returns void. Check even the last batch before the caller marks recording complete.
			if (commands.HasRecordingError())
			{
				return false;
			}
			row += count;
		}
		return true;
	}
}
