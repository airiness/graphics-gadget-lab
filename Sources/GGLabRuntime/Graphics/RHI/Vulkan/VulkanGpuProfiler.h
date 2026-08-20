#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Profiling/GpuProfiler.h"

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class VulkanDevice;

	[[nodiscard]] constexpr inline uint64_t VulkanTimestampDelta(
		uint64_t begin, uint64_t end, uint32_t validBits) noexcept
	{
		if (validBits == 0)
		{
			return 0;
		}
		if (validBits >= 64)
		{
			return end - begin;
		}
		const uint64_t mask = (uint64_t{ 1 } << validBits) - 1;
		return (end - begin) & mask;
	}

	class VulkanGpuProfiler final : public GpuProfiler
	{
	public:
		VulkanGpuProfiler(VulkanDevice* device, uint32_t frameSlotCount) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(VulkanGpuProfiler);
		~VulkanGpuProfiler() override;

		void SetEnabled(bool enabled) noexcept override;
		[[nodiscard]] bool IsEnabled() const noexcept override;
		[[nodiscard]] GpuProfileFrameSnapshot GetLatestFrame() const override;

		void BeginFrame(uint32_t frameSlotIndex, VkCommandBuffer commandBuffer) noexcept;
		void EndFrame(uint32_t frameSlotIndex, VkCommandBuffer commandBuffer) noexcept;
		void AbortFrame(uint32_t frameSlotIndex) noexcept;
		void BeginScope(VkCommandBuffer commandBuffer, std::string_view name) noexcept;
		void EndScope(VkCommandBuffer commandBuffer) noexcept;

		[[nodiscard]] bool IsAvailable() const noexcept { return m_Available; }
		[[nodiscard]] uint32_t GetTimestampValidBits() const noexcept
		{
			return m_TimestampValidBits;
		}

	private:
		static constexpr uint32_t MaxScopeCount = 128;
		static constexpr uint32_t MaxTimestampCount = MaxScopeCount * 2 + 2;
		static constexpr uint32_t InvalidQueryIndex = std::numeric_limits<uint32_t>::max();

		struct ScopeRecord
		{
			std::string m_Name;
			uint32_t m_BeginQuery = InvalidQueryIndex;
			uint32_t m_EndQuery = InvalidQueryIndex;
		};

		struct FrameResource
		{
			VkQueryPool m_QueryPool = VK_NULL_HANDLE;
			uint64_t m_FrameIndex = 0;
			uint32_t m_TimestampCount = 0;
			uint32_t m_FrameBeginQuery = InvalidQueryIndex;
			uint32_t m_FrameEndQuery = InvalidQueryIndex;
			std::vector<uint64_t> m_Timestamps;
			std::vector<ScopeRecord> m_Scopes;
			std::vector<int32_t> m_ScopeStack;
			bool m_PendingResults = false;
			bool m_Recording = false;
		};

		void ResolveCompletedFrame(FrameResource& frame) noexcept;
		uint32_t WriteTimestamp(FrameResource& frame, VkCommandBuffer commandBuffer) noexcept;

		VulkanDevice* m_Device = nullptr;
		mutable std::mutex m_SnapshotMutex;
		std::atomic_bool m_Enabled = true;
		std::vector<FrameResource> m_Frames;
		GpuProfileFrameSnapshot m_LatestFrame;
		FrameResource* m_ActiveFrame = nullptr;
		uint32_t m_ActiveFrameSlot = 0;
		uint64_t m_NextFrameIndex = 0;
		double m_TimestampPeriodNanoseconds = 0.0;
		uint32_t m_TimestampValidBits = 0;
		bool m_Available = false;
	};
}
