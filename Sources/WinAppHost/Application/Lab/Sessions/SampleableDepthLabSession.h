#pragma once
#include "AssetPreparationTracker.h"
#include "Lab/LabSessionBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"

namespace gglab
{
	class SampleableDepthLabSession final : public LabSessionBase
	{
	public:
		explicit SampleableDepthLabSession(const LabSessionCreateInfo& createInfo) noexcept;
		~SampleableDepthLabSession() override = default;

		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			return m_LoadingProgress;
		}
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

		bool m_EnableCameraInput = true;
		float m_NearPlane = 0.05f;
		float m_FarPlane = 5000.0f;
		uint32_t m_ViewportWidth = 0;
		uint32_t m_ViewportHeight = 0;
		float m_InitialFarMarkerViewDistance = 0.0f;
		bool m_FixtureConfigured = false;
		PostProcessDebugSelection m_PreviousPreviewSelection{};
		uint64_t m_PreviewUpdateCountOnEnter = 0;
		AssetPreparationTracker m_AssetPreparation;
		LoadingProgress m_LoadingProgress{};
	};
}
