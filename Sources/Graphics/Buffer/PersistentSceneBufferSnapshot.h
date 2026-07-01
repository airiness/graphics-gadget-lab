#pragma once
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHIResource.h"

#include <string>
#include <vector>

namespace gglab
{
	class Renderer;

	struct PersistentBufferVersionSnapshot
	{
		uint32_t m_BufferIndex = 0;
		RHIBufferHandle m_Buffer{};
		uint32_t m_PendingSlotCount = 0;
		uint32_t m_PendingRangeCount = 0;
		uint64_t m_PendingUploadBytes = 0;
		uint32_t m_LastUploadRangeCount = 0;
		uint64_t m_LastUploadBytes = 0;
		uint64_t m_TotalUploadBytes = 0;
	};

	struct PersistentBufferSlotSnapshot
	{
		uint32_t m_Slot = 0;
		bool m_Occupied = false;
		std::string m_Key;
		uint64_t m_Revision = 0;
		std::vector<uint64_t> m_UploadedRevisions;
	};

	struct PersistentBufferTableSnapshot
	{
		std::string m_Name;
		RHIMemoryUsage m_MemoryUsage = RHIMemoryUsage::GpuOnly;
		uint32_t m_ElementStride = 0;
		uint32_t m_Capacity = 0;
		uint32_t m_LiveCount = 0;
		uint32_t m_FreeCount = 0;
		uint64_t m_UpdateSerial = 0;
		std::vector<PersistentBufferVersionSnapshot> m_BufferVersions;
		std::vector<PersistentBufferSlotSnapshot> m_Slots;
	};

	struct PersistentSceneBufferSnapshot
	{
		PersistentBufferTableSnapshot m_Objects;
		PersistentBufferTableSnapshot m_Materials;
		PersistentBufferTableSnapshot m_Lights;
	};

	void BuildPersistentSceneBufferSnapshot(
		const Renderer& renderer,
		PersistentSceneBufferSnapshot& outSnapshot) noexcept;
}
