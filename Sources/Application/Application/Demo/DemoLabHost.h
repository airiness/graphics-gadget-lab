#pragma once
#include "Application/Demo/DemoBase.h"
#include "Application/Lab/LabRuntime.h"

#include <span>

namespace gglab
{
	class DemoLabHost final : public DemoBase
	{
	public:
		DemoLabHost(const DemoCreateInfo& createInfo, const LabId& startupLab,
			std::span<const LabRegistration> labRegistrations) noexcept;
		~DemoLabHost() override = default;

		std::string_view GetName() const noexcept override { return "Demo.LabHost"; }
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override;
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;
		std::optional<LoadingProgress> GetActiveLoadingProgress() const noexcept override;

		void OnEnter() noexcept override;
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void OnExit() noexcept override;
		void Update() noexcept override;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept override;

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
		RenderPipelineBase& GetRenderPipeline() noexcept override
		{
			return m_Runtime.GetRenderPipeline();
		}

		LabRuntime& GetLabRuntime() noexcept { return m_Runtime; }
		const LabRuntime& GetLabRuntime() const noexcept { return m_Runtime; }
		bool IsValid() const noexcept
		{
			return m_RegistrationsValid && m_StartupLab.IsValid() &&
				m_Runtime.GetCatalog().Find(m_StartupLab);
		}

	private:
		LabId m_StartupLab;
		LabRuntime m_Runtime;
		bool m_RegistrationsValid = true;
	};
}
