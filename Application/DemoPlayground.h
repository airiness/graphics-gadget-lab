#pragma once
#include "DemoBase.h"
#include "World.h"

namespace gglab
{
	class Camera;
	class RenderPipelineBase;

	class DemoPlayground : public DemoBase
	{
	public:
		DemoPlayground() noexcept;
		~DemoPlayground() override = default;

		void OnEnter() noexcept override;
		void OnExit() noexcept override;

		void Update() noexcept override;

		World& GetWorld() noexcept override { return m_World; }
		Camera& GetCamera() noexcept override { return *m_Camera; }
		RenderPipelineBase& GetRenderPipeline() noexcept override { return *m_RenderPipeline; }

	private:
		void InitializeScene() noexcept override;

	private:
		World m_World;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
	};
}