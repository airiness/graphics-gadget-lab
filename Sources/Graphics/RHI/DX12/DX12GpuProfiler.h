#pragma once
#include "Core/CoreMacros.h"
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/Profiling/GpuProfiler.h"

#include <atomic>
#include <limits>
#include <mutex>

namespace gglab
{
	class DX12CommandList;
	class DX12CommandQueue;
	class DX12Device;

	class DX12GpuProfiler final : public GpuProfiler
	{
	public:
		DX12GpuProfiler(DX12Device* device,
			DX12CommandQueue* graphicsQueue,
			uint32_t frameCount) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12GpuProfiler);
		~DX12GpuProfiler() override = default;

		void SetEnabled(bool enabled) noexcept override;
		[[nodiscard]] bool IsEnabled() const noexcept override;
		[[nodiscard]] GpuProfileFrameSnapshot GetLatestFrame() const override;

		void BeginFrame(uint32_t frameSlot, DX12CommandList& commandList) noexcept;
		void EndFrame(uint32_t frameSlot, DX12CommandList& commandList) noexcept;
		void AbortFrame(uint32_t frameSlot) noexcept;
		void BeginScope(DX12CommandList& commandList, std::string_view name) noexcept;
		void EndScope(DX12CommandList& commandList) noexcept;

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
			ComPtr<ID3D12QueryHeap> m_QueryHeap;
			ComPtr<ID3D12Resource> m_ReadbackBuffer;
			uint64_t m_FrameIndex = 0;
			uint32_t m_TimestampCount = 0;
			uint32_t m_FrameBeginQuery = InvalidQueryIndex;
			uint32_t m_FrameEndQuery = InvalidQueryIndex;
			std::vector<ScopeRecord> m_Scopes;
			std::vector<int32_t> m_ScopeStack;
			bool m_PendingResults = false;
			bool m_Recording = false;
		};

		void InitializeFrameResources(DX12Device& device, uint32_t frameCount) noexcept;
		void ResolveCompletedFrame(FrameResource& frame) noexcept;
		uint32_t WriteTimestamp(FrameResource& frame, DX12CommandList& commandList) noexcept;

	private:
		mutable std::mutex m_SnapshotMutex;
		std::atomic_bool m_Enabled = true;
		std::vector<FrameResource> m_Frames;
		GpuProfileFrameSnapshot m_LatestFrame;
		FrameResource* m_ActiveFrame = nullptr;
		uint32_t m_ActiveFrameSlot = 0;
		uint64_t m_NextFrameIndex = 0;
		uint64_t m_TimestampFrequency = 0;
	};
}
