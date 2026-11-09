#pragma once
#include "Demo/DemoBase.h"
#include "Lab/LabRuntime.h"

#include <span>

namespace gglab
{
	class DemoLabHost final : public DemoBase
	{
	public:
		DemoLabHost(const DemoCreateInfo& createInfo, const LabStartupSelection& startupLab,
			std::span<const LabRegistration> labRegistrations) noexcept;
		~DemoLabHost() override = default;

		std::string_view GetName() const noexcept override { return "Demo.LabHost"; }
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override;
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		std::optional<LoadingProgress> GetActiveLoadingProgress() const noexcept override;
		void AppendCaptureReadiness(FrameCaptureReadiness& readiness) const noexcept override;
		std::string GetCaptureContentId() const noexcept override;
		std::optional<FrameCaptureAssetBundle> GetCaptureAssetBundle() const noexcept override
		{
			const auto* session = m_Runtime.GetActiveSession();
			return session ? session->GetCaptureAssetBundle() : std::nullopt;
		}

		void OnEnter() noexcept override;
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void OnExit() noexcept override;
		void Update() noexcept override;
		LabRuntime* GetParameterLabRuntime() noexcept override { return &m_Runtime; }
		std::span<const WorldLightingProfile> GetLightingProfiles() const noexcept override
		{
			const auto* session = m_Runtime.GetActiveSession();
			return session ? session->GetLightingProfiles() : std::span<const WorldLightingProfile>{};
		}
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept override;
		void OnCameraPathFrameApplied(const CameraPath& path, uint32_t frame) noexcept override;

		World& GetWorld() noexcept override { return m_Runtime.GetWorld(); }
		Camera& GetCamera() noexcept override { return m_Runtime.GetCamera(); }
		CameraController& GetCameraController() noexcept override
		{
			return m_Runtime.GetCameraController();
		}
		CameraRig& GetCameraRig() noexcept override { return m_Runtime.GetCameraRig(); }
		const ViewRenderProfile& GetViewRenderProfile() const noexcept override
		{
			return m_Runtime.GetViewRenderProfile();
		}
		uint32_t GetTemporalSessionSerial() const noexcept override
		{
			return m_Runtime.GetTemporalSessionSerial();
		}
		RenderPipelineBase& GetRenderPipeline() noexcept override
		{
			return m_Runtime.GetRenderPipeline();
		}

		LabRuntime& GetLabRuntime() noexcept { return m_Runtime; }
		const LabRuntime& GetLabRuntime() const noexcept { return m_Runtime; }
		bool IsValid() const noexcept
		{
			return m_RegistrationsValid && m_StartupLab.m_Lab.IsValid() &&
				m_Runtime.GetCatalog().Find(m_StartupLab.m_Lab);
		}

	private:
		LabStartupSelection m_StartupLab;
		LabRuntime m_Runtime;
		bool m_RegistrationsValid = true;
	};
}
