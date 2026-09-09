#pragma once

#include "GGLabRuntime/Core/Math/Color.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/CameraControllerSettings.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gglab
{
	struct CameraEditSettings
	{
		Vector3 m_Position = Vector3::Zero;
		float m_Yaw = 0.0f;
		float m_Pitch = 0.0f;
		float m_Fov = 60.0f;
		float m_Near = 0.01f;
		float m_Far = 1000.0f;
		float m_ExposureCompensationEV = 0.0f;
	};

	struct CameraToolingObservation
	{
		// Process-local identity, never a pointer, index or persisted camera identifier.
		uint64_t m_Id = 0;
		std::string m_Name;
		CameraEditSettings m_Settings;
		std::optional<CameraControllerSettings> m_Controller;
		Color m_FrustumColor = Color::Gold;
		RenderViewID m_RenderViewId = RenderViewID::Unknown;
		RenderViewVisibilityMode m_VisibilityMode = RenderViewVisibilityMode::Self;
		bool m_ShowFrustum = false;
		bool m_EnableRenderView = false;
		bool m_IsDebug = false;
		float m_Aspect = 1.0f;
		Vector3 m_Forward = Vector3::Forward;
		Vector3 m_Right = Vector3::UnitX;
		Vector3 m_Up = Vector3::UnitY;
		Matrix m_ViewMatrix = Matrix::Identity;
		Matrix m_ProjMatrix = Matrix::Identity;
	};

	struct CameraToolingSnapshot
	{
		std::vector<CameraToolingObservation> m_Cameras;
		uint64_t m_ActiveCameraId = 0;
		RenderViewID m_DisplayViewId = RenderViewID::Main;

		[[nodiscard]] const CameraToolingObservation* FindCamera(uint64_t id) const noexcept
		{
			for (const auto& camera : m_Cameras)
			{
				if (camera.m_Id == id) return &camera;
			}
			return nullptr;
		}
	};

	// Interfaces are borrowed only during synchronous owner-thread tooling draw.
	// Observations own their values; IDs grant no lifetime and must be revalidated by the owner.
	class CameraToolingViewBase
	{
	public:
		virtual ~CameraToolingViewBase() = default;
		[[nodiscard]] virtual CameraToolingSnapshot GetCameras() const = 0;
	};

	class CameraToolingControlBase
	{
	public:
		virtual ~CameraToolingControlBase() = default;
		virtual bool SetActiveCamera(uint64_t id) noexcept = 0;
		virtual bool SetDisplayCamera(uint64_t id) noexcept = 0;
		[[nodiscard]] virtual uint64_t AddDebugCamera() noexcept = 0;
		virtual bool RemoveCamera(uint64_t id) noexcept = 0;
		virtual bool SetCamera(uint64_t id, const CameraEditSettings& settings) noexcept = 0;
		virtual bool SetController(uint64_t id, const CameraControllerSettings& settings) noexcept = 0;
		virtual bool ResetVelocity(uint64_t id) noexcept = 0;
		virtual bool SetFrustum(uint64_t id, bool show, const Color& color) noexcept = 0;
		virtual bool SetRenderViewEnabled(uint64_t id, bool enabled) noexcept = 0;
		virtual bool SetVisibilityMode(uint64_t id, RenderViewVisibilityMode mode) noexcept = 0;
	};
}
