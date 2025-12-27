#pragma once
#include "DemoBase.h"
#include "World.h"
#include "RenderPipelineBase.h"

namespace gglab
{
	class Camera;
	class RenderPipelineBase;

	class DemoPlayground : public DemoBase
	{
	public:
		DemoPlayground() noexcept;
		~DemoPlayground() override = default;

		std::string_view GetName() const noexcept override { return "Demo.Playground"; }

		void OnEnter() noexcept override;
		void OnResize(uint32_t width, uint32_t height) noexcept override;
		void OnExit() noexcept override;

		void Update() noexcept override;

		World& GetWorld() noexcept override { return m_World; }
		Camera& GetCamera() noexcept override { return *m_Camera; }
		RenderPipelineBase& GetRenderPipeline() noexcept override { return *m_RenderPipeline; }

	private:
		void InitializeScene() noexcept;

	private:
		World m_World;
		std::unique_ptr<Camera> m_Camera;
		std::unique_ptr<RenderPipelineBase> m_RenderPipeline;
	};
}