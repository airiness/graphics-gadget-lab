#include "Graphics/Pipeline/TemporalReferenceAccumulator.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <algorithm>
#include <utility>

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
		GGLAB_ASSERT_MSG(!m_Sums[0].IsValid() && !m_Sums[1].IsValid(),
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

		if (m_Width != width || m_Height != height || !m_Sums[0].IsValid())
		{
			Release(retirementFence);
			const RHIOwnedTextureCreateInfo createInfo = MakeSumTextureCreateInfo(width, height);
			m_Sums[0] = m_TexturePool->AcquireTexture(createInfo, "TemporalReference.Sum0");
			m_Sums[1] = m_TexturePool->AcquireTexture(createInfo, "TemporalReference.Sum1");
			if (!m_Sums[0].IsValid() || !m_Sums[1].IsValid())
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"Temporal reference failed to allocate its {}x{} sum pair.", width, height);
				Release(retirementFence);
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
		const uint32_t readIndex = m_ReadIndex;
		const uint32_t writeIndex = 1u - m_ReadIndex;
		const bool previousValid = m_PendingSample->m_Index > 0 && m_Initialized[readIndex];
		outResources = {
			.m_PreviousSum = builder.ImportTexture("TemporalReference.PreviousSum",
				m_Sums[readIndex].GetTexture(), m_Sums[readIndex].GetCreateInfo().m_Desc,
				m_Initialized[readIndex] ? CommonRHIResourceState() : UndefinedRHITextureState(),
				previousValid ? RGContentValidity::Defined : RGContentValidity::Undefined),
			.m_NextSum = builder.ImportTexture("TemporalReference.NextSum",
				m_Sums[writeIndex].GetTexture(), m_Sums[writeIndex].GetCreateInfo().m_Desc,
				m_Initialized[writeIndex] ? CommonRHIResourceState() : UndefinedRHITextureState(),
				RGContentValidity::Undefined),
			.m_PreviousValid = previousValid,
		};
		m_Imported = true;
		return true;
	}

	bool TemporalReferenceAccumulator::ExportRenderGraphResources(
		RenderGraph::RGBuilder& builder, const TemporalReferenceRenderGraphResources& resources) noexcept
	{
		if (!m_Imported || m_Exported || !resources.IsValid() ||
			!builder.IsTextureFullyWrittenByCurrentPass(resources.m_NextSum))
		{
			return false;
		}
		if (resources.m_PreviousValid)
		{
			builder.Export(resources.m_PreviousSum, RGTextureAccess::None);
		}
		builder.Export(resources.m_NextSum, RGTextureAccess::None);
		m_Exported = true;
		return true;
	}

	void TemporalReferenceAccumulator::CommitFrame() noexcept
	{
		if (m_PendingSample && m_Exported)
		{
			m_ReadIndex = 1u - m_ReadIndex;
			m_Initialized[m_ReadIndex] = true;
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
		for (PersistentTextureAllocation& sum : m_Sums)
		{
			if (!sum.IsValid())
			{
				continue;
			}
			// The renderer's last submitted fence covers every frame that used the pair.
			const bool released = retirementFence.IsValid()
				? m_TexturePool->ReleaseTexture(std::move(sum), retirementFence)
				: m_TexturePool->ReleaseTextureWithoutSubmission(std::move(sum));
			GGLAB_ASSERT_MSG(released, "Temporal reference sums must retire through their pool.");
		}
		m_Initialized = {};
		m_ReadIndex = 0;
		m_CommittedSamples = 0;
		m_Width = 0;
		m_Height = 0;
		AbortFrame();
	}
}
