#pragma once
#include "Graphics/Resource/PersistentTexturePool.h"
#include "Graphics/RenderFrameOutcome.h"
#include "GGLabRuntime/Graphics/Pipeline/GIReceiverPreparation.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gglab
{
	class RHIDevice;

	// The observation supplies identity/reset policy. This owner supplies bounded
	// preparation, graph accesses and fence-safe retention, without owning assets.
	class GIReceiverCache final
	{
	public:
		static constexpr size_t SurfaceCount = 4;
		using Views = std::array<RGTextureViewId, SurfaceCount>;
		GIReceiverCache(RHIDevice& device, PersistentTexturePool& pool,
			std::array<std::string_view, SurfaceCount> names) noexcept;
		~GIReceiverCache();
		void BeginFrame(uint64_t serial) noexcept;
		[[nodiscard]] bool Prepare(uint32_t width, uint32_t height, uint32_t queryBudget) noexcept;
		[[nodiscard]] Views Import(RenderGraph::RGBuilder& builder) noexcept;
		void MarkRecorded() noexcept { m_Recorded = true; }
		void Settle(const RenderFrameOutcome& outcome) noexcept;
		void Release() noexcept;
		void Shutdown() noexcept;
		[[nodiscard]] bool CanSample() const noexcept;
		[[nodiscard]] uint32_t GetBeginPixel() const noexcept { return m_SubmittedPixels; }
		[[nodiscard]] GIReceiverPreparationProgress GetProgress() const noexcept;

	private:
		RHIDevice& m_Device;
		PersistentTexturePool& m_Pool;
		std::array<std::string_view, SurfaceCount> m_Names;
		std::array<PersistentTextureAllocation, SurfaceCount> m_Textures;
		RHIFencePoint m_LastUse;
		RHIFencePoint m_PendingFence;
		uint64_t m_FrameSerial = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_SubmittedPixels = 0;
		uint32_t m_CompletedPixels = 0;
		uint32_t m_BatchPixels = 0;
		uint32_t m_QueryBudget = 0;
		bool m_Initialized = false;
		bool m_Exported = false;
		bool m_Recorded = false;
		bool m_Settled = false;
		bool m_Failed = false;
		bool m_UnknownCompletion = false;
	};
}
