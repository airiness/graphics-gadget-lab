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

		virtual void OnEnter() noexcept {}
		virtual void OnResize(uint32_t width, uint32_t height) noexcept {}
		virtual void OnExit() noexcept {}

		virtual void Update()  noexcept = 0;

		virtual World& GetWorld() noexcept = 0;
		virtual Camera& GetCamera() noexcept = 0;

		virtual RenderPipelineBase& GetRenderPipeline() noexcept = 0;
	};
}