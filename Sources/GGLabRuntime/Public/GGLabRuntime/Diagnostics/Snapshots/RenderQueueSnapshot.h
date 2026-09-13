#pragma once

#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gglab
{
	struct RenderQueueStatisticsSnapshot
	{
		uint32_t m_TotalInstanceCount = 0;
		uint32_t m_VisibleInstanceCount = 0;
		uint32_t m_CulledInstanceCount = 0;
		uint32_t m_InvalidInstanceCount = 0;
		uint32_t m_UnboundedInstanceCount = 0;
		uint32_t m_DrawItemCount = 0;
	};

	struct RenderQueueBucketSnapshot
	{
		uint32_t m_Start = 0;
		uint32_t m_Count = 0;
		uint32_t m_UniqueMeshes = 0;
		uint32_t m_UniqueMaterials = 0;
	};

	struct RenderQueueEntrySnapshot
	{
		RenderViewID m_ViewId = RenderViewID::Unknown;
		RenderQueueStatisticsSnapshot m_Statistics{};
		std::array<RenderQueueBucketSnapshot, static_cast<size_t>(RenderBucket::Count)> m_Buckets{};
	};

	// Owns observations only; no draw packets, resource handles or borrowed queue storage.
	struct RenderQueueSnapshot
	{
		std::vector<RenderQueueEntrySnapshot> m_Queues;
	};

	template <> struct SnapshotTraits<RenderQueueSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.RenderQueueSnapshot");
	};
}
