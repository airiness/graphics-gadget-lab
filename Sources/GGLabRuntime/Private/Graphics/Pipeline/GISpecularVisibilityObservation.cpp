#include "Graphics/Pipeline/GISpecularVisibilityObservation.h"
#include "GGLabRuntime/Graphics/Pipeline/GIObservationLimits.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <string_view>

namespace gglab
{
	GISpecularVisibilityObservation::GISpecularVisibilityObservation(RHIDevice& device,
		PersistentTexturePool& pool) noexcept :
		m_Execution(device, pool, { "GI.SpecularVisibility.ReceiverCache.PositionError",
			"GI.SpecularVisibility.ReceiverCache.FaceMatch", "GI.SpecularVisibility.ReceiverCache.Instance",
			"GI.SpecularVisibility.ReceiverCache.Primitive" }) {}

	bool GISpecularVisibilityObservation::Prepare(const GISpecularVisibilityKey& key,
		uint32_t samplesPerBatch, uint32_t queryWorkBudget,
		uint32_t maximumTilePixels, uint32_t receiverQueryBudget) noexcept
	{
		const GIObservationWorkBudget budget
		{
			.m_PixelSampleWork = queryWorkBudget / GIObservationLimits::VisibilityQueriesPerSample,
			.m_SamplesPerBatch = samplesPerBatch,
			.m_MaximumTilePixels = maximumTilePixels,
			.m_ReceiverQueryWork = receiverQueryBudget,
		};
		if (!m_Execution.ValidatePreparation(key.m_Width, key.m_Height, key.m_Samples, budget))
		{
			return false;
		}
		if (!m_Execution.IsAllocated() || !(m_Key == key))
		{
			m_Key = key;
			std::array<GITiledObservation<SurfaceCount>::SurfaceDesc, SurfaceCount> descriptions{};
			constexpr std::array<std::array<std::string_view, 2>, SurfaceCount> names
			{
				std::array<std::string_view, 2>{ "GI.SpecularVisibility.BaseSum0", "GI.SpecularVisibility.BaseSum1" },
				std::array<std::string_view, 2>{ "GI.SpecularVisibility.CoatSum0", "GI.SpecularVisibility.CoatSum1" },
				std::array<std::string_view, 2>{ "GI.SpecularVisibility.BaseCompensation0", "GI.SpecularVisibility.BaseCompensation1" },
				std::array<std::string_view, 2>{ "GI.SpecularVisibility.CoatCompensation0", "GI.SpecularVisibility.CoatCompensation1" },
			};
			for (size_t index = 0; index < descriptions.size(); ++index)
			{
				descriptions[index] = { .m_CreateInfo = { .m_Desc = {
					.m_Format = RHIFormat::R32G32B32A32Float,
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

	std::array<GISpecularVisibilityObservation::Surface, GISpecularVisibilityObservation::SurfaceCount>
		GISpecularVisibilityObservation::Import(RenderGraph::RGBuilder& builder) noexcept
	{
		constexpr std::array previous{ "GI.SpecularVisibility.PreviousBaseSum",
			"GI.SpecularVisibility.PreviousCoatSum", "GI.SpecularVisibility.PreviousBaseCompensation",
			"GI.SpecularVisibility.PreviousCoatCompensation" };
		constexpr std::array next{ "GI.SpecularVisibility.NextBaseSum",
			"GI.SpecularVisibility.NextCoatSum", "GI.SpecularVisibility.NextBaseCompensation",
			"GI.SpecularVisibility.NextCoatCompensation" };
		return m_Execution.Import(builder, previous, next);
	}

	void GISpecularVisibilityObservation::Settle(const RenderFrameOutcome& outcome) noexcept
	{
		if (m_Execution.Settle(outcome))
		{
			m_Key = {};
		}
	}
	void GISpecularVisibilityObservation::Cancel() noexcept
	{
		if (m_Execution.Cancel())
		{
			m_Key = {};
		}
	}
	void GISpecularVisibilityObservation::Shutdown() noexcept
	{
		m_Execution.Shutdown();
		m_Key = {};
	}
}
