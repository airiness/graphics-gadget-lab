#pragma once

#include "AssetPreparationTracker.h"
#include "Lab/LabSessionBase.h"
#include "Graphics/PostProcess/PostProcessDebug.h"

#include <entt/entity/entity.hpp>

namespace gglab
{
	class TemporalAALabSession final : public LabSessionBase
	{
	public:
		explicit TemporalAALabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~TemporalAALabSession() override = default;

		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_LoadingProgress; }
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept override;

		static LabId GetId() noexcept;
		static LabDescriptor GetDescriptor() noexcept;
		static std::unique_ptr<LabSessionBase> Create(
			const LabSessionCreateInfo& createInfo) noexcept;

	private:
		void ApplyImmediateParameters() noexcept override;
		void RebuildScene() noexcept override;
		void OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept override;
		void BuildScene() noexcept;
		void BuildLighting() noexcept;
		void ApplySelectedPreviewSelection() noexcept;
		void RequestPreviewRefresh() noexcept;

		AssetPreparationTracker m_AssetPreparation;
		LoadingProgress m_LoadingProgress{};
		PostProcessDebugSelection m_PreviousPreviewSelection{};
		PostProcessDebugTap m_SelectedTap = PostProcessDebugTap::TemporalHistoryWeight;
		entt::entity m_MovingEntity = entt::null;
		float m_ElapsedSeconds = 0.0f;
		uint32_t m_LastCameraCutSerial = 0;
		bool m_EnableCameraInput = false;
		bool m_AnimateObject = true;
		bool m_OrbitCamera = false;
		bool m_ContinuousFovZoom = false;
		bool m_FixtureConfigured = false;
		bool m_IsEntered = false;
	};
}
