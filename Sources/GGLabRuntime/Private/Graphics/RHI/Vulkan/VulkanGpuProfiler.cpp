#include "Graphics/RHI/Vulkan/VulkanGpuProfiler.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "Graphics/RHI/Vulkan/VulkanDevice.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <utility>

namespace gglab
{
	VulkanGpuProfiler::VulkanGpuProfiler(
		VulkanDevice* device, uint32_t frameSlotCount) noexcept :
		m_Device(device)
	{
		GGLAB_ASSERT_NOT_NULL(device);
		GGLAB_ASSERT_MSG(frameSlotCount > 0,
			"VulkanGpuProfiler requires at least one frame slot.");
		if (!device || frameSlotCount == 0)
		{
			m_Enabled.store(false, std::memory_order_relaxed);
			return;
		}

		m_TimestampPeriodNanoseconds = device->GetPhysicalDeviceLimits().timestampPeriod;
		uint32_t familyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device->GetPhysicalDevice(), &familyCount, nullptr);
		std::vector<VkQueueFamilyProperties> families(familyCount);
		if (familyCount > 0)
		{
			vkGetPhysicalDeviceQueueFamilyProperties(
				device->GetPhysicalDevice(), &familyCount, families.data());
		}
		const uint32_t familyIndex = device->GetGraphicsQueueFamilyIndex();
		if (familyIndex >= families.size() || families[familyIndex].timestampValidBits == 0 ||
			m_TimestampPeriodNanoseconds <= 0.0)
		{
			GGLAB_LOG_GRAPHICS_WARN_ALWAYS(
				"Vulkan GPU profiling is unavailable on the selected graphics queue family.");
			m_Enabled.store(false, std::memory_order_relaxed);
			return;
		}
		m_TimestampValidBits = families[familyIndex].timestampValidBits;

		m_Frames.resize(frameSlotCount);
		VkQueryPoolCreateInfo queryPoolInfo{};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryPoolInfo.queryCount = MaxTimestampCount;
		for (uint32_t frameSlotIndex = 0; frameSlotIndex < frameSlotCount; ++frameSlotIndex)
		{
			FrameResource& frame = m_Frames[frameSlotIndex];
			const VkResult createResult = vkCreateQueryPool(
				device->Get(), &queryPoolInfo, nullptr, &frame.m_QueryPool);
			if (createResult != VK_SUCCESS)
			{
				GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
					"Vulkan GPU profiler failed to create a timestamp query pool: {}.",
					ToString(createResult)));
				m_Enabled.store(false, std::memory_order_relaxed);
				return;
			}
			const std::string debugName =
				std::format("VulkanGpuProfiler.FrameSlot{}", frameSlotIndex);
			SetVulkanObjectDebugName(device->Get(), VK_OBJECT_TYPE_QUERY_POOL,
				reinterpret_cast<uint64_t>(frame.m_QueryPool), debugName.c_str());
			frame.m_Timestamps.resize(MaxTimestampCount);
			frame.m_Scopes.reserve(MaxScopeCount);
			frame.m_ScopeStack.reserve(MaxScopeCount);
		}
		m_Available = true;
	}

	VulkanGpuProfiler::~VulkanGpuProfiler()
	{
		if (!m_Device)
		{
			return;
		}
		for (FrameResource& frame : m_Frames)
		{
			if (frame.m_QueryPool != VK_NULL_HANDLE)
			{
				vkDestroyQueryPool(m_Device->Get(), frame.m_QueryPool, nullptr);
				frame.m_QueryPool = VK_NULL_HANDLE;
			}
		}
	}

	void VulkanGpuProfiler::RequestEnabled(bool enabled) noexcept
	{
		m_Enabled.store(enabled && m_Available, std::memory_order_relaxed);
	}

	bool VulkanGpuProfiler::IsEnabled() const noexcept
	{
		return m_Available && m_Enabled.load(std::memory_order_relaxed);
	}

	GpuProfileFrameSnapshot VulkanGpuProfiler::GetLatestFrame() const
	{
		std::scoped_lock lock(m_SnapshotMutex);
		return m_LatestFrame;
	}

	void VulkanGpuProfiler::BeginFrame(
		uint32_t frameSlotIndex, VkCommandBuffer commandBuffer) noexcept
	{
		GGLAB_ASSERT_MSG(m_ActiveFrame == nullptr,
			"VulkanGpuProfiler only supports one active frame.");
		GGLAB_ASSERT(frameSlotIndex < m_Frames.size());
		if (m_ActiveFrame || frameSlotIndex >= m_Frames.size() ||
			commandBuffer == VK_NULL_HANDLE)
		{
			return;
		}

		FrameResource& frame = m_Frames[frameSlotIndex];
		ResolveCompletedFrame(frame);
		++m_NextFrameIndex;
		if (!IsEnabled())
		{
			return;
		}

		frame.m_FrameIndex = m_NextFrameIndex;
		frame.m_TimestampCount = 0;
		frame.m_FrameBeginQuery = InvalidQueryIndex;
		frame.m_FrameEndQuery = InvalidQueryIndex;
		frame.m_Scopes.clear();
		frame.m_ScopeStack.clear();
		frame.m_PendingResults = false;
		frame.m_Recording = true;
		m_ActiveFrame = &frame;
		m_ActiveFrameSlot = frameSlotIndex;
		vkCmdResetQueryPool(commandBuffer, frame.m_QueryPool, 0, MaxTimestampCount);
		frame.m_FrameBeginQuery = WriteTimestamp(frame, commandBuffer);
	}

	void VulkanGpuProfiler::EndFrame(
		uint32_t frameSlotIndex, VkCommandBuffer commandBuffer) noexcept
	{
		if (!m_ActiveFrame)
		{
			return;
		}
		GGLAB_ASSERT_MSG(frameSlotIndex == m_ActiveFrameSlot,
			"VulkanGpuProfiler::EndFrame received the wrong frame slot.");
		if (frameSlotIndex != m_ActiveFrameSlot || commandBuffer == VK_NULL_HANDLE)
		{
			return;
		}

		FrameResource& frame = *m_ActiveFrame;
		while (!frame.m_ScopeStack.empty())
		{
			EndScope(commandBuffer);
		}
		frame.m_FrameEndQuery = WriteTimestamp(frame, commandBuffer);
		frame.m_PendingResults = frame.m_TimestampCount > 0;
		frame.m_Recording = false;
		m_ActiveFrame = nullptr;
	}

	void VulkanGpuProfiler::AbortFrame(uint32_t frameSlotIndex) noexcept
	{
		if (frameSlotIndex >= m_Frames.size())
		{
			return;
		}
		FrameResource& frame = m_Frames[frameSlotIndex];
		if (m_ActiveFrame)
		{
			GGLAB_ASSERT_MSG(frameSlotIndex == m_ActiveFrameSlot,
				"VulkanGpuProfiler::AbortFrame received the wrong frame slot.");
			if (frameSlotIndex != m_ActiveFrameSlot)
			{
				return;
			}
			m_ActiveFrame = nullptr;
		}
		frame.m_Recording = false;
		frame.m_PendingResults = false;
		frame.m_ScopeStack.clear();
	}

	void VulkanGpuProfiler::BeginScope(
		VkCommandBuffer commandBuffer, std::string_view name) noexcept
	{
		if (!m_ActiveFrame || !m_ActiveFrame->m_Recording || commandBuffer == VK_NULL_HANDLE)
		{
			return;
		}

		FrameResource& frame = *m_ActiveFrame;
		if (frame.m_Scopes.size() >= MaxScopeCount ||
			frame.m_TimestampCount + 2 > MaxTimestampCount)
		{
			frame.m_ScopeStack.push_back(-1);
			return;
		}

		ScopeRecord scope{};
		scope.m_Name = name.empty() ? "Unnamed Pass" : std::string(name);
		scope.m_BeginQuery = WriteTimestamp(frame, commandBuffer);
		frame.m_Scopes.push_back(std::move(scope));
		frame.m_ScopeStack.push_back(static_cast<int32_t>(frame.m_Scopes.size() - 1));
	}

	void VulkanGpuProfiler::EndScope(VkCommandBuffer commandBuffer) noexcept
	{
		if (!m_ActiveFrame || !m_ActiveFrame->m_Recording || commandBuffer == VK_NULL_HANDLE)
		{
			return;
		}

		FrameResource& frame = *m_ActiveFrame;
		GGLAB_ASSERT_MSG(!frame.m_ScopeStack.empty(),
			"VulkanGpuProfiler::EndScope called without a matching BeginScope.");
		if (frame.m_ScopeStack.empty())
		{
			return;
		}

		const int32_t scopeIndex = frame.m_ScopeStack.back();
		frame.m_ScopeStack.pop_back();
		if (scopeIndex >= 0)
		{
			frame.m_Scopes[static_cast<size_t>(scopeIndex)].m_EndQuery =
				WriteTimestamp(frame, commandBuffer);
		}
	}

	void VulkanGpuProfiler::ResolveCompletedFrame(FrameResource& frame) noexcept
	{
		if (!frame.m_PendingResults || frame.m_TimestampCount == 0)
		{
			return;
		}

		const VkResult result = vkGetQueryPoolResults(m_Device->Get(), frame.m_QueryPool, 0,
			frame.m_TimestampCount, frame.m_TimestampCount * sizeof(uint64_t),
			frame.m_Timestamps.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
		if (result == VK_NOT_READY)
		{
			GGLAB_LOG_GRAPHICS_WARN_ALWAYS(
				"Vulkan GPU timestamp results were not ready after frame-slot reuse synchronization.");
			frame.m_PendingResults = false;
			return;
		}
		if (result != VK_SUCCESS)
		{
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(std::format(
				"Vulkan GPU timestamp readback failed: {}.", ToString(result)));
			frame.m_PendingResults = false;
			m_Available = false;
			m_Enabled.store(false, std::memory_order_relaxed);
			return;
		}

		GpuProfileFrameSnapshot snapshot{};
		snapshot.m_FrameIndex = frame.m_FrameIndex;
		const auto milliseconds = [this](uint64_t begin, uint64_t end) noexcept
			{
				return static_cast<double>(VulkanTimestampDelta(
					begin, end, m_TimestampValidBits)) * m_TimestampPeriodNanoseconds / 1'000'000.0;
			};
		if (frame.m_FrameBeginQuery != InvalidQueryIndex &&
			frame.m_FrameEndQuery != InvalidQueryIndex)
		{
			snapshot.m_FrameMilliseconds = milliseconds(
				frame.m_Timestamps[frame.m_FrameBeginQuery],
				frame.m_Timestamps[frame.m_FrameEndQuery]);
		}

		for (const ScopeRecord& scope : frame.m_Scopes)
		{
			if (scope.m_BeginQuery == InvalidQueryIndex || scope.m_EndQuery == InvalidQueryIndex)
			{
				continue;
			}
			const double elapsed = milliseconds(
				frame.m_Timestamps[scope.m_BeginQuery], frame.m_Timestamps[scope.m_EndQuery]);
			auto sample = std::ranges::find_if(snapshot.m_Samples,
				[&scope](const GpuProfileSample& candidate)
				{ return candidate.m_Name == scope.m_Name; });
			if (sample == snapshot.m_Samples.end())
			{
				snapshot.m_Samples.push_back({ .m_Name = scope.m_Name });
				sample = std::prev(snapshot.m_Samples.end());
			}
			sample->m_Milliseconds += elapsed;
			++sample->m_CallCount;
		}

		frame.m_PendingResults = false;
		std::scoped_lock lock(m_SnapshotMutex);
		m_LatestFrame = std::move(snapshot);
	}

	uint32_t VulkanGpuProfiler::WriteTimestamp(
		FrameResource& frame, VkCommandBuffer commandBuffer) noexcept
	{
		GGLAB_ASSERT(frame.m_TimestampCount < MaxTimestampCount);
		if (frame.m_TimestampCount >= MaxTimestampCount)
		{
			return InvalidQueryIndex;
		}
		const uint32_t queryIndex = frame.m_TimestampCount++;
		vkCmdWriteTimestamp2(
			commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.m_QueryPool, queryIndex);
		return queryIndex;
	}
}
