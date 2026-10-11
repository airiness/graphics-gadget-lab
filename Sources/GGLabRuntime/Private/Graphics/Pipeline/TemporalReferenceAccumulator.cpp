#include "Graphics/Pipeline/TemporalReferenceAccumulator.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] RHIOwnedTextureCreateInfo MakeSumTextureCreateInfo(
			uint32_t width, uint32_t height) noexcept
		{
			return {
				.m_Desc = {
					.m_Format = TemporalReferenceSumFormat,
					.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
					.m_Extent = { width, height, 1 },
				},
				.m_InitialState = UndefinedRHITextureState(),
			};
		}
	}

	TemporalReferenceAccumulator::TemporalReferenceAccumulator(
		PersistentTexturePool* texturePool) noexcept :
		m_TexturePool(texturePool)
	{
	}

	TemporalReferenceAccumulator::~TemporalReferenceAccumulator() noexcept
	{
		GGLAB_ASSERT_MSG(!m_Sums.IsAllocated(),
			"The temporal reference sum pair must be released before destruction.");
	}

	bool TemporalReferenceAccumulator::BeginFrame(
		const std::optional<TemporalReferenceSample>& sample, uint32_t width, uint32_t height,
		const RHIFencePoint& retirementFence) noexcept
	{
		m_PendingSample.reset();
		m_Imported = false;
		m_Exported = false;
		if (!sample)
		{
			Release(retirementFence);
			return true;
		}
		GGLAB_ASSERT_MSG(sample->IsValid(), "A temporal reference sample must be valid.");
		if (!sample->IsValid() || !m_TexturePool)
		{
			return false;
		}

		if (m_Width != width || m_Height != height || !m_Sums.IsAllocated())
		{
			Release(retirementFence);
			if (!m_Sums.Acquire(*m_TexturePool, { {
				{ .m_CreateInfo = MakeSumTextureCreateInfo(width, height),
					.m_AllocationNames = { "TemporalReference.Sum0", "TemporalReference.Sum1" } },
				} }))
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Temporal reference failed to allocate its {}x{} sum pair.", width, height);
				return false;
			}
			m_Width = width;
			m_Height = height;
		}

		if (sample->m_Index == 0)
		{
			m_CommittedSamples = 0;
		}
		else if (sample->m_Index != m_CommittedSamples)
		{
			// A skipped or repeated sample would silently weight the mean.
			return false;
		}
		m_PendingSample = sample;
		return true;
	}

	bool TemporalReferenceAccumulator::ImportRenderGraphResources(
		RenderGraph::RGBuilder& builder, TemporalReferenceRenderGraphResources& outResources) noexcept
	{
		if (!m_PendingSample || m_Imported)
		{
			return false;
		}
		const bool previousValid = m_PendingSample->m_Index > 0 && m_Sums.IsReadInitialized();
		// The accumulation pass overwrites every texel of the next sum.
		const auto sums = m_Sums.Import(builder, 0, "TemporalReference.PreviousSum",
			"TemporalReference.NextSum", previousValid, false);
		outResources = {
			.m_PreviousSum = sums.m_Previous,
			.m_NextSum = sums.m_Next,
			.m_PreviousValid = previousValid,
		};
		m_Imported = true;
		return true;
	}

	bool TemporalReferenceAccumulator::ExportRenderGraphResources(
		RenderGraph::RGBuilder& builder, const TemporalReferenceRenderGraphResources& resources) noexcept
	{
		const TemporalHistoryTextures<1>::RenderGraphSurface sums{
			resources.m_PreviousSum, resources.m_NextSum };
		if (!m_Imported || m_Exported || !resources.IsValid() ||
			!TemporalHistoryTextures<1>::IsFullyWritten(builder, sums))
		{
			return false;
		}
		TemporalHistoryTextures<1>::Export(builder, sums, resources.m_PreviousValid);
		m_Exported = true;
		return true;
	}

	void TemporalReferenceAccumulator::CommitFrame() noexcept
	{
		if (m_PendingSample && m_Exported)
		{
			m_Sums.Commit();
			m_CommittedSamples = m_PendingSample->m_Index + 1;
		}
		AbortFrame();
	}

	void TemporalReferenceAccumulator::AbortFrame() noexcept
	{
		m_PendingSample.reset();
		m_Imported = false;
		m_Exported = false;
	}

	void TemporalReferenceAccumulator::Release(const RHIFencePoint& retirementFence) noexcept
	{
		// The renderer's last submitted fence covers every frame that used the pair.
		if (m_TexturePool)
		{
			m_Sums.Release(*m_TexturePool, retirementFence);
		}
		m_CommittedSamples = 0;
		m_Width = 0;
		m_Height = 0;
		AbortFrame();
	}
}
