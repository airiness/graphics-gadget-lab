#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAO.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/Pipeline/TemporalHistoryTextures.h"
#include "Graphics/Resource/PersistentTexturePool.h"

#include <cstddef>

namespace gglab
{
	// Owns the persistent visibility and view-Z pairs of temporal GTAO at half render
	// extent. A frame reads the committed pair and writes the other one, which becomes
	// the committed history only when its frame is submitted; a frame that ends without
	// submission leaves the committed history unchanged. The pairs exist only while temporal
	// GTAO keeps its own history (see UsesAmbientOcclusionHistory) and retire through the
	// pool's fences.
	class GTAOTemporalHistory
	{
	public:
		explicit GTAOTemporalHistory(PersistentTexturePool* texturePool) noexcept;
		~GTAOTemporalHistory() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(GTAOTemporalHistory);

		// Begins a frame. An inactive frame retires the pairs after the fence; a new
		// extent reallocates them. Returns false when an active frame cannot allocate them.
		[[nodiscard]] bool BeginFrame(bool active, const GTAOExtent& halfExtent,
			const RHIFencePoint& retirementFence) noexcept;
		// The committed history is read only when the frame continues the previous view.
		[[nodiscard]] bool ImportRenderGraphResources(RenderGraph::RGBuilder& builder,
			bool continuesPreviousView,
			GTAOTemporalHistoryRenderGraphResources& outResources) noexcept;
		[[nodiscard]] bool ExportRenderGraphResources(RenderGraph::RGBuilder& builder,
			const GTAOTemporalHistoryRenderGraphResources& resources) noexcept;
		// A frame without depth-coverage draws needs no AO samples. Stage that outcome
		// explicitly before importing resources; only submission makes it continuous.
		[[nodiscard]] bool StageEmptyFrame() noexcept;
		// The frame was submitted: commit either exported samples or the staged empty
		// outcome. An unstaged frame that exported nothing invalidates continuity.
		void CommitFrame() noexcept;
		// The frame ended without submission: the committed history is unchanged.
		void AbortFrame() noexcept;
		// Retires the pairs after a fatal submission, before shutdown, or when inactive.
		void Release(const RHIFencePoint& retirementFence) noexcept;

		[[nodiscard]] bool HasCommittedHistory() const noexcept
		{
			return m_CommittedFrame == CommittedFrame::Samples;
		}
		[[nodiscard]] bool HasCompatiblePreviousHistory(bool continuesPreviousView) const noexcept
		{
			return m_Active && continuesPreviousView && HasCommittedHistory() &&
				m_Textures.IsReadInitialized();
		}
		// An empty submitted frame preserves continuity but provides no samples to read.
		[[nodiscard]] bool HasCompatiblePreviousFrame(bool continuesPreviousView) const noexcept
		{
			return m_Active && continuesPreviousView &&
				(m_CommittedFrame == CommittedFrame::Empty ||
					HasCompatiblePreviousHistory(continuesPreviousView));
		}

	private:
		enum class CommittedFrame
		{
			None,
			Samples,
			Empty,
		};

		enum HistorySurface : size_t
		{
			VisibilitySurface,
			ViewZSurface,
			HistorySurfaceCount,
		};

		PersistentTexturePool* m_TexturePool = nullptr;
		TemporalHistoryTextures<HistorySurfaceCount> m_Textures;
		GTAOExtent m_Extent{};
		CommittedFrame m_CommittedFrame = CommittedFrame::None;
		bool m_Active = false;
		bool m_Imported = false;
		bool m_Exported = false;
		bool m_EmptyFrameStaged = false;
	};
}
