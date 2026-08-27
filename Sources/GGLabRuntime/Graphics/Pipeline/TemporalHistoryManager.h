#pragma once
#include "Core/Math/Vector.h"
#include "Graphics/Pipeline/TemporalAA.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Resource/PersistentTexturePool.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace gglab
{
	inline constexpr RHIFormat TemporalHistoryColorFormat = RHIFormat::R16G16B16A16Float;
	inline constexpr RHIFormat TemporalHistoryDepthFormat = RHIFormat::R32Float;

	struct TemporalHistorySurfaceFormatSupport
	{
		RHITextureSupportResult m_ShaderResource{};
		RHITextureSupportResult m_TypedUavStore{};

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return m_ShaderResource.IsSupported() && m_TypedUavStore.IsSupported();
		}
	};

	struct TemporalHistoryFormatSupport
	{
		TemporalHistorySurfaceFormatSupport m_Color{};
		TemporalHistorySurfaceFormatSupport m_Depth{};

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return m_Color.IsSupported() && m_Depth.IsSupported();
		}
	};

	[[nodiscard]] TemporalHistoryFormatSupport QueryTemporalHistoryFormatSupport(
		const RHIDevice& device) noexcept;

	enum class TemporalHistoryResetReason : uint8_t
	{
		None,
		ColdStart,
		Disabled,
		DisplayViewChanged,
		ResetIdentityChanged,
		SessionIdentityChanged,
		ExtentChanged,
		FormatChanged,
		AllocationFailure,
		AvailabilityChanged,
		FatalSubmission,
		Resume,
		Shutdown,
	};

	struct TemporalHistoryCompatibilityIdentity
	{
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		RHIFormat m_ColorFormat = TemporalHistoryColorFormat;
		RHIFormat m_DepthFormat = TemporalHistoryDepthFormat;

		bool operator==(const TemporalHistoryCompatibilityIdentity&) const noexcept = default;
	};

	struct TemporalHistoryCommittedMetadata
	{
		TemporalHistoryCompatibilityIdentity m_Compatibility{};
		Vector2 m_JitterUV = Vector2::Zero;
		uint32_t m_JitterIndex = 0;
		RHIFencePoint m_GraphicsFence{};
	};

	struct TemporalHistoryFrameState
	{
		uint64_t m_AllocationGeneration = 0;
		uint32_t m_ReadIndex = 0;
		uint32_t m_WriteIndex = 1;
		bool m_Active = false;
		bool m_PreviousValid = false;
		bool m_RenderGraphImported = false;
		bool m_RenderGraphExported = false;
		bool m_Ended = false;
	};

	struct TemporalHistoryRenderGraphResources
	{
		RGTextureId m_PreviousColor;
		RGTextureId m_PreviousDepth;
		RGTextureId m_NextColor;
		RGTextureId m_NextDepth;
		uint32_t m_ReadIndex = 0;
		uint32_t m_WriteIndex = 1;
		bool m_PreviousValid = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_PreviousColor.IsValid() && m_PreviousDepth.IsValid() &&
				m_NextColor.IsValid() && m_NextDepth.IsValid();
		}
	};

	struct TemporalHistoryManagerDiagnostics
	{
		TemporalHistoryCompatibilityIdentity m_Compatibility{};
		TemporalHistoryCommittedMetadata m_LastCommitted{};
		TemporalHistoryResetReason m_LastResetReason = TemporalHistoryResetReason::None;
		uint64_t m_AllocationGeneration = 0;
		uint64_t m_ResetCount = 0;
		uint64_t m_ActiveBytes = 0;
		uint64_t m_PendingRetirementBytes = 0;
		uint32_t m_ReadIndex = 0;
		bool m_HasActiveHistory = false;
		bool m_HistoryValid = false;
		std::vector<RHIFencePoint> m_PendingRetirementFences;
	};

	class TemporalHistoryManager
	{
	public:
		explicit TemporalHistoryManager(PersistentTexturePool* texturePool) noexcept;
		~TemporalHistoryManager() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TemporalHistoryManager);

		[[nodiscard]] TemporalHistoryFrameState BeginFrame(
			const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept;
		[[nodiscard]] bool ImportRenderGraphResources(TemporalHistoryFrameState& frame,
			RenderGraph::RGBuilder& builder,
			TemporalHistoryRenderGraphResources& outResources) noexcept;
		[[nodiscard]] bool ExportRenderGraphResources(TemporalHistoryFrameState& frame,
			RenderGraph::RGBuilder& builder,
			const TemporalHistoryRenderGraphResources& resources) noexcept;

		[[nodiscard]] bool CommitFrame(TemporalHistoryFrameState& frame,
			const TemporalHistoryCommittedMetadata& metadata,
			const RHIFencePoint& submittedFence) noexcept;
		void AbortFrame(
			TemporalHistoryFrameState& frame, const RHIFencePoint& retirementFence) noexcept;
		void InvalidateAfterFatal(TemporalHistoryFrameState& frame,
			const RHIFencePoint& submittedFence) noexcept;
		void Invalidate(TemporalHistoryResetReason reason,
			const RHIFencePoint& retirementFence = {}) noexcept;
		void Shutdown() noexcept;

		[[nodiscard]] TemporalHistoryManagerDiagnostics GetDiagnostics() const;

	private:
		struct HistorySet
		{
			std::array<PersistentTextureAllocation, 2> m_Color;
			std::array<PersistentTextureAllocation, 2> m_Depth;
			std::array<bool, 2> m_Initialized{};
			TemporalHistoryCompatibilityIdentity m_Compatibility{};
			TemporalHistoryCommittedMetadata m_LastCommitted{};
			RHIFencePoint m_LastPossibleUseFence{};
			uint64_t m_AllocationGeneration = 0;
			uint32_t m_ReadIndex = 0;
			bool m_Valid = false;
		};

		[[nodiscard]] TemporalHistoryResetReason ResolveCompatibilityResetReason(
			const TemporalHistoryCompatibilityIdentity& compatibility) const noexcept;
		[[nodiscard]] bool CreateHistorySet(
			const TemporalHistoryCompatibilityIdentity& compatibility) noexcept;
		[[nodiscard]] bool IsCurrentFrame(const TemporalHistoryFrameState& frame) const noexcept;
		void RetireActiveHistory(TemporalHistoryResetReason reason,
			const RHIFencePoint& retirementFence) noexcept;
		void RecordReset(TemporalHistoryResetReason reason) noexcept;
		static void UpdateFence(
			RHIFencePoint& destination, const RHIFencePoint& candidate) noexcept;

		PersistentTexturePool* m_TexturePool = nullptr;
		std::optional<HistorySet> m_ActiveHistory;
		TemporalHistoryResetReason m_LastResetReason = TemporalHistoryResetReason::None;
		uint64_t m_NextAllocationGeneration = 1;
		uint64_t m_ResetCount = 0;
		bool m_HasEstablishedHistory = false;
		bool m_Shutdown = false;
	};
}
