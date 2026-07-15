#pragma once
#include "Application/Demo/DemoTypes.h"
#include "Application/LoadingProgress.h"
#include "Core/CoreMacros.h"

#include <optional>
#include <string_view>

namespace gglab
{
	class World;
	class Camera;
	class CameraController;
	class CameraRig;
	class RenderPipelineBase;
	struct ViewRenderProfile;

	class DemoBase
	{
	public:
		DemoBase() noexcept = default;
		virtual ~DemoBase() = default;

		virtual std::string_view GetName() const noexcept = 0;

		virtual void BeginPrepare() noexcept {}
		virtual void TickPrepare() noexcept {}
		virtual LoadingProgress GetPreparationProgress() const noexcept
		{
			return LoadingProgress::Ready();
		}
		virtual void CommitPrepare() noexcept {}
		virtual void CancelPrepare() noexcept {}
		virtual std::optional<LoadingProgress> GetActiveLoadingProgress() const noexcept
		{
			return std::nullopt;
		}

		virtual void OnEnter() noexcept {}
		virtual void OnResize(uint32_t width, uint32_t height) noexcept {}
		virtual void OnExit() noexcept {}
		virtual void Update()  noexcept = 0;
		virtual void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
		{
			GGLAB_UNUSED(feedback);
		}

		virtual World& GetWorld() noexcept = 0;
		virtual Camera& GetCamera() noexcept = 0;
		virtual CameraController& GetCameraController() noexcept = 0;
		virtual CameraRig& GetCameraRig() noexcept = 0;
		virtual const ViewRenderProfile& GetViewRenderProfile() const noexcept = 0;

		virtual RenderPipelineBase& GetRenderPipeline() noexcept = 0;
	};
}
