#pragma once
#include "Application/Demo/DemoBase.h"
#include "Core/World.h"
#include "Graphics/CameraRig.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"

namespace gglab
{
	class Camera;
	class CameraController;
	class RenderPipelineBase;

	class DemoPlayground : public DemoBase
	{
	public:
		explicit DemoPlayground(const DemoCreateInfo& createInfo) noexcept;
		~DemoPlayground() override = default;

		std::string_view GetName() const noexcept override { return "Demo.Playground"; }

		void OnEnter() noexcept override;
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void OnExit() noexcept override;

		void Update() noexcept override;

		World& GetWorld() noexcept override { return m_World; }
		Camera& GetCamera() noexcept override { return m_CameraRig.GetActiveCamera(); }
		CameraController& GetCameraController() noexcept override
		{
			return m_CameraRig.GetActiveCameraController();
		}
		CameraRig& GetCameraRig() noexcept override { return m_CameraRig; }
		RenderPipelineBase& GetRenderPipeline() noexcept override { return *m_RenderPipeline; }

	private:
		void InitializeScene() noexcept;

	private:
		DemoServices m_Services{};
		World m_World;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		CameraRig m_CameraRig;
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
	};
}
