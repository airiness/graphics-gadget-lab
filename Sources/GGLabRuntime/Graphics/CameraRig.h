#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Math/Color.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/GraphicsTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	class DebugDrawContext;

	class CameraRig
	{
	public:
		struct CameraSlot
		{
			std::string m_Name;
			Camera* m_Camera = nullptr;
			CameraController* m_Controller = nullptr;
			std::unique_ptr<Camera> m_OwnedCamera;
			std::unique_ptr<CameraController> m_OwnedController;
			Color m_FrustumColor = Color::Gold;
			RenderViewID m_RenderViewId = RenderViewID::Unknown;
			RenderViewVisibilityMode m_VisibilityMode = RenderViewVisibilityMode::Self;
			bool m_ShowFrustum = false;
			bool m_EnableRenderView = false;
			bool m_IsDebug = false;
		};

		struct EffectiveDisplayView
		{
			const CameraSlot* m_CameraSlot = nullptr;
			RenderViewID m_ViewId = RenderViewID::Unknown;

			[[nodiscard]] bool IsValid() const noexcept
			{
				return m_CameraSlot != nullptr && m_CameraSlot->m_Camera != nullptr &&
					   m_ViewId != RenderViewID::Unknown;
			}
		};

		CameraRig() noexcept = default;
		GGLAB_DELETE_COPYABLE(CameraRig);
		GGLAB_DEFAULT_MOVABLE(CameraRig);
		~CameraRig() = default;

		void AttachMainCamera(Camera& camera, CameraController& controller) noexcept;
		void OnResize(uint32_t width, uint32_t height) noexcept;
		void SubmitDebugDraw(DebugDrawContext& debugDraw) const noexcept;

		[[nodiscard]] size_t GetCameraCount() const noexcept { return m_Cameras.size(); }
		[[nodiscard]] size_t GetActiveCameraIndex() const noexcept { return m_ActiveCameraIndex; }
		void SetActiveCameraIndex(size_t index) noexcept;
		[[nodiscard]] RenderViewID GetDisplayViewId() const noexcept { return m_DisplayViewId; }
		bool SetDisplayViewId(RenderViewID viewId) noexcept;

		[[nodiscard]] EffectiveDisplayView ResolveEffectiveDisplayView() const noexcept;

		[[nodiscard]] CameraSlot* GetCameraSlot(size_t index) noexcept;
		[[nodiscard]] const CameraSlot* GetCameraSlot(size_t index) const noexcept;
		[[nodiscard]] CameraSlot* GetActiveCameraSlot() noexcept;
		[[nodiscard]] const CameraSlot* GetActiveCameraSlot() const noexcept;
		[[nodiscard]] CameraSlot* GetMainCameraSlot() noexcept;
		[[nodiscard]] const CameraSlot* GetMainCameraSlot() const noexcept;
		[[nodiscard]] CameraSlot* FindRenderViewSlot(RenderViewID viewId) noexcept;
		[[nodiscard]] const CameraSlot* FindRenderViewSlot(RenderViewID viewId) const noexcept;

		[[nodiscard]] Camera& GetMainCamera() noexcept;
		[[nodiscard]] const Camera& GetMainCamera() const noexcept;
		[[nodiscard]] Camera& GetActiveCamera() noexcept;
		[[nodiscard]] const Camera& GetActiveCamera() const noexcept;
		[[nodiscard]] CameraController& GetActiveCameraController() noexcept;

		size_t AddDebugCameraFromActive() noexcept;
		bool RemoveCamera(size_t index) noexcept;
		bool SetDebugRenderViewEnabled(size_t index, bool enabled) noexcept;

	private:
		[[nodiscard]] std::string MakeDebugCameraName() noexcept;
		[[nodiscard]] RenderViewID AcquireDebugRenderViewId() const noexcept;

		std::vector<CameraSlot> m_Cameras;
		size_t m_ActiveCameraIndex = 0;
		uint32_t m_NextDebugCameraIndex = 1;
		RenderViewID m_DisplayViewId = RenderViewID::Main;
	};
}
