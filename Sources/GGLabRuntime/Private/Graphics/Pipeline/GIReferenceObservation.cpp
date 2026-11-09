#include "Graphics/Pipeline/GIReferenceObservation.h"
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <algorithm>
#include <string_view>

namespace gglab
{
	GIReferenceObservation::GIReferenceObservation(RHIDevice& device,
		PersistentTexturePool& pool) noexcept :
		m_Execution(device, pool, { "GI.Reference.ReceiverCache.PositionError",
			"GI.Reference.ReceiverCache.FaceMatch", "GI.Reference.ReceiverCache.Instance",
			"GI.Reference.ReceiverCache.Primitive" }) {}

	bool GIReferenceObservation::Prepare(const GIReferenceObservationKey& key,
		uint32_t samplesPerBatch, uint32_t sampleWorkBudget, uint32_t queryWorkBudget,
		uint32_t maximumTilePixels, uint32_t receiverQueryBudget) noexcept
	{
		const uint64_t queriesPerPixelSample =
			uint64_t(GIObservationLimits::QueriesPerDiffuseDepth) * key.m_MaximumDepth;
		const GIObservationWorkBudget budget
		{
			.m_PixelSampleWork = queriesPerPixelSample != 0 ?
				std::min(uint64_t(sampleWorkBudget), queryWorkBudget / queriesPerPixelSample) :
				0,
			.m_SamplesPerBatch = samplesPerBatch,
			.m_MaximumTilePixels = maximumTilePixels,
			.m_ReceiverQueryWork = receiverQueryBudget,
		};
		if (!m_Execution.ValidatePreparation(key.m_Width, key.m_Height, key.m_Samples, budget) ||
			key.m_MaximumDepth == 0 || key.m_MaximumDepth > GIObservationLimits::MaximumDiffuseDepth)
		{
			return false;
		}
		if (!m_Execution.IsAllocated() || !(m_Key == key))
		{
			m_Key = key;
			m_MaximumDepth = key.m_MaximumDepth;
			std::array<GITiledObservation<SurfaceCount>::SurfaceDesc, SurfaceCount> descriptions{};
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
					.m_Format = index == static_cast<size_t>(SurfaceSlot::RandomState) ? RHIFormat::R32Uint : RHIFormat::R32G32B32A32Float,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess | RHITextureUsage::CopySource,
					.m_Extent = { key.m_Width, key.m_Height, 1 } } }, .m_AllocationNames = names[index] };
			}
			if (!m_Execution.Reset(key.m_Width, key.m_Height, key.m_Samples, descriptions))
			{
				return false;
			}
		}
		return m_Execution.Prepare(budget);
	}

	std::array<GIReferenceObservation::Surface, GIReferenceObservation::SurfaceCount> GIReferenceObservation::Import(
		RenderGraph::RGBuilder& builder) noexcept
	{
		constexpr std::array previous{ "GI.Reference.PreviousSunSum", "GI.Reference.PreviousEnvironmentSum",
			"GI.Reference.PreviousOutcomeSum", "GI.Reference.PreviousRandomState",
			"GI.Reference.PreviousSunCompensation", "GI.Reference.PreviousEnvironmentCompensation" };
		constexpr std::array next{ "GI.Reference.NextSunSum", "GI.Reference.NextEnvironmentSum",
			"GI.Reference.NextOutcomeSum", "GI.Reference.NextRandomState",
			"GI.Reference.NextSunCompensation", "GI.Reference.NextEnvironmentCompensation" };
		return m_Execution.Import(builder, previous, next);
	}

	void GIReferenceObservation::Settle(const RenderFrameOutcome& outcome) noexcept
	{
		if (m_Execution.Settle(outcome))
		{
			m_Key = {};
		}
	}
	void GIReferenceObservation::Cancel() noexcept
	{
		if (m_Execution.Cancel())
		{
			m_Key = {};
		}
	}
	void GIReferenceObservation::Shutdown() noexcept
	{
		m_Execution.Shutdown();
		m_Key = {};
	}
}
