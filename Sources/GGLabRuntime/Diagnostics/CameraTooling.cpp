#include "Diagnostics/CameraTooling.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"

namespace gglab
{
	size_t CameraTooling::FindIndex(uint64_t id) const noexcept
	{
		for (size_t index = 0; index < m_Rig.GetCameraCount(); ++index)
		{
			const auto* slot = m_Rig.GetCameraSlot(index);
			if (id != 0 && slot && slot->m_Id == id && slot->m_Camera) return index;
		}
		return m_Rig.GetCameraCount();
	}

	CameraToolingSnapshot CameraTooling::GetCameras() const
	{
		CameraToolingSnapshot result;
		result.m_DisplayViewId = m_Rig.GetDisplayViewId();
		for (size_t index = 0; index < m_Rig.GetCameraCount(); ++index)
		{
			const auto* slot = m_Rig.GetCameraSlot(index);
			if (!slot || !slot->m_Camera) continue;
			const auto& camera = *slot->m_Camera;
			CameraToolingObservation observation;
			observation.m_Id = slot->m_Id;
			observation.m_Name = slot->m_Name;
			observation.m_Settings = { camera.GetPosition(), camera.GetYaw(), camera.GetPitch(),
				camera.GetFov(), camera.GetNear(), camera.GetFar(), camera.GetExposureCompensationEV() };
			if (slot->m_Controller) observation.m_Controller = slot->m_Controller->GetParams();
			observation.m_FrustumColor = slot->m_FrustumColor;
			observation.m_RenderViewId = slot->m_RenderViewId;
			observation.m_VisibilityMode = slot->m_VisibilityMode;
			observation.m_ShowFrustum = slot->m_ShowFrustum;
			observation.m_EnableRenderView = slot->m_EnableRenderView;
			observation.m_IsDebug = slot->m_IsDebug;
			observation.m_Aspect = camera.GetAspect();
			observation.m_Forward = camera.GetForward();
			observation.m_Right = camera.GetRight();
			observation.m_Up = camera.GetUp();
			observation.m_ViewMatrix = camera.GetViewMatrix();
			observation.m_ProjMatrix = camera.GetProjMatrix();
			result.m_Cameras.push_back(observation);
			if (index == m_Rig.GetActiveCameraIndex()) result.m_ActiveCameraId = slot->m_Id;
		}
		return result;
	}

	bool CameraTooling::SetActiveCamera(uint64_t id) noexcept
	{
		const size_t index = FindIndex(id);
		if (index == m_Rig.GetCameraCount()) return false;
		m_Rig.SetActiveCameraIndex(index);
		return true;
	}

	bool CameraTooling::SetDisplayCamera(uint64_t id) noexcept
	{
		const auto* slot = m_Rig.GetCameraSlot(FindIndex(id));
		return slot && m_Rig.SetDisplayViewId(slot->m_RenderViewId);
	}

	uint64_t CameraTooling::AddDebugCamera() noexcept
	{
		const auto* active = m_Rig.GetActiveCameraSlot();
		if (!active || !active->m_Camera) return 0;
		const auto index = m_Rig.AddDebugCameraFromActive();
		return m_Rig.GetCameraSlot(index)->m_Id;
	}

	bool CameraTooling::RemoveCamera(uint64_t id) noexcept
	{
		return m_Rig.RemoveCamera(FindIndex(id));
	}

	bool CameraTooling::SetCamera(uint64_t id, const CameraEditSettings& settings) noexcept
	{
		auto* slot = m_Rig.GetCameraSlot(FindIndex(id));
		if (!slot) return false;
		auto& camera = *slot->m_Camera;
		camera.SetPosition(settings.m_Position);
		camera.SetYawPitch(settings.m_Yaw, settings.m_Pitch);
		camera.SetFov(Camera::ClampFov(settings.m_Fov));
		const float nearZ = Camera::ClampNear(settings.m_Near);
		camera.SetNearFar(nearZ, Camera::ClampFar(nearZ, settings.m_Far));
		camera.SetExposureCompensationEV(Camera::ClampExposureCompensationEV(settings.m_ExposureCompensationEV));
		camera.Update();
		return true;
	}

	bool CameraTooling::SetController(uint64_t id, const CameraControllerSettings& settings) noexcept
	{
		auto* slot = m_Rig.GetCameraSlot(FindIndex(id));
		if (!slot || !slot->m_Controller) return false;
		slot->m_Controller->SetParams(settings);
		return true;
	}

	bool CameraTooling::ResetVelocity(uint64_t id) noexcept
	{
		auto* slot = m_Rig.GetCameraSlot(FindIndex(id));
		if (!slot || !slot->m_Controller) return false;
		slot->m_Controller->ResetVelocity();
		return true;
	}

	bool CameraTooling::SetFrustum(uint64_t id, bool show, const Color& color) noexcept
	{
		auto* slot = m_Rig.GetCameraSlot(FindIndex(id));
		if (!slot) return false;
		slot->m_ShowFrustum = show;
		slot->m_FrustumColor = color;
		return true;
	}

	bool CameraTooling::SetRenderViewEnabled(uint64_t id, bool enabled) noexcept
	{
		return m_Rig.SetDebugRenderViewEnabled(FindIndex(id), enabled);
	}

	bool CameraTooling::SetVisibilityMode(uint64_t id, RenderViewVisibilityMode mode) noexcept
	{
		auto* slot = m_Rig.GetCameraSlot(FindIndex(id));
		if (!slot || !slot->m_IsDebug || !slot->m_EnableRenderView) return false;
		switch (mode)
		{
		case RenderViewVisibilityMode::Self:
		case RenderViewVisibilityMode::MainCamera:
		case RenderViewVisibilityMode::IntersectionWithMainCamera:
		case RenderViewVisibilityMode::None:
			slot->m_VisibilityMode = mode;
			return true;
		default:
			return false;
		}
	}
}
