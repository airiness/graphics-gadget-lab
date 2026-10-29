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
		uint32_t samplesPerBatch, uint32_t sampleWorkBudget) noexcept
	{
		m_Visited = true;
		const uint64_t pixels = static_cast<uint64_t>(key.m_Width) * key.m_Height;
		if (m_UnknownCompletion || pixels == 0 || pixels > sampleWorkBudget ||
			key.m_Samples == 0 || samplesPerBatch == 0)
		{
			return false;
		}
		if (!m_Textures.IsAllocated() || !(m_Key == key))
		{
			Cancel();
			m_Key = key;
			const uint64_t generation = m_Diagnostics.m_Generation + 1;
			m_Diagnostics = { .m_Status = GIReferenceObservationStatus::Running,
				.m_Generation = generation, .m_TargetSamples = key.m_Samples };
			std::array<TemporalHistoryTextures<4>::SurfaceDesc, 4> descriptions{};
			constexpr std::array<std::array<std::string_view, 2>, 4> names
			{
				std::array<std::string_view, 2>{ "GI.Reference.SunSum0", "GI.Reference.SunSum1" },
				std::array<std::string_view, 2>{ "GI.Reference.EnvironmentSum0", "GI.Reference.EnvironmentSum1" },
				std::array<std::string_view, 2>{ "GI.Reference.OutcomeSum0", "GI.Reference.OutcomeSum1" },
				std::array<std::string_view, 2>{ "GI.Reference.RandomState0", "GI.Reference.RandomState1" },
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
			m_Diagnostics.m_BatchSamples = std::min({ samplesPerBatch,
				static_cast<uint32_t>(sampleWorkBudget / pixels),
				key.m_Samples - m_Diagnostics.m_SubmittedSamples });
		}
		return true;
	}

	std::array<GIReferenceObservation::Surface, 4> GIReferenceObservation::Import(
		RenderGraph::RGBuilder& builder) noexcept
	{
		std::array<Surface, 4> surfaces{};
		constexpr std::array previous{ "GI.Reference.PreviousSunSum", "GI.Reference.PreviousEnvironmentSum",
			"GI.Reference.PreviousOutcomeSum", "GI.Reference.PreviousRandomState" };
		constexpr std::array next{ "GI.Reference.NextSunSum", "GI.Reference.NextEnvironmentSum",
			"GI.Reference.NextOutcomeSum", "GI.Reference.NextRandomState" };
		for (size_t index = 0; index < surfaces.size(); ++index)
		{
			surfaces[index] = m_Textures.Import(builder, index, previous[index], next[index],
				m_Diagnostics.m_SubmittedSamples != 0, false);
		}
		return surfaces;
	}

	void GIReferenceObservation::Export(RenderGraph::RGBuilder& builder,
		const std::array<Surface, 4>& surfaces) noexcept
	{
		const bool writing = m_Diagnostics.m_BatchSamples != 0;
		for (const auto& surface : surfaces)
		{
			if (m_Diagnostics.m_SubmittedSamples != 0)
			{
				builder.Export(surface.m_Previous, RGTextureAccess::None);
			}
			if (writing)
			{
				GGLAB_ASSERT(TemporalHistoryTextures<4>::IsFullyWritten(builder, surface));
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
			m_Textures.Commit();
			m_Diagnostics.m_SubmittedSamples += m_Diagnostics.m_BatchSamples;
			++m_Diagnostics.m_CommittedVersion;
			m_PendingFence = outcome.m_Fence;
		}
	}

	void GIReferenceObservation::Release() noexcept
	{
		m_Textures.Release(m_Pool, m_LastUse);
		m_LastUse.Reset();
		m_PendingFence.Reset();
		m_Key = {};
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
