#pragma once
#include "Demo/DemoBase.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/PostProcess/ViewRenderSettings.h"

namespace gglab
{
	class Camera;
	class CameraController;
	class RenderPipelineBase;

	class DemoLoadingShell final : public DemoBase
	{
	public:
		explicit DemoLoadingShell(const DemoCreateInfo& createInfo) noexcept;
		~DemoLoadingShell() override;

		std::string_view GetName() const noexcept override { return "Demo.LoadingShell"; }
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void Update() noexcept override;

		World& GetWorld() noexcept override { return m_World; }
		Camera& GetCamera() noexcept override { return m_CameraRig.GetActiveCamera(); }
		CameraController& GetCameraController() noexcept override
		{
			return m_CameraRig.GetActiveCameraController();
		}
		CameraRig& GetCameraRig() noexcept override { return m_CameraRig; }
		const ViewRenderProfile& GetViewRenderProfile() const noexcept override
		{
			return m_ViewRenderProfile;
		}
		RenderPipelineBase& GetRenderPipeline() noexcept override { return *m_RenderPipeline; }

	private:
		World m_World;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		CameraRig m_CameraRig;
		ViewRenderProfile m_ViewRenderProfile{};
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
	};
}
