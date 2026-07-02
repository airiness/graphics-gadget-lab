#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12GpuProfiler.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Core/HResult.h"

namespace gglab
{
	DX12GpuProfiler::DX12GpuProfiler(DX12Device* device,
		DX12CommandQueue* graphicsQueue,
		uint32_t frameCount) noexcept :
		m_Device(device)
	{
		GGLAB_ASSERT_NOT_NULL(device);
		GGLAB_ASSERT_NOT_NULL(graphicsQueue);
		GGLAB_ASSERT_MSG(frameCount > 0, "DX12GpuProfiler requires at least one frame slot.");
		if (!device || !graphicsQueue || frameCount == 0)
		{
			m_Enabled.store(false, std::memory_order_relaxed);
			return;
		}

		GGLAB_HR_DX(graphicsQueue->Get()->GetTimestampFrequency(&m_TimestampFrequency), device->Get());
		InitializeFrameResources(*device, frameCount);
	}

	DX12GpuProfiler::~DX12GpuProfiler()
	{
		if (!m_Device)
		{
			return;
		}

		for (auto& frame : m_Frames)
		{
			if (frame.m_ReadbackBuffer && frame.m_MappedTimestamps)
			{
				m_Device->UnmapBuffer(frame.m_ReadbackBuffer.Get());
				frame.m_MappedTimestamps = nullptr;
			}
		}
	}

	void DX12GpuProfiler::SetEnabled(bool enabled) noexcept
	{
		m_Enabled.store(enabled, std::memory_order_relaxed);
	}

	bool DX12GpuProfiler::IsEnabled() const noexcept
	{
		return m_Enabled.load(std::memory_order_relaxed);
	}

	GpuProfileFrameSnapshot DX12GpuProfiler::GetLatestFrame() const
	{
		std::scoped_lock lock(m_SnapshotMutex);
		return m_LatestFrame;
	}

	void DX12GpuProfiler::BeginFrame(uint32_t frameSlot, DX12CommandList& commandList) noexcept
	{
		GGLAB_ASSERT_MSG(m_ActiveFrame == nullptr, "DX12GpuProfiler only supports one active frame.");
		GGLAB_ASSERT(frameSlot < m_Frames.size());
		if (m_ActiveFrame || frameSlot >= m_Frames.size())
		{
			return;
		}

		auto& frame = m_Frames[frameSlot];
		ResolveCompletedFrame(frame);
		++m_NextFrameIndex;
		if (!IsEnabled() || m_TimestampFrequency == 0)
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
		m_ActiveFrameSlot = frameSlot;
		frame.m_FrameBeginQuery = WriteTimestamp(frame, commandList);
	}

	void DX12GpuProfiler::EndFrame(uint32_t frameSlot, DX12CommandList& commandList) noexcept
	{
		if (!m_ActiveFrame)
		{
			return;
		}
		GGLAB_ASSERT_MSG(frameSlot == m_ActiveFrameSlot,
			"DX12GpuProfiler::EndFrame received the wrong frame slot.");
		if (frameSlot != m_ActiveFrameSlot)
		{
			return;
		}

		auto& frame = *m_ActiveFrame;
		while (!frame.m_ScopeStack.empty())
		{
			EndScope(commandList);
		}
		frame.m_FrameEndQuery = WriteTimestamp(frame, commandList);
		if (frame.m_TimestampCount > 0)
		{
			DX12Buffer* readbackBuffer = m_Device->ResolveBuffer(frame.m_ReadbackBuffer.Get());
			GGLAB_ASSERT_NOT_NULL(readbackBuffer);
			if (!readbackBuffer)
			{
				frame.m_Recording = false;
				m_ActiveFrame = nullptr;
				return;
			}
			commandList.Get()->ResolveQueryData(
				frame.m_QueryHeap.Get(),
				D3D12_QUERY_TYPE_TIMESTAMP,
				0,
				frame.m_TimestampCount,
				readbackBuffer->Get(),
				0);
			frame.m_PendingResults = true;
		}
		frame.m_Recording = false;
		m_ActiveFrame = nullptr;
	}

	void DX12GpuProfiler::AbortFrame(uint32_t frameSlot) noexcept
	{
		if (!m_ActiveFrame)
		{
			return;
		}
		GGLAB_ASSERT_MSG(frameSlot == m_ActiveFrameSlot,
			"DX12GpuProfiler::AbortFrame received the wrong frame slot.");
		if (frameSlot != m_ActiveFrameSlot)
		{
			return;
		}

		m_ActiveFrame->m_Recording = false;
		m_ActiveFrame->m_PendingResults = false;
		m_ActiveFrame->m_ScopeStack.clear();
		m_ActiveFrame = nullptr;
	}

	void DX12GpuProfiler::BeginScope(DX12CommandList& commandList, std::string_view name) noexcept
	{
		if (!m_ActiveFrame || !m_ActiveFrame->m_Recording)
		{
			return;
		}

		auto& frame = *m_ActiveFrame;
		if (frame.m_Scopes.size() >= MaxScopeCount ||
			frame.m_TimestampCount + 2 > MaxTimestampCount)
		{
			frame.m_ScopeStack.push_back(-1);
			return;
		}

		ScopeRecord scope{};
		scope.m_Name = name.empty() ? "Unnamed Pass" : std::string(name);
		scope.m_BeginQuery = WriteTimestamp(frame, commandList);
		frame.m_Scopes.push_back(std::move(scope));
		frame.m_ScopeStack.push_back(static_cast<int32_t>(frame.m_Scopes.size() - 1));
	}

	void DX12GpuProfiler::EndScope(DX12CommandList& commandList) noexcept
	{
		if (!m_ActiveFrame || !m_ActiveFrame->m_Recording)
		{
			return;
		}

		auto& frame = *m_ActiveFrame;
		GGLAB_ASSERT_MSG(!frame.m_ScopeStack.empty(),
			"DX12GpuProfiler::EndScope called without a matching BeginScope.");
		if (frame.m_ScopeStack.empty())
		{
			return;
		}

		const int32_t scopeIndex = frame.m_ScopeStack.back();
		frame.m_ScopeStack.pop_back();
		if (scopeIndex >= 0)
		{
			frame.m_Scopes[static_cast<size_t>(scopeIndex)].m_EndQuery =
				WriteTimestamp(frame, commandList);
		}
	}

	void DX12GpuProfiler::InitializeFrameResources(DX12Device& device, uint32_t frameCount) noexcept
	{
		m_Frames.resize(frameCount);
		for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
		{
			auto& frame = m_Frames[frameIndex];
			D3D12_QUERY_HEAP_DESC queryHeapDesc{};
			queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			queryHeapDesc.Count = MaxTimestampCount;
			GGLAB_HR_DX(device.Get()->CreateQueryHeap(
				&queryHeapDesc,
				IID_PPV_ARGS(&frame.m_QueryHeap)), device.Get());

			const std::string debugName = std::format(
				"GpuProfiler.TimestampReadback[{}]",
				frameIndex);
			RHIBufferDesc readbackDesc{};
			readbackDesc.m_SizeInBytes =
				static_cast<uint64_t>(MaxTimestampCount) * sizeof(uint64_t);
			readbackDesc.m_Usage = RHIBufferUsage::CopyDest;
			readbackDesc.m_MemoryUsage = RHIMemoryUsage::GpuToCpu;
			readbackDesc.m_DebugName = debugName.c_str();
			const RHIBufferHandle readbackBuffer = device.CreateBuffer(readbackDesc);
			GGLAB_ASSERT_MSG(readbackBuffer.IsValid(),
				"DX12GpuProfiler failed to create a timestamp readback buffer.");
			if (!readbackBuffer.IsValid())
			{
				m_Enabled.store(false, std::memory_order_relaxed);
				continue;
			}
			frame.m_ReadbackBuffer = RHIBufferOwner(&device, readbackBuffer);
			frame.m_MappedTimestamps =
				static_cast<const uint64_t*>(device.MapBuffer(readbackBuffer));
			GGLAB_ASSERT_MSG(frame.m_MappedTimestamps != nullptr,
				"DX12GpuProfiler failed to map a timestamp readback buffer.");
			if (!frame.m_MappedTimestamps)
			{
				m_Enabled.store(false, std::memory_order_relaxed);
			}

			frame.m_Scopes.reserve(MaxScopeCount);
			frame.m_ScopeStack.reserve(MaxScopeCount);
		}
	}

	void DX12GpuProfiler::ResolveCompletedFrame(FrameResource& frame) noexcept
	{
		if (!frame.m_PendingResults || frame.m_TimestampCount == 0)
		{
			return;
		}

		const uint64_t* timestamps = frame.m_MappedTimestamps;
		GGLAB_ASSERT_NOT_NULL(timestamps);
		if (!timestamps)
		{
			frame.m_PendingResults = false;
			return;
		}

		GpuProfileFrameSnapshot snapshot{};
		snapshot.m_FrameIndex = frame.m_FrameIndex;
		if (frame.m_FrameBeginQuery != InvalidQueryIndex &&
			frame.m_FrameEndQuery != InvalidQueryIndex)
		{
			const uint64_t begin = timestamps[frame.m_FrameBeginQuery];
			const uint64_t end = timestamps[frame.m_FrameEndQuery];
			if (end >= begin)
			{
				snapshot.m_FrameMilliseconds =
					static_cast<double>(end - begin) * 1000.0 /
					static_cast<double>(m_TimestampFrequency);
			}
		}

		for (const auto& scope : frame.m_Scopes)
		{
			if (scope.m_BeginQuery == InvalidQueryIndex || scope.m_EndQuery == InvalidQueryIndex)
			{
				continue;
			}
			const uint64_t begin = timestamps[scope.m_BeginQuery];
			const uint64_t end = timestamps[scope.m_EndQuery];
			if (end < begin)
			{
				continue;
			}

			const double milliseconds =
				static_cast<double>(end - begin) * 1000.0 /
				static_cast<double>(m_TimestampFrequency);
			auto sampleIter = std::ranges::find_if(snapshot.m_Samples,
				[&scope](const GpuProfileSample& sample)
				{
					return sample.m_Name == scope.m_Name;
				});
			if (sampleIter == snapshot.m_Samples.end())
			{
				snapshot.m_Samples.push_back({ .m_Name = scope.m_Name });
				sampleIter = std::prev(snapshot.m_Samples.end());
			}
			sampleIter->m_Milliseconds += milliseconds;
			++sampleIter->m_CallCount;
		}

		frame.m_PendingResults = false;

		std::scoped_lock lock(m_SnapshotMutex);
		m_LatestFrame = std::move(snapshot);
	}

	uint32_t DX12GpuProfiler::WriteTimestamp(
		FrameResource& frame,
		DX12CommandList& commandList) noexcept
	{
		GGLAB_ASSERT(frame.m_TimestampCount < MaxTimestampCount);
		if (frame.m_TimestampCount >= MaxTimestampCount)
		{
			return InvalidQueryIndex;
		}

		const uint32_t queryIndex = frame.m_TimestampCount++;
		commandList.Get()->EndQuery(
			frame.m_QueryHeap.Get(),
			D3D12_QUERY_TYPE_TIMESTAMP,
			queryIndex);
		return queryIndex;
	}
}
