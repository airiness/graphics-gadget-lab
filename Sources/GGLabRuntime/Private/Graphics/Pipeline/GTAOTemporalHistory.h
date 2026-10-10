#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAO.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/Resource/PersistentTexturePool.h"

#include <array>
#include <cstdint>

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
		// The frame was submitted: an exported pair becomes the committed history, and an
		// active frame that exported nothing leaves no history to continue.
		void CommitFrame() noexcept;
		// The frame ended without submission: the committed history is unchanged.
		void AbortFrame() noexcept;
		// Retires the pairs after a fatal submission, before shutdown, or when inactive.
		void Release(const RHIFencePoint& retirementFence) noexcept;

		[[nodiscard]] bool HasCommittedHistory() const noexcept { return m_CommittedValid; }

	private:
		PersistentTexturePool* m_TexturePool = nullptr;
		std::array<PersistentTextureAllocation, 2> m_Visibility;
		std::array<PersistentTextureAllocation, 2> m_ViewZ;
		std::array<bool, 2> m_Initialized{};
		GTAOExtent m_Extent{};
		uint32_t m_ReadIndex = 0;
		bool m_CommittedValid = false;
		bool m_Active = false;
		bool m_Imported = false;
		bool m_Exported = false;
	};
}
