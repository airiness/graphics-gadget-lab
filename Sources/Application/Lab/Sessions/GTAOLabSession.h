#pragma once

#include "Application/AssetPreparationTracker.h"
#include "Application/Lab/LabSessionBase.h"
#include "Graphics/PostProcess/PostProcessDebug.h"

namespace gglab
{
	class GTAOLabSession final : public LabSessionBase
	{
	public:
		explicit GTAOLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~GTAOLabSession() override = default;

		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_LoadingProgress; }
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		void OnEnter() noexcept override;
		void OnExit() noexcept override;
		void Update(float deltaTime) noexcept override;
		void OnResize(uint32_t width, uint32_t height) noexcept override;
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
		void ApplyCameraPreset() noexcept;
		void RequestSelectedPreview() noexcept;

		PostProcessDebugTap m_SelectedTap = PostProcessDebugTap::GTAORawAO;
		PostProcessDebugSelection m_PreviousPreviewSelection{};
		AssetPreparationTracker m_AssetPreparation;
		LoadingProgress m_LoadingProgress{};
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		uint64_t m_PreviewUpdateCountOnEnter = 0;
		float m_FovDegrees = 50.0f;
		float m_NearPlane = 0.05f;
		float m_FarPlane = 1000.0f;
		bool m_EnableCameraInput = false;
		bool m_FixtureConfigured = false;
	};
}
