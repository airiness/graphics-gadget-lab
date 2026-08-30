#pragma once
#include "Demo/DemoTypes.h"
#include "LoadingProgress.h"
#include "GGLabFoundation/Base/CoreMacros.h"

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
		virtual void OnResize(uint32_t, uint32_t) noexcept {}
		virtual void OnExit() noexcept {}
		virtual void Update() noexcept = 0;
		virtual void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
		{
			GGLAB_UNUSED(feedback);
		}

		virtual World& GetWorld() noexcept = 0;
		virtual Camera& GetCamera() noexcept = 0;
		virtual CameraController& GetCameraController() noexcept = 0;
		virtual CameraRig& GetCameraRig() noexcept = 0;
		virtual const ViewRenderProfile& GetViewRenderProfile() const noexcept = 0;
		virtual uint32_t GetTemporalSessionSerial() const noexcept { return 0; }

		virtual RenderPipelineBase& GetRenderPipeline() noexcept = 0;
	};
}
