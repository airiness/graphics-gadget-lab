#include "Graphics/Pipeline/GIReferenceObservation.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"

#include <algorithm>

namespace gglab
{
	GIReferenceObservation::GIReferenceObservation(RHIDevice& device,
		PersistentTexturePool& pool) noexcept : m_Device(device), m_Pool(pool) {}
	GIReferenceObservation::~GIReferenceObservation()
	{
		GGLAB_ASSERT_MSG(!m_Textures.IsAllocated(), "GI reference requires checked renderer teardown.");
	}

	void GIReferenceObservation::BeginFrame(uint64_t serial) noexcept
	{
		m_FrameSerial = serial;
		m_Settled = false;
		m_Visited = false;
		m_Exported = false;
		m_Recorded = false;
		m_Diagnostics.m_BatchSamples = 0;
		m_Tile = {};
		if (m_PendingFence.IsValid())
		{
			const auto query = m_Device.QueryFencePoint(m_PendingFence);
			if (query == RHIFenceQueryResult::Completed)
			{
				m_Diagnostics.m_CompletedSamples = m_Diagnostics.m_SubmittedSamples;
				m_PendingFence.Reset();
				if (m_Diagnostics.m_Status == GIReferenceObservationStatus::Running &&
					m_Diagnostics.m_CompletedSamples == m_Diagnostics.m_TargetSamples)
				{
					m_Diagnostics.m_Status = GIReferenceObservationStatus::Completed;
				}
			}
			else if (query != RHIFenceQueryResult::Pending)
			{
				m_Diagnostics.m_Status = GIReferenceObservationStatus::Failed;
			}
		}
	}

	bool GIReferenceObservation::Prepare(const GIReferenceObservationKey& key,
		uint32_t samplesPerBatch, uint32_t sampleWorkBudget, uint32_t queryWorkBudget,
		uint32_t maximumTilePixels) noexcept
	{
		m_Visited = true;
		const uint64_t pixels = static_cast<uint64_t>(key.m_Width) * key.m_Height;
		if (m_UnknownCompletion || pixels == 0 || pixels > 8'388'608 || sampleWorkBudget == 0 ||
			key.m_Samples == 0 || samplesPerBatch == 0 ||
			key.m_MaximumDepth == 0 || key.m_MaximumDepth > 256 ||
			maximumTilePixels > 8'388'608)
		{
			return false;
		}
		const uint64_t queriesPerPixelSample = 2ull * key.m_MaximumDepth;
		if (queriesPerPixelSample > queryWorkBudget)
		{
			return false;
		}
		if (!m_Textures.IsAllocated() || !(m_Key == key))
		{
			Cancel();
			m_Key = key;
			const uint64_t generation = m_Diagnostics.m_Generation + 1;
			m_Diagnostics = { .m_Status = GIReferenceObservationStatus::Running,
				.m_Generation = generation, .m_TargetSamples = key.m_Samples,
				.m_MaximumDepth = key.m_MaximumDepth };
			std::array<TemporalHistoryTextures<SurfaceCount>::SurfaceDesc, SurfaceCount> descriptions{};
			constexpr std::array<std::array<std::string_view, 2>, SurfaceCount> names
			{
				std::array<std::string_view, 2>{ "GI.Reference.SunSum0", "GI.Reference.SunSum1" },
				std::array<std::string_view, 2>{ "GI.Reference.EnvironmentSum0", "GI.Reference.EnvironmentSum1" },
				std::array<std::string_view, 2>{ "GI.Reference.OutcomeSum0", "GI.Reference.OutcomeSum1" },
				std::array<std::string_view, 2>{ "GI.Reference.RandomState0", "GI.Reference.RandomState1" },
				std::array<std::string_view, 2>{ "GI.Reference.SunCompensation0", "GI.Reference.SunCompensation1" },
				std::array<std::string_view, 2>{ "GI.Reference.EnvironmentCompensation0", "GI.Reference.EnvironmentCompensation1" },
			};
			for (size_t index = 0; index < descriptions.size(); ++index)
			{
				descriptions[index] = { .m_CreateInfo = { .m_Desc = {
					.m_Format = index == 3 ? RHIFormat::R32Uint : RHIFormat::R32G32B32A32Float,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
					.m_Extent = { key.m_Width, key.m_Height, 1 } } }, .m_AllocationNames = names[index] };
			}
			if (!m_Textures.Acquire(m_Pool, descriptions))
			{
				m_Diagnostics.m_Status = GIReferenceObservationStatus::Failed;
				return false;
			}
		}
		if (m_Diagnostics.m_Status == GIReferenceObservationStatus::Failed)
		{
			return false;
		}
		if (!m_PendingFence.IsValid() && m_Diagnostics.m_Status == GIReferenceObservationStatus::Running)
		{
			if (!m_SweepActive)
			{
				const auto wholeImageSamples = std::min({ samplesPerBatch,
					static_cast<uint32_t>(sampleWorkBudget / pixels),
					static_cast<uint32_t>(queryWorkBudget / (pixels * queriesPerPixelSample)),
					key.m_Samples - m_Diagnostics.m_SubmittedSamples });
				m_Diagnostics.m_SweepSamples = std::max(1u, wholeImageSamples);
				uint64_t tilePixels = std::min(static_cast<uint64_t>(sampleWorkBudget),
					queryWorkBudget / queriesPerPixelSample) / m_Diagnostics.m_SweepSamples;
				if (maximumTilePixels != 0)
				{
					tilePixels = std::min(tilePixels, static_cast<uint64_t>(maximumTilePixels));
				}
				m_TileWidth = static_cast<uint32_t>(std::min(tilePixels,
					static_cast<uint64_t>(key.m_Width)));
				if (m_TileWidth < key.m_Width && m_TileWidth >= 8)
				{
					m_TileWidth = m_TileWidth / 8 * 8;
				}
				m_TileHeight = static_cast<uint32_t>(std::min(tilePixels / m_TileWidth,
					static_cast<uint64_t>(key.m_Height)));
				if (m_TileHeight < key.m_Height && m_TileHeight >= 8)
				{
					m_TileHeight = m_TileHeight / 8 * 8;
				}
				m_Diagnostics.m_TileCount = ((key.m_Width + m_TileWidth - 1) / m_TileWidth) *
					((key.m_Height + m_TileHeight - 1) / m_TileHeight);
				m_Diagnostics.m_SubmittedTiles = 0;
				m_SweepActive = true;
			}
			const uint32_t columns = (key.m_Width + m_TileWidth - 1) / m_TileWidth;
			const uint32_t x = (m_Diagnostics.m_SubmittedTiles % columns) * m_TileWidth;
			const uint32_t y = (m_Diagnostics.m_SubmittedTiles / columns) * m_TileHeight;
			const uint32_t width = std::min(m_TileWidth, key.m_Width - x);
			const uint32_t height = std::min(m_TileHeight, key.m_Height - y);
			const uint64_t work = static_cast<uint64_t>(width) * height * m_Diagnostics.m_SweepSamples;
			// A changed execution budget cannot submit an oversized active tile.
			// Lab budget changes restart safely; partition preferences apply next sweep.
			if (work > sampleWorkBudget || work * queriesPerPixelSample > queryWorkBudget)
			{
				return false;
			}
			m_Tile = { .m_X = x, .m_Y = y, .m_Width = width, .m_Height = height,
				.m_BeginSweep = m_Diagnostics.m_SubmittedTiles == 0,
				.m_EndSweep = m_Diagnostics.m_SubmittedTiles + 1 == m_Diagnostics.m_TileCount };
			m_Diagnostics.m_BatchSamples = m_Diagnostics.m_SweepSamples;
		}
		return true;
	}

	std::array<GIReferenceObservation::Surface, GIReferenceObservation::SurfaceCount> GIReferenceObservation::Import(
		RenderGraph::RGBuilder& builder) noexcept
	{
		std::array<Surface, SurfaceCount> surfaces{};
		constexpr std::array previous{ "GI.Reference.PreviousSunSum", "GI.Reference.PreviousEnvironmentSum",
			"GI.Reference.PreviousOutcomeSum", "GI.Reference.PreviousRandomState",
			"GI.Reference.PreviousSunCompensation", "GI.Reference.PreviousEnvironmentCompensation" };
		constexpr std::array next{ "GI.Reference.NextSunSum", "GI.Reference.NextEnvironmentSum",
			"GI.Reference.NextOutcomeSum", "GI.Reference.NextRandomState",
			"GI.Reference.NextSunCompensation", "GI.Reference.NextEnvironmentCompensation" };
		for (size_t index = 0; index < surfaces.size(); ++index)
		{
			surfaces[index] = m_Textures.Import(builder, index, previous[index], next[index],
				m_Diagnostics.m_SubmittedSamples != 0, m_PendingInitialized);
		}
		return surfaces;
	}

	void GIReferenceObservation::Export(RenderGraph::RGBuilder& builder,
		const std::array<Surface, SurfaceCount>& surfaces) noexcept
	{
		const bool writing = m_Diagnostics.m_BatchSamples != 0;
		for (const auto& surface : surfaces)
		{
			if (m_Diagnostics.m_SubmittedSamples != 0)
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

	void GIReferenceObservation::Settle(const RenderFrameOutcome& outcome) noexcept
	{
		GGLAB_ASSERT(outcome.m_FrameSerial == m_FrameSerial);
		if (m_Settled)
		{
			return;
		}
		m_Settled = true;
		if (outcome.m_Kind == RenderFrameOutcomeKind::CompletionUnknown)
		{
			m_UnknownCompletion = true;
			m_Diagnostics.m_Status = GIReferenceObservationStatus::Failed;
			return;
		}
		if (m_Visited && m_Exported && outcome.m_Fence.IsValid())
		{
			m_LastUse = outcome.m_Fence;
		}
		if (outcome.m_Kind == RenderFrameOutcomeKind::Failed)
		{
			m_Diagnostics.m_Status = GIReferenceObservationStatus::Failed;
			return;
		}
		if (!m_Visited)
		{
			Cancel();
		}
		else if (outcome.m_Kind == RenderFrameOutcomeKind::Submitted && m_Exported && m_Recorded &&
			m_Diagnostics.m_BatchSamples != 0)
		{
			GGLAB_ASSERT(outcome.m_Fence.IsValid());
			m_Textures.MarkWriteInitialized();
			m_PendingInitialized = true;
			++m_Diagnostics.m_SubmittedTiles;
			if (m_Diagnostics.m_SubmittedTiles == m_Diagnostics.m_TileCount)
			{
				m_Textures.Commit();
				m_Diagnostics.m_SubmittedSamples += m_Diagnostics.m_SweepSamples;
				++m_Diagnostics.m_CommittedVersion;
				m_SweepActive = false;
				m_PendingInitialized = false;
			}
			m_PendingFence = outcome.m_Fence;
		}
	}

	void GIReferenceObservation::Release() noexcept
	{
		m_Textures.Release(m_Pool, m_LastUse);
		m_LastUse.Reset();
		m_PendingFence.Reset();
		m_Key = {};
		m_SweepActive = false;
		m_PendingInitialized = false;
		m_Tile = {};
	}
	void GIReferenceObservation::Cancel() noexcept
	{
		if (!m_UnknownCompletion)
		{
			Release();
			if (m_Diagnostics.m_Status != GIReferenceObservationStatus::Inactive)
			{
				m_Diagnostics.m_Status = GIReferenceObservationStatus::Cancelled;
			}
		}
	}
	void GIReferenceObservation::Shutdown() noexcept
	{
		// Called only after Renderer verifies quiescence or confirms device loss.
		m_UnknownCompletion = false;
		Release();
	}
}
