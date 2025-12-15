#pragma once
#include <string_view>

namespace gglab
{
	class World;
	class Camera;
	class RenderPipelineBase;

	class DemoBase
	{
	public:
		DemoBase() noexcept = default;
		virtual ~DemoBase() = default;

		virtual std::string_view GetName() const noexcept = 0;

		virtual void OnEnter() {}
		virtual void OnResize() {}
		virtual void OnExit() {}

		virtual void Update() = 0;

		virtual World& GetWorld() noexcept = 0;
		virtual Camera& GetCamera() noexcept = 0;

		virtual RenderPipelineBase& GetRenderPipeline() noexcept = 0;
	};
}