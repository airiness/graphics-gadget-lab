#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "Graphics/Pipeline/GIComputeDispatch.h"
#include "Graphics/Pipeline/GIReceiverCache.h"
#include "Graphics/Pipeline/TemporalHistoryTextures.h"
#include "Graphics/RenderFrameOutcome.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gglab
{
	// Domain wrappers convert their sample/query limits to pixel-sample work.
	// Receiver preparation retains its own independent query-work admission.
	struct GIObservationWorkBudget
	{
		uint64_t m_PixelSampleWork = 0;
		uint32_t m_SamplesPerBatch = 0;
		uint32_t m_MaximumTilePixels = 0;
		uint32_t m_ReceiverQueryWork = 0;
	};

	// One instance per domain observation. This is the sole owner of mutable sweep,
	// history and submission state; wrappers retain only typed identity and metadata.
	template <size_t SurfaceCount>
	class GITiledObservation final
	{
	public:
		using Surface = typename TemporalHistoryTextures<SurfaceCount>::RenderGraphSurface;
		using SurfaceDesc = typename TemporalHistoryTextures<SurfaceCount>::SurfaceDesc;
		using Tile = GIObservationTile;

		GITiledObservation(RHIDevice& device, PersistentTexturePool& pool,
			std::array<std::string_view, GIReceiverCache::SurfaceCount> receiverNames) noexcept :
			m_Device(device), m_Pool(pool), m_ReceiverCache(device, pool, receiverNames) {}
		~GITiledObservation()
		{
			GGLAB_ASSERT_MSG(!m_Textures.IsAllocated(), "GI observation requires checked renderer teardown.");
		}

		void BeginFrame(uint64_t serial) noexcept
		{
			m_FrameSerial = serial;
			m_ReceiverCache.BeginFrame(serial);
			m_Settled = false;
			m_Visited = false;
			m_Exported = false;
			m_Recorded = false;
			m_Progress.m_BatchSamples = 0;
			m_Tile = {};
			if (m_PendingFence.IsValid())
			{
				const auto query = m_Device.QueryFencePoint(m_PendingFence);
				if (query == RHIFenceQueryResult::Completed)
				{
					m_Progress.m_CompletedSamples = m_Progress.m_SubmittedSamples;
					m_PendingFence.Reset();
					if (m_Progress.m_Status == Status::Running &&
						m_Progress.m_CompletedSamples == m_Progress.m_TargetSamples)
					{
						m_Progress.m_Status = Status::Completed;
					}
				}
				else if (query != RHIFenceQueryResult::Pending)
				{
					m_Progress.m_Status = Status::Failed;
				}
			}
		}

		[[nodiscard]] bool ValidatePreparation(uint32_t width, uint32_t height,
			uint32_t samples, const GIObservationWorkBudget& budget) noexcept
		{
			m_Visited = true;
			const uint64_t pixels = uint64_t(width) * height;
			return !m_UnknownCompletion && pixels != 0 &&
				pixels <= GIObservationLimits::MaximumImagePixels && budget.m_PixelSampleWork != 0 &&
				samples != 0 && budget.m_SamplesPerBatch != 0 &&
				budget.m_MaximumTilePixels <= GIObservationLimits::MaximumImagePixels &&
				budget.m_ReceiverQueryWork >= GIObservationLimits::MaximumReceiverQueriesPerPixel &&
				budget.m_ReceiverQueryWork <= GIObservationLimits::MaximumReceiverQueryWorkPerSubmission;
		}

		[[nodiscard]] bool IsAllocated() const noexcept { return m_Textures.IsAllocated(); }
		[[nodiscard]] bool Reset(uint32_t width, uint32_t height, uint32_t samples,
			const std::array<SurfaceDesc, SurfaceCount>& descriptions) noexcept
		{
			GGLAB_ASSERT(!m_UnknownCompletion);
			GGLAB_UNUSED(Cancel());
			m_Width = width;
			m_Height = height;
			m_Progress = { .m_Status = Status::Running,
				.m_Generation = m_Progress.m_Generation + 1, .m_TargetSamples = samples };
			if (!m_Textures.Acquire(m_Pool, descriptions))
			{
				m_Progress.m_Status = Status::Failed;
				return false;
			}
			return true;
		}

		[[nodiscard]] bool Prepare(const GIObservationWorkBudget& budget) noexcept
		{
			if (m_Progress.m_Status == Status::Failed ||
				!m_ReceiverCache.Prepare(m_Width, m_Height, budget.m_ReceiverQueryWork))
			{
				return false;
			}
			if (!m_ReceiverCache.CanSample())
			{
				return true;
			}
			if (!m_PendingFence.IsValid() && m_Progress.m_Status == Status::Running)
			{
				if (!m_SweepActive)
				{
					const uint64_t pixels = uint64_t(m_Width) * m_Height;
					const auto wholeImageSamples = std::min({ budget.m_SamplesPerBatch,
						static_cast<uint32_t>(budget.m_PixelSampleWork / pixels),
						m_Progress.m_TargetSamples - m_Progress.m_SubmittedSamples });
					m_Progress.m_SweepSamples = std::max(1u, wholeImageSamples);
					uint64_t tilePixels = budget.m_PixelSampleWork / m_Progress.m_SweepSamples;
					if (budget.m_MaximumTilePixels != 0)
					{
						tilePixels = std::min(tilePixels, uint64_t(budget.m_MaximumTilePixels));
					}
					m_TileWidth = static_cast<uint32_t>(std::min(tilePixels, uint64_t(m_Width)));
					if (m_TileWidth < m_Width && m_TileWidth >= GIQueryGroupWidth)
					{
						m_TileWidth = m_TileWidth / GIQueryGroupWidth * GIQueryGroupWidth;
					}
					m_TileHeight = static_cast<uint32_t>(std::min(tilePixels / m_TileWidth,
						uint64_t(m_Height)));
					if (m_TileHeight < m_Height && m_TileHeight >= GIQueryGroupHeight)
					{
						m_TileHeight = m_TileHeight / GIQueryGroupHeight * GIQueryGroupHeight;
					}
					m_Progress.m_TileCount = ((m_Width + m_TileWidth - 1) / m_TileWidth) *
						((m_Height + m_TileHeight - 1) / m_TileHeight);
					m_Progress.m_SubmittedTiles = 0;
					m_SweepActive = true;
				}
				const uint32_t columns = (m_Width + m_TileWidth - 1) / m_TileWidth;
				const uint32_t x = (m_Progress.m_SubmittedTiles % columns) * m_TileWidth;
				const uint32_t y = (m_Progress.m_SubmittedTiles / columns) * m_TileHeight;
				const uint32_t width = std::min(m_TileWidth, m_Width - x);
				const uint32_t height = std::min(m_TileHeight, m_Height - y);
				const uint64_t work = uint64_t(width) * height * m_Progress.m_SweepSamples;
				// A changed execution budget cannot submit an oversized active tile.
				// Partition preferences apply only at the next sweep boundary.
				if (work > budget.m_PixelSampleWork)
				{
					return false;
				}
				m_Tile = { .m_X = x, .m_Y = y, .m_Width = width, .m_Height = height,
					.m_BeginSweep = m_Progress.m_SubmittedTiles == 0,
					.m_EndSweep = m_Progress.m_SubmittedTiles + 1 == m_Progress.m_TileCount };
				m_Progress.m_BatchSamples = m_Progress.m_SweepSamples;
			}
			return true;
		}

		[[nodiscard]] Tile GetTile() const noexcept { return m_Tile; }
		[[nodiscard]] GIReceiverCache& GetReceiverCache() noexcept { return m_ReceiverCache; }
		[[nodiscard]] std::array<Surface, SurfaceCount> Import(RenderGraph::RGBuilder& builder,
			const std::array<const char*, SurfaceCount>& previous,
			const std::array<const char*, SurfaceCount>& next) noexcept
		{
			std::array<Surface, SurfaceCount> surfaces{};
			for (size_t index = 0; index < surfaces.size(); ++index)
			{
				surfaces[index] = m_Textures.Import(builder, index, previous[index], next[index],
					m_Progress.m_SubmittedSamples != 0, m_PendingInitialized);
			}
			return surfaces;
		}

		void Export(RenderGraph::RGBuilder& builder,
			const std::array<Surface, SurfaceCount>& surfaces) noexcept
		{
			const bool writing = m_Progress.m_BatchSamples != 0;
			for (const auto& surface : surfaces)
			{
				if (m_Progress.m_SubmittedSamples != 0)
				{
					builder.Export(surface.m_Previous, RGTextureAccess::None);
				}
				if (writing || m_PendingInitialized)
				{
					if (writing)
					{
						GGLAB_ASSERT(TemporalHistoryTextures<SurfaceCount>::IsFullyWritten(builder, surface));
					}
					builder.Export(surface.m_Next, RGTextureAccess::None);
				}
			}
			m_Exported = true;
		}
		void MarkRecorded() noexcept
		{
			m_Recorded = true;
			m_ReceiverCache.MarkRecorded();
		}

		// A true result retires the wrapper's typed identity, not additional GPU state.
		[[nodiscard]] bool Settle(const RenderFrameOutcome& outcome) noexcept
		{
			GGLAB_ASSERT(outcome.m_FrameSerial == m_FrameSerial);
			if (m_Settled)
			{
				return false;
			}
			m_Settled = true;
			m_ReceiverCache.Settle(outcome);
			if (outcome.m_Kind == RenderFrameOutcomeKind::CompletionUnknown)
			{
				m_UnknownCompletion = true;
				m_Progress.m_Status = Status::Failed;
				return false;
			}
			if (m_Visited && m_Exported && outcome.m_Fence.IsValid())
			{
				m_LastUse = outcome.m_Fence;
			}
			if (outcome.m_Kind == RenderFrameOutcomeKind::Failed)
			{
				m_Progress.m_Status = Status::Failed;
				return false;
			}
			if (!m_Visited)
			{
				return Cancel();
			}
			if (outcome.m_Kind == RenderFrameOutcomeKind::Submitted && m_Exported && m_Recorded &&
				m_Progress.m_BatchSamples != 0)
			{
				GGLAB_ASSERT(outcome.m_Fence.IsValid());
				m_Textures.MarkWriteInitialized();
				m_PendingInitialized = true;
				++m_Progress.m_SubmittedTiles;
				if (m_Progress.m_SubmittedTiles == m_Progress.m_TileCount)
				{
					m_Textures.Commit();
					m_Progress.m_SubmittedSamples += m_Progress.m_SweepSamples;
					++m_Progress.m_CommittedVersion;
					m_SweepActive = false;
					m_PendingInitialized = false;
				}
				m_PendingFence = outcome.m_Fence;
			}
			return false;
		}

		[[nodiscard]] bool Cancel() noexcept
		{
			if (m_UnknownCompletion)
			{
				return false;
			}
			Release();
			if (m_Progress.m_Status != Status::Inactive)
			{
				m_Progress.m_Status = Status::Cancelled;
			}
			return true;
		}
		void Shutdown() noexcept
		{
			// Called only after Renderer verifies quiescence or confirms device loss.
			m_UnknownCompletion = false;
			m_ReceiverCache.Shutdown();
			Release();
		}

		// Public diagnostics are value projections, never a second mutable state owner.
		template <typename PublicProgress>
		[[nodiscard]] PublicProgress ProjectProgress() const noexcept
		{
			using PublicStatus = decltype(PublicProgress{}.m_Status);
			PublicStatus status = PublicStatus::Inactive;
			switch (m_Progress.m_Status)
			{
			case Status::Inactive:
				status = PublicStatus::Inactive;
				break;
			case Status::Running:
				status = PublicStatus::Running;
				break;
			case Status::Completed:
				status = PublicStatus::Completed;
				break;
			case Status::Cancelled:
				status = PublicStatus::Cancelled;
				break;
			case Status::Failed:
				status = PublicStatus::Failed;
				break;
			}
			return { .m_Status = status, .m_Generation = m_Progress.m_Generation,
				.m_CommittedVersion = m_Progress.m_CommittedVersion,
				.m_TargetSamples = m_Progress.m_TargetSamples,
				.m_SubmittedSamples = m_Progress.m_SubmittedSamples,
				.m_CompletedSamples = m_Progress.m_CompletedSamples,
				.m_BatchSamples = m_Progress.m_BatchSamples,
				.m_SweepSamples = m_Progress.m_SweepSamples,
				.m_SubmittedTiles = m_Progress.m_SubmittedTiles,
				.m_TileCount = m_Progress.m_TileCount, .m_Active = m_Visited,
				.m_ReceiverPreparation = m_ReceiverCache.GetProgress() };
		}

	private:
		enum class Status : uint8_t
		{
			Inactive,
			Running,
			Completed,
			Cancelled,
			Failed,
		};
		struct Progress
		{
			Status m_Status = Status::Inactive;
			uint64_t m_Generation = 0;
			uint64_t m_CommittedVersion = 0;
			uint32_t m_TargetSamples = 0;
			uint32_t m_SubmittedSamples = 0;
			uint32_t m_CompletedSamples = 0;
			uint32_t m_BatchSamples = 0;
			uint32_t m_SweepSamples = 0;
			uint32_t m_TileCount = 0;
			uint32_t m_SubmittedTiles = 0;
		};
		void Release() noexcept
		{
			m_ReceiverCache.Release();
			m_Textures.Release(m_Pool, m_LastUse);
			m_LastUse.Reset();
			m_PendingFence.Reset();
			m_SweepActive = false;
			m_PendingInitialized = false;
			m_Tile = {};
		}

		RHIDevice& m_Device;
		PersistentTexturePool& m_Pool;
		GIReceiverCache m_ReceiverCache;
		TemporalHistoryTextures<SurfaceCount> m_Textures;
		Progress m_Progress{};
		RHIFencePoint m_LastUse{};
		RHIFencePoint m_PendingFence{};
		uint64_t m_FrameSerial = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		bool m_Visited = false;
		bool m_Exported = false;
		bool m_Recorded = false;
		bool m_UnknownCompletion = false;
		bool m_Settled = false;
		bool m_SweepActive = false;
		bool m_PendingInitialized = false;
		uint32_t m_TileWidth = 0;
		uint32_t m_TileHeight = 0;
		Tile m_Tile{};
	};
}
