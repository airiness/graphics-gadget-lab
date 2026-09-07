#include "Diagnostics/Builders/RenderQueueSnapshotBuilder.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"

#include <algorithm>

namespace gglab
{
	namespace
	{
		template <typename T, typename Predicate>
		uint32_t CountUniqueInRange(std::span<const DrawItem> items,
			const DrawItemsRange& range, Predicate predicate)
		{
			const size_t start = std::min<size_t>(range.m_Start, items.size());
			const size_t count = std::min<size_t>(range.m_Count, items.size() - start);
			std::vector<T> values;
			values.reserve(count);
			for (size_t index = start; index < start + count; ++index)
			{
				const T value = predicate(items[index]);
				if (std::find(values.begin(), values.end(), value) == values.end())
				{
					values.push_back(value);
				}
			}
			return static_cast<uint32_t>(values.size());
		}
	}

	RenderQueueSnapshot BuildRenderQueueSnapshot(std::span<const RenderQueue> queues) noexcept
	{
		RenderQueueSnapshot result;
		result.m_Queues.reserve(queues.size());
		for (const RenderQueue& queue : queues)
		{
			RenderQueueEntrySnapshot entry;
			entry.m_ViewId = queue.m_ViewId;
			const auto& stats = queue.m_Statistics;
			entry.m_Statistics = { stats.m_TotalInstanceCount, stats.m_VisibleInstanceCount,
				stats.m_CulledInstanceCount, stats.m_InvalidInstanceCount,
				stats.m_UnboundedInstanceCount, stats.m_DrawItemCount };
			for (size_t index = 0; index < entry.m_Buckets.size(); ++index)
			{
				const auto& range = queue.m_BucketDrawRanges[index];
				entry.m_Buckets[index] = { range.m_Start, range.m_Count,
					CountUniqueInRange<MeshID>(queue.m_DrawItems, range,
						[](const DrawItem& item) { return item.m_CoverageDrawPacket.m_Geometry.m_MeshId; }),
					CountUniqueInRange<RenderMaterialKey>(queue.m_DrawItems, range,
						[](const DrawItem& item) { return item.m_MaterialKey; }) };
			}
			result.m_Queues.push_back(entry);
		}
		return result;
	}
}
