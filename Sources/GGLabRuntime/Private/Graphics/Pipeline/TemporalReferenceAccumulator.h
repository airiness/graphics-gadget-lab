#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/Pipeline/TemporalHistoryTextures.h"
#include "Graphics/Resource/PersistentTexturePool.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	// Owns the persistent RGBA32F sum pair of the supersampled reference. Sample N
	// reads the committed sum of N samples and writes the sum of N + 1; the write
	// becomes the committed sum only when its frame is submitted, so a frame that
	// ends without submission repeats the same sample. The pair exists only while
	// frames carry reference samples and retires through the pool's fences.
	class TemporalReferenceAccumulator
	{
	public:
		explicit TemporalReferenceAccumulator(PersistentTexturePool* texturePool) noexcept;
		~TemporalReferenceAccumulator() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TemporalReferenceAccumulator);

		// Begins a frame. A frame without a sample retires the pair after the fence;
		// a new extent reallocates it. Returns false when a sample cannot be
		// accumulated: the pair could not be allocated, or the sample does not follow
		// the committed sum.
		[[nodiscard]] bool BeginFrame(const std::optional<TemporalReferenceSample>& sample,
			uint32_t width, uint32_t height, const RHIFencePoint& retirementFence) noexcept;
		[[nodiscard]] bool ImportRenderGraphResources(RenderGraph::RGBuilder& builder,
			TemporalReferenceRenderGraphResources& outResources) noexcept;
		[[nodiscard]] bool ExportRenderGraphResources(RenderGraph::RGBuilder& builder,
			const TemporalReferenceRenderGraphResources& resources) noexcept;
		// The frame was submitted: an exported sum becomes the committed sum.
		void CommitFrame() noexcept;
		// The frame ended without submission: the committed sum is unchanged.
		void AbortFrame() noexcept;
		// Retires the pair after a fatal submission, before shutdown, or when sampling ends.
		void Release(const RHIFencePoint& retirementFence) noexcept;

		[[nodiscard]] uint32_t GetCommittedSampleCount() const noexcept
		{
			return m_CommittedSamples;
		}

	private:
		PersistentTexturePool* m_TexturePool = nullptr;
		TemporalHistoryTextures<1> m_Sums;
		uint32_t m_CommittedSamples = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::optional<TemporalReferenceSample> m_PendingSample;
		bool m_Imported = false;
		bool m_Exported = false;
	};
}
