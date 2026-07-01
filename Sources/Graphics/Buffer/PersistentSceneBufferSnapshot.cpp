#include "Core/Precompiled.h"
#include "Graphics/Buffer/PersistentSceneBufferSnapshot.h"
#include "Graphics/Buffer/PersistentStructuredBuffer.h"
#include "Graphics/Buffer/PersistentStructuredBufferTable.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/Renderer.h"

namespace gglab
{
	namespace
	{
		template<typename Key, typename T, typename KeyFormatter>
		void BuildTableSnapshot(
			std::string_view name,
			const PersistentStructuredBuffer<T>& buffer,
			const PersistentStructuredBufferTable<Key, T>& table,
			KeyFormatter&& keyFormatter,
			bool includeFreeDirtySlots,
			PersistentBufferTableSnapshot& outSnapshot)
		{
			outSnapshot = {};
			outSnapshot.m_Name = name;
			outSnapshot.m_MemoryUsage = RHIMemoryUsage::GpuOnly;
			outSnapshot.m_ElementStride = static_cast<uint32_t>(sizeof(T));
			outSnapshot.m_Capacity = table.GetCapacity();
			outSnapshot.m_LiveCount = table.GetLiveCount();
			outSnapshot.m_FreeCount = table.GetFreeCount();
			outSnapshot.m_UpdateSerial = table.GetUpdateSerial();

			outSnapshot.m_BufferVersions.reserve(table.GetBufferCount());
			for (uint32_t bufferIndex = 0; bufferIndex < table.GetBufferCount(); ++bufferIndex)
			{
				const auto dirtyRanges = includeFreeDirtySlots ?
					table.BuildDirtyRangesIncludingFreeSlots(bufferIndex) :
					table.BuildDirtyRanges(bufferIndex);
				uint32_t pendingSlots = 0;
				for (const auto& range : dirtyRanges)
				{
					pendingSlots += range.m_ElementCount;
				}
				const auto& stats = table.GetBufferVersionStats(bufferIndex);
				outSnapshot.m_BufferVersions.push_back({
					.m_BufferIndex = bufferIndex,
					.m_Buffer = buffer.GetBufferHandle(bufferIndex),
					.m_PendingSlotCount = pendingSlots,
					.m_PendingRangeCount = static_cast<uint32_t>(dirtyRanges.size()),
					.m_PendingUploadBytes = static_cast<uint64_t>(pendingSlots) * sizeof(T),
					.m_LastUploadRangeCount = stats.m_LastUploadRangeCount,
					.m_LastUploadBytes = stats.m_LastUploadBytes,
					.m_TotalUploadBytes = stats.m_TotalUploadBytes,
				});
			}

			outSnapshot.m_Slots.reserve(table.GetCapacity());
			for (uint32_t slot = 0; slot < table.GetCapacity(); ++slot)
			{
				PersistentBufferSlotSnapshot slotSnapshot{};
				slotSnapshot.m_Slot = slot;
				slotSnapshot.m_Occupied = table.IsOccupied(slot);
				slotSnapshot.m_Revision = table.GetRevision(slot);
				if (slotSnapshot.m_Occupied)
				{
					slotSnapshot.m_Key = keyFormatter(table.GetKey(slot).value());
				}
				slotSnapshot.m_UploadedRevisions.reserve(table.GetBufferCount());
				for (uint32_t bufferIndex = 0; bufferIndex < table.GetBufferCount(); ++bufferIndex)
				{
					slotSnapshot.m_UploadedRevisions.push_back(
						table.GetUploadedRevision(bufferIndex, slot));
				}
				outSnapshot.m_Slots.push_back(std::move(slotSnapshot));
			}
		}
	}

	void BuildPersistentSceneBufferSnapshot(
		const Renderer& renderer,
		PersistentSceneBufferSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		const auto* objectBuffer = renderer.GetObjectStructuredBuffer();
		const auto* materialBuffer = renderer.GetMaterialStructuredBuffer();
		const auto* lightBuffer = renderer.GetLightStructuredBuffer();
		const auto* objectTable = renderer.GetObjectStructuredBufferTable();
		const auto* materialTable = renderer.GetMaterialStructuredBufferTable();
		const auto* lightTable = renderer.GetLightStructuredBufferTable();
		if (!objectBuffer || !materialBuffer || !lightBuffer ||
			!objectTable || !materialTable || !lightTable)
		{
			return;
		}

		BuildTableSnapshot(
			"Objects", *objectBuffer, *objectTable,
			[](uint64_t key)
			{
				return std::format("entity={} submesh={}", key >> 32, key & 0xffffffffull);
			},
			false,
			outSnapshot.m_Objects);
		BuildTableSnapshot(
			"Materials", *materialBuffer, *materialTable,
			[](MaterialID key)
			{
				return std::format("MaterialID {}", key.Value());
			},
			false,
			outSnapshot.m_Materials);
		BuildTableSnapshot(
			"Lights", *lightBuffer, *lightTable,
			[](uint64_t key)
			{
				return key == std::numeric_limits<uint64_t>::max() ?
					std::string("DefaultDirectionalLight") :
					std::format("entity={}", key);
			},
			true,
			outSnapshot.m_Lights);
	}
}
