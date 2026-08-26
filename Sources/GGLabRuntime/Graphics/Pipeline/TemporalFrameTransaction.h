#pragma once
#include "Core/Hash/KeyHash.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Pipeline/TemporalAA.h"
#include "Graphics/ScreenSpace/ScreenSpaceTypes.h"

#include <cstdint>
#include <tuple>
#include <unordered_map>

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

	struct RenderObjectHistoryKey
	{
		uint32_t m_EntityIdentity = 0;
		ModelID m_ModelId{};
		uint64_t m_ModelContentGeneration = 0;
		uint32_t m_ModelMeshIndex = 0;
		MeshID m_MeshId{};
		uint64_t m_MeshContentGeneration = 0;
		uint64_t m_SessionIdentity = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_ModelId.IsValid() && m_ModelContentGeneration != 0 &&
				m_MeshId.IsValid() && m_MeshContentGeneration != 0 &&
				m_SessionIdentity != 0;
		}

		[[nodiscard]] constexpr auto AsTuple() const noexcept
		{
			return std::tuple{ m_EntityIdentity, m_ModelId.Value(), m_ModelContentGeneration,
				m_ModelMeshIndex, m_MeshId.Value(), m_MeshContentGeneration, m_SessionIdentity };
		}

		friend constexpr bool operator==(
			const RenderObjectHistoryKey&, const RenderObjectHistoryKey&) = default;
	};
	using RenderObjectHistoryKeyHash = KeyHash<RenderObjectHistoryKey>;

	struct TemporalCommittedObjectState
	{
		Matrix m_Model = Matrix::Identity;
		uint64_t m_LastSeenCommittedFrame = 0;
	};

	struct TemporalObjectHistoryDiagnostics
	{
		uint32_t m_EntryCount = 0;
		uint32_t m_Capacity = MaxObjectCapacity;
		uint64_t m_LastCommittedFrame = 0;
	};

	class TemporalObjectHistory
	{
	public:
		void Invalidate() noexcept;
		[[nodiscard]] const TemporalCommittedObjectState* Find(
			const RenderObjectHistoryKey& key) const noexcept;
		[[nodiscard]] TemporalObjectHistoryDiagnostics GetDiagnostics() const noexcept;

	private:
		friend class TemporalFrameTransaction;

		std::unordered_map<RenderObjectHistoryKey, TemporalCommittedObjectState,
			RenderObjectHistoryKeyHash>
			m_Committed;
		uint64_t m_LastCommittedFrame = 0;
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
		void Begin(TemporalViewHistory& viewHistory, TemporalObjectHistory& objectHistory,
			const ResolvedTemporalFramePlan& plan, uint32_t width, uint32_t height) noexcept;
		void PrepareDisplayView(RenderView& view) noexcept;
		[[nodiscard]] Matrix ResolvePreviousObjectModel(
			const RenderObjectHistoryKey& key, const Matrix& currentModel) const noexcept;
		[[nodiscard]] bool StageSubmittedObject(
			const RenderObjectHistoryKey& key, const Matrix& currentModel) noexcept;
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
		[[nodiscard]] uint64_t GetSessionIdentity() const noexcept
		{
			return m_Plan.m_SessionIdentity;
		}

	private:
		[[nodiscard]] bool IsCompatible(const TemporalViewHistory& history) const noexcept;
		void CommitObjectHistory() noexcept;

		TemporalViewHistory* m_ViewHistory = nullptr;
		TemporalObjectHistory* m_ObjectHistory = nullptr;
		ResolvedTemporalFramePlan m_Plan{};
		TemporalCommittedViewState m_PendingView{};
		std::unordered_map<RenderObjectHistoryKey, Matrix, RenderObjectHistoryKeyHash>
			m_PendingObjects;
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
