#pragma once

#include "Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

namespace gglab
{
	class RHIDevice;

	struct ForwardPlusTileReadback
	{
		uint64_t m_FrameSerial = 0;
		uint64_t m_RequestGeneration = 0;
		ForwardPlusTileGrid m_TileGrid{};
		uint32_t m_TileX = 0;
		uint32_t m_TileY = 0;
		ForwardPlusTileHeader m_Header{};
		std::array<uint32_t, ForwardPlusTileLightCapacity> m_LightIndices{};
		bool m_IsValid = false;
	};

	struct ForwardPlusHdrDiffReadback
	{
		uint64_t m_FrameSerial = 0;
		uint64_t m_RequestGeneration = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		float m_MaxAbsoluteError = 0.0f;
		float m_MaxRelativeLuminanceError = 0.0f;
		uint32_t m_MaxErrorPixelX = 0;
		uint32_t m_MaxErrorPixelY = 0;
		uint32_t m_ComparedPixelCount = 0;
		bool m_IsValid = false;
	};

	struct ForwardPlusGridReadback
	{
		uint64_t m_FrameSerial = 0;
		uint64_t m_RequestGeneration = 0;
		ForwardPlusTileGrid m_TileGrid{};
		std::vector<ForwardPlusTileHeader> m_Headers;
		std::vector<ForwardPlusTileDepthRange> m_DepthRanges;
		bool m_IsValid = false;
	};

	struct ForwardPlusPerformanceReadback
	{
		uint64_t m_LegacyFrameSerial = 0;
		uint64_t m_ForwardPlusFrameSerial = 0;
		double m_LegacyOpaqueMilliseconds = 0.0;
		double m_ForwardPlusCullMilliseconds = 0.0;
		double m_ForwardPlusOpaqueMilliseconds = 0.0;
		bool m_HasLegacySample = false;
		bool m_HasForwardPlusSample = false;
	};

	inline constexpr float ForwardPlusHdrDiffAbsoluteTolerance = 1.0e-3f;
	inline constexpr float ForwardPlusHdrDiffRelativeLuminanceTolerance = 2.0e-3f;

	[[nodiscard]] constexpr bool IsForwardPlusReadbackGenerationCurrent(
		uint64_t resultGeneration, uint64_t currentGeneration) noexcept
	{
		return resultGeneration != 0 && resultGeneration == currentGeneration;
	}

	[[nodiscard]] constexpr bool ShouldPublishForwardPlusReadback(
		uint64_t candidateGeneration, uint64_t candidateFrameSerial,
		uint64_t currentGeneration, uint64_t publishedGeneration,
		uint64_t publishedFrameSerial) noexcept
	{
		return IsForwardPlusReadbackGenerationCurrent(candidateGeneration, currentGeneration) &&
			(candidateGeneration != publishedGeneration ||
				candidateFrameSerial >= publishedFrameSerial);
	}

	[[nodiscard]] constexpr bool IsForwardPlusHdrDiffWithinTolerance(
		const ForwardPlusHdrDiffReadback& result) noexcept
	{
		return result.m_IsValid && result.m_ComparedPixelCount > 0 &&
			std::isfinite(result.m_MaxAbsoluteError) &&
			std::isfinite(result.m_MaxRelativeLuminanceError) &&
			result.m_MaxAbsoluteError <= ForwardPlusHdrDiffAbsoluteTolerance &&
			result.m_MaxRelativeLuminanceError <=
			ForwardPlusHdrDiffRelativeLuminanceTolerance;
	}

	class ForwardPlusDebugReadback final
	{
	public:
		ForwardPlusDebugReadback() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(ForwardPlusDebugReadback);
		~ForwardPlusDebugReadback();

		void Initialize(RHIDevice& device, uint32_t bufferCount) noexcept;
		void PrepareGridBuffer(RHIDevice& device, uint32_t bufferIndex,
			const ForwardPlusTileGrid& tileGrid) noexcept;
		void InvalidateResults() noexcept;
		void ResetPerformance() noexcept;
		void SetSelectedTile(uint32_t tileX, uint32_t tileY) noexcept;
		void ConsumeCompletedSlot(uint32_t bufferIndex) noexcept;
		void MarkScheduled(uint32_t bufferIndex, uint64_t frameSerial,
			const ForwardPlusTileGrid& tileGrid, uint32_t tileX, uint32_t tileY) noexcept;
		void MarkHdrDiffScheduled(uint32_t bufferIndex, uint64_t frameSerial, uint32_t width,
			uint32_t height) noexcept;
		void MarkGridScheduled(uint32_t bufferIndex, uint64_t frameSerial,
			const ForwardPlusTileGrid& tileGrid) noexcept;
		void RecordLegacyGpuTiming(uint64_t frameSerial, double opaqueMilliseconds) noexcept;
		void RecordForwardPlusGpuTiming(uint64_t frameSerial, double cullMilliseconds,
			double opaqueMilliseconds) noexcept;

		[[nodiscard]] RHIBufferHandle GetBuffer(uint32_t bufferIndex) const noexcept;
		[[nodiscard]] RHIBufferHandle GetHdrDiffBuffer(uint32_t bufferIndex) const noexcept;
		[[nodiscard]] RHIBufferHandle GetGridBuffer(uint32_t bufferIndex) const noexcept;
		[[nodiscard]] uint32_t GetSelectedTileX() const noexcept;
		[[nodiscard]] uint32_t GetSelectedTileY() const noexcept;
		[[nodiscard]] ForwardPlusTileReadback GetLatest() const noexcept;
		[[nodiscard]] ForwardPlusHdrDiffReadback GetLatestHdrDiff() const noexcept;
		[[nodiscard]] std::shared_ptr<const ForwardPlusGridReadback> GetLatestGrid() const noexcept;
		[[nodiscard]] ForwardPlusPerformanceReadback GetPerformance() const noexcept;
		[[nodiscard]] uint64_t GetCurrentGeneration() const noexcept
		{
			return m_RequestGeneration.load(std::memory_order_acquire);
		}
		[[nodiscard]] uint64_t GetScheduledCount() const noexcept
		{
			return m_ScheduledCount.load(std::memory_order_relaxed);
		}

		static constexpr uint64_t HeaderReadbackOffset = 0;
		static constexpr uint64_t IndicesReadbackOffset = sizeof(ForwardPlusTileHeader);
		static constexpr uint64_t ReadbackSizeInBytes =
			IndicesReadbackOffset + sizeof(uint32_t) * ForwardPlusTileLightCapacity;
		static constexpr uint64_t HdrDiffReadbackSizeInBytes = sizeof(uint32_t) * 4;
		[[nodiscard]] static constexpr uint64_t GetGridHeadersSizeInBytes(
			uint32_t tileCount) noexcept
		{
			return static_cast<uint64_t>(tileCount) * sizeof(ForwardPlusTileHeader);
		}
		[[nodiscard]] static constexpr uint64_t GetGridDepthRangesOffset(
			uint32_t tileCount) noexcept
		{
			return GetGridHeadersSizeInBytes(tileCount);
		}
		[[nodiscard]] static constexpr uint64_t GetGridReadbackSizeInBytes(
			uint32_t tileCount) noexcept
		{
			return GetGridDepthRangesOffset(tileCount) +
				static_cast<uint64_t>(tileCount) * sizeof(ForwardPlusTileDepthRange);
		}

	private:
		struct PendingSlot
		{
			uint64_t m_FrameSerial = 0;
			uint64_t m_RequestGeneration = 0;
			ForwardPlusTileGrid m_TileGrid{};
			uint32_t m_TileX = 0;
			uint32_t m_TileY = 0;
			bool m_Pending = false;
		};

		struct PendingHdrDiffSlot
		{
			uint64_t m_FrameSerial = 0;
			uint64_t m_RequestGeneration = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			bool m_Pending = false;
		};

		struct PendingGridSlot
		{
			uint64_t m_FrameSerial = 0;
			uint64_t m_RequestGeneration = 0;
			ForwardPlusTileGrid m_TileGrid{};
			bool m_Pending = false;
		};

		void ReleaseBuffers() noexcept;

		RHIDevice* m_Device = nullptr;
		std::vector<RHIBufferHandle> m_Buffers;
		std::vector<RHIBufferHandle> m_HdrDiffBuffers;
		std::vector<RHIBufferHandle> m_GridBuffers;
		std::vector<uint64_t> m_GridBufferSizes;
		std::vector<PendingSlot> m_PendingSlots;
		std::vector<PendingHdrDiffSlot> m_PendingHdrDiffSlots;
		std::vector<PendingGridSlot> m_PendingGridSlots;
		std::atomic<uint32_t> m_SelectedTileX = 0;
		std::atomic<uint32_t> m_SelectedTileY = 0;
		std::atomic<uint64_t> m_ScheduledCount = 0;
		std::atomic<uint64_t> m_RequestGeneration = 1;
		mutable std::mutex m_ResultMutex;
		ForwardPlusTileReadback m_Latest{};
		ForwardPlusHdrDiffReadback m_LatestHdrDiff{};
		std::shared_ptr<const ForwardPlusGridReadback> m_LatestGrid;
		ForwardPlusPerformanceReadback m_Performance{};
		bool m_LoggedFirstResult = false;
		bool m_LoggedFirstHdrDiff = false;
	};
}
