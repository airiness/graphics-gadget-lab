#pragma once
#include "Application/Demo/DemoBase.h"
#include "Core/World.h"
#include "Graphics/CameraRig.h"
#include "Graphics/AssetManager.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"

#include <filesystem>
#include <vector>

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
		void BeginPrepare() noexcept override;
		void TickPrepare() noexcept override;
		LoadingProgress GetPreparationProgress() const noexcept override { return m_LoadingProgress; }
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
		RenderPipelineBase& GetRenderPipeline() noexcept override { return *m_RenderPipeline; }

	private:
		struct PendingModel
		{
			std::filesystem::path m_Path;
			Vector3 m_Position{};
			Vector3 m_Rotation{};
			Vector3 m_Scale = Vector3::One;
			ModelID m_ModelId{};
		};

		void CommitScene() noexcept;

	private:
		DemoServices m_Services{};
		AssetOwnerScope m_AssetOwnerScope;
		World m_World;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<CameraController> m_CameraController;
		CameraRig m_CameraRig;
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
		std::vector<PendingModel> m_PendingModels;
		LoadingProgress m_LoadingProgress{};
	};
}
