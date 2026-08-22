#pragma once
#include "Demo/DemoBase.h"
#include "Core/World.h"
#include "Graphics/CameraRig.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"

namespace gglab
{
	class Camera;
	class CameraController;

	class StartDemo final : public DemoBase
	{
	public:
		explicit StartDemo(const DemoCreateInfo& createInfo) noexcept;
		~StartDemo() override = default;

		std::string_view GetName() const noexcept override { return "Demo.Start"; }
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override
		{
			return m_LoadingProgress;
		}
		void CommitPrepare() noexcept override;
		void CancelPrepare() noexcept override;

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
		const ViewRenderProfile& GetViewRenderProfile() const noexcept override
		{
			return m_ViewRenderProfile;
		}
		RenderPipelineBase& GetRenderPipeline() noexcept override { return *m_RenderPipeline; }

	private:
		void BuildScene() noexcept;

		DemoServices m_Services{};
		World m_World;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		CameraRig m_CameraRig;
		ViewRenderProfile m_ViewRenderProfile{};
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
		entt::entity m_AnimatedSphereEntity = entt::null;
		entt::entity m_AnimatedCubeEntity = entt::null;
		float m_AnimationTime = 0.0f;
		LoadingProgress m_LoadingProgress{};
		bool m_PreviousSkyboxEnabled = true;
		bool m_HasSkyboxOverride = false;
	};
}
