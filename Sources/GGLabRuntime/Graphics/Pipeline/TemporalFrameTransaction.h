#pragma once
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Pipeline/TemporalAA.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <cstdint>

namespace gglab
{
	struct RenderView;

	struct TemporalCommittedViewState
	{
		Matrix m_View = Matrix::Identity;
		Matrix m_RasterViewProj = Matrix::Identity;
		Vector4 m_DepthReconstructionParams = Vector4::Zero;
		Vector2 m_JitterUV = Vector2::Zero;
		DepthConvention m_DepthConvention = DepthConvention::Standard;
		RenderViewID m_DisplayViewId = RenderViewID::Unknown;
		uint64_t m_ResetIdentity = 0;
		uint64_t m_SessionIdentity = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
	};

	struct TemporalViewHistory
	{
		TemporalCommittedViewState m_Committed{};
		uint32_t m_NextJitterIndex = 0;
		bool m_Valid = false;

		void Invalidate() noexcept;
	};

	enum class TemporalFrameTransactionState : uint8_t
	{
		Idle,
		Pending,
		Committed,
		Aborted,
		Invalidated,
	};

	class TemporalFrameTransaction
	{
	public:
		void Begin(TemporalViewHistory& history, const ResolvedTemporalFramePlan& plan,
			uint32_t width, uint32_t height) noexcept;
		void PrepareDisplayView(RenderView& view) noexcept;
		void MarkResolveParticipated() noexcept;
		void CommitCompleted() noexcept;
		void Abort() noexcept;
		void InvalidateAfterFatal() noexcept;

		[[nodiscard]] TemporalFrameTransactionState GetState() const noexcept { return m_State; }
		[[nodiscard]] uint32_t GetJitterIndex() const noexcept { return m_JitterIndex; }
		[[nodiscard]] const Vector2& GetJitterPixels() const noexcept { return m_JitterPixels; }
		[[nodiscard]] bool HasCompatiblePreviousView() const noexcept
		{
			return m_HasCompatiblePreviousView;
		}
		[[nodiscard]] bool ParticipatedInResolve() const noexcept
		{
			return m_ParticipatedInResolve;
		}

	private:
		[[nodiscard]] bool IsCompatible(const TemporalViewHistory& history) const noexcept;

		TemporalViewHistory* m_History = nullptr;
		ResolvedTemporalFramePlan m_Plan{};
		TemporalCommittedViewState m_PendingView{};
		TemporalFrameTransactionState m_State = TemporalFrameTransactionState::Idle;
		Vector2 m_JitterPixels = Vector2::Zero;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_JitterIndex = 0;
		bool m_HasCompatiblePreviousView = false;
		bool m_HasPendingView = false;
		bool m_ParticipatedInResolve = false;
	};
}
