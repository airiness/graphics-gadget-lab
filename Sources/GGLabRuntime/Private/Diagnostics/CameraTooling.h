#pragma once

#include "GGLabRuntime/Graphics/CameraTooling.h"
#include <cstddef>

namespace gglab
{
	class CameraRig;

	class CameraTooling final : public CameraToolingViewBase, public CameraToolingControlBase
	{
	public:
		explicit CameraTooling(CameraRig& rig) noexcept : m_Rig(rig) {}
		[[nodiscard]] CameraToolingSnapshot GetCameras() const override;
		bool SetActiveCamera(uint64_t id) noexcept override;
		bool SetDisplayCamera(uint64_t id) noexcept override;
		[[nodiscard]] uint64_t AddDebugCamera() noexcept override;
		bool RemoveCamera(uint64_t id) noexcept override;
		bool SetCamera(uint64_t id, const CameraEditSettings& settings) noexcept override;
		bool SetController(uint64_t id, const CameraControllerSettings& settings) noexcept override;
		bool ResetVelocity(uint64_t id) noexcept override;
		bool SetFrustum(uint64_t id, bool show, const Color& color) noexcept override;
		bool SetRenderViewEnabled(uint64_t id, bool enabled) noexcept override;
		bool SetVisibilityMode(uint64_t id, RenderViewVisibilityMode mode) noexcept override;

	private:
		[[nodiscard]] size_t FindIndex(uint64_t id) const noexcept;
		CameraRig& m_Rig;
	};
}
