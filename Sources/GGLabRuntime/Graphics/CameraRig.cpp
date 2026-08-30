#include "Graphics/CameraRig.h"
#include "Core/Math/MathFunctions.h"
#include "Graphics/DebugDraw/DebugDraw.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <utility>

namespace gglab
{
	namespace
	{
		constexpr std::array<RenderViewID, 3> DebugCameraRenderViewIds = {
			RenderViewID::DebugCamera0,
			RenderViewID::DebugCamera1,
			RenderViewID::DebugCamera2,
		};

	}

	void CameraRig::AttachMainCamera(Camera& camera, CameraController& controller) noexcept
	{
		CameraSlot mainSlot{};
		mainSlot.m_Name = "Main Camera";
		mainSlot.m_Camera = &camera;
		mainSlot.m_Controller = &controller;
		mainSlot.m_FrustumColor = Color::Cyan;
		mainSlot.m_RenderViewId = RenderViewID::Main;
		mainSlot.m_VisibilityMode = RenderViewVisibilityMode::Self;
		mainSlot.m_ShowFrustum = false;
		mainSlot.m_EnableRenderView = true;
		mainSlot.m_IsDebug = false;

		if (m_Cameras.empty())
		{
			m_Cameras.push_back(std::move(mainSlot));
		}
		else
		{
			m_Cameras[0] = std::move(mainSlot);
		}
		m_ActiveCameraIndex = std::min(m_ActiveCameraIndex, m_Cameras.size() - 1);
		if (m_DisplayViewId == RenderViewID::Unknown)
		{
			m_DisplayViewId = RenderViewID::Main;
		}
	}

	void CameraRig::OnResize(uint32_t width, uint32_t height) noexcept
	{
		for (CameraSlot& slot : m_Cameras)
		{
			if (slot.m_Camera)
			{
				slot.m_Camera->OnResize(width, height);
			}
		}
	}

	void CameraRig::SubmitDebugDraw(DebugDrawContext& debugDraw) const noexcept
	{
		const StringID channel("Debug.Camera.Frustum");
		for (const CameraSlot& slot : m_Cameras)
		{
			if (!slot.m_Camera || !slot.m_ShowFrustum)
			{
				continue;
			}

			const DebugDrawStyle style{
				.m_Color = slot.m_FrustumColor,
				.m_DepthMode = DebugDrawDepthMode::Always,
				.m_CullingMode = DebugDrawCullingMode::None,
				.m_Channel = channel,
			};
			const Matrix inverseViewProjection =
				math::SafeInverse(slot.m_Camera->GetViewMatrix() * slot.m_Camera->GetProjMatrix());
			const std::array<Vector3, 8> corners =
				math::BuildFrustumCornersFromInverseViewProjection(inverseViewProjection);
			debugDraw.Frustum(corners, style);
			debugDraw.Point(slot.m_Camera->GetPosition(), 0.15f, style);
		}
	}

	void CameraRig::SetActiveCameraIndex(size_t index) noexcept
	{
		if (index < m_Cameras.size())
		{
			m_ActiveCameraIndex = index;
		}
	}

	bool CameraRig::SetDisplayViewId(RenderViewID viewId) noexcept
	{
		if (viewId == RenderViewID::Main)
		{
			m_DisplayViewId = viewId;
			return true;
		}

		const CameraSlot* slot = FindRenderViewSlot(viewId);
		if (!slot || !slot->m_EnableRenderView)
		{
			return false;
		}
		m_DisplayViewId = viewId;
		return true;
	}

	CameraRig::EffectiveDisplayView CameraRig::ResolveEffectiveDisplayView() const noexcept
	{
		const CameraSlot* slot = FindRenderViewSlot(m_DisplayViewId);
		if (slot && slot->m_Camera && slot->m_EnableRenderView)
		{
			return {
				.m_CameraSlot = slot,
				.m_ViewId = m_DisplayViewId,
			};
		}

		const CameraSlot* mainSlot = GetMainCameraSlot();
		return {
			.m_CameraSlot = mainSlot,
			.m_ViewId = mainSlot && mainSlot->m_Camera ? RenderViewID::Main : RenderViewID::Unknown,
		};
	}

	CameraRig::CameraSlot* CameraRig::GetCameraSlot(size_t index) noexcept
	{
		return index < m_Cameras.size() ? &m_Cameras[index] : nullptr;
	}

	const CameraRig::CameraSlot* CameraRig::GetCameraSlot(size_t index) const noexcept
	{
		return index < m_Cameras.size() ? &m_Cameras[index] : nullptr;
	}

	CameraRig::CameraSlot* CameraRig::GetActiveCameraSlot() noexcept
	{
		return GetCameraSlot(m_ActiveCameraIndex);
	}

	const CameraRig::CameraSlot* CameraRig::GetActiveCameraSlot() const noexcept
	{
		return GetCameraSlot(m_ActiveCameraIndex);
	}

	CameraRig::CameraSlot* CameraRig::GetMainCameraSlot() noexcept
	{
		return GetCameraSlot(0);
	}

	const CameraRig::CameraSlot* CameraRig::GetMainCameraSlot() const noexcept
	{
		return GetCameraSlot(0);
	}

	CameraRig::CameraSlot* CameraRig::FindRenderViewSlot(RenderViewID viewId) noexcept
	{
		for (CameraSlot& slot : m_Cameras)
		{
			if (slot.m_RenderViewId == viewId)
			{
				return &slot;
			}
		}
		return nullptr;
	}

	const CameraRig::CameraSlot* CameraRig::FindRenderViewSlot(RenderViewID viewId) const noexcept
	{
		for (const CameraSlot& slot : m_Cameras)
		{
			if (slot.m_RenderViewId == viewId)
			{
				return &slot;
			}
		}
		return nullptr;
	}

	Camera& CameraRig::GetMainCamera() noexcept
	{
		CameraSlot* slot = GetMainCameraSlot();
		GGLAB_ASSERT_NOT_NULL(slot);
		GGLAB_ASSERT_NOT_NULL(slot->m_Camera);
		return *slot->m_Camera;
	}

	const Camera& CameraRig::GetMainCamera() const noexcept
	{
		const CameraSlot* slot = GetMainCameraSlot();
		GGLAB_ASSERT_NOT_NULL(slot);
		GGLAB_ASSERT_NOT_NULL(slot->m_Camera);
		return *slot->m_Camera;
	}

	Camera& CameraRig::GetActiveCamera() noexcept
	{
		CameraSlot* slot = GetActiveCameraSlot();
		GGLAB_ASSERT_NOT_NULL(slot);
		GGLAB_ASSERT_NOT_NULL(slot->m_Camera);
		return *slot->m_Camera;
	}

	const Camera& CameraRig::GetActiveCamera() const noexcept
	{
		const CameraSlot* slot = GetActiveCameraSlot();
		GGLAB_ASSERT_NOT_NULL(slot);
		GGLAB_ASSERT_NOT_NULL(slot->m_Camera);
		return *slot->m_Camera;
	}

	CameraController& CameraRig::GetActiveCameraController() noexcept
	{
		CameraSlot* slot = GetActiveCameraSlot();
		GGLAB_ASSERT_NOT_NULL(slot);
		GGLAB_ASSERT_NOT_NULL(slot->m_Controller);
		return *slot->m_Controller;
	}

	size_t CameraRig::AddDebugCameraFromActive() noexcept
	{
		CameraSlot* sourceSlot = GetActiveCameraSlot();
		if (!sourceSlot || !sourceSlot->m_Camera)
		{
			return m_ActiveCameraIndex;
		}

		CameraController::CreateInfo controllerCreateInfo{};
		if (sourceSlot->m_Controller)
		{
			controllerCreateInfo.m_Params = sourceSlot->m_Controller->GetParams();
		}

		CameraSlot debugSlot{};
		debugSlot.m_Name = MakeDebugCameraName();
		debugSlot.m_OwnedCamera = std::make_unique<Camera>(*sourceSlot->m_Camera);
		debugSlot.m_OwnedController = std::make_unique<CameraController>(controllerCreateInfo);
		debugSlot.m_Camera = debugSlot.m_OwnedCamera.get();
		debugSlot.m_Controller = debugSlot.m_OwnedController.get();
		debugSlot.m_FrustumColor = Color::Gold;
		debugSlot.m_RenderViewId = AcquireDebugRenderViewId();
		debugSlot.m_VisibilityMode = RenderViewVisibilityMode::IntersectionWithMainCamera;
		debugSlot.m_ShowFrustum = true;
		debugSlot.m_EnableRenderView = IsDebugCameraRenderViewID(debugSlot.m_RenderViewId);
		debugSlot.m_IsDebug = true;

		m_Cameras.push_back(std::move(debugSlot));
		m_ActiveCameraIndex = m_Cameras.size() - 1;
		return m_ActiveCameraIndex;
	}

	bool CameraRig::RemoveCamera(size_t index) noexcept
	{
		if (index == 0 || index >= m_Cameras.size())
		{
			return false;
		}

		if (m_Cameras[index].m_RenderViewId == m_DisplayViewId)
		{
			m_DisplayViewId = RenderViewID::Main;
		}

		m_Cameras.erase(m_Cameras.begin() + static_cast<std::ptrdiff_t>(index));
		if (m_Cameras.empty())
		{
			m_ActiveCameraIndex = 0;
		}
		else if (m_ActiveCameraIndex >= m_Cameras.size())
		{
			m_ActiveCameraIndex = m_Cameras.size() - 1;
		}
		else if (m_ActiveCameraIndex > index)
		{
			--m_ActiveCameraIndex;
		}
		return true;
	}

	bool CameraRig::SetDebugRenderViewEnabled(size_t index, bool enabled) noexcept
	{
		CameraSlot* slot = GetCameraSlot(index);
		if (!slot || !slot->m_IsDebug)
		{
			return false;
		}

		if (!enabled)
		{
			if (slot->m_RenderViewId == m_DisplayViewId)
			{
				m_DisplayViewId = RenderViewID::Main;
			}
			slot->m_EnableRenderView = false;
			slot->m_RenderViewId = RenderViewID::Unknown;
			return true;
		}

		if (!IsDebugCameraRenderViewID(slot->m_RenderViewId))
		{
			slot->m_RenderViewId = AcquireDebugRenderViewId();
		}
		slot->m_EnableRenderView = IsDebugCameraRenderViewID(slot->m_RenderViewId);
		return slot->m_EnableRenderView;
	}

	std::string CameraRig::MakeDebugCameraName() noexcept
	{
		return std::format("Debug Camera {}", m_NextDebugCameraIndex++);
	}

	RenderViewID CameraRig::AcquireDebugRenderViewId() const noexcept
	{
		for (const RenderViewID candidate : DebugCameraRenderViewIds)
		{
			bool alreadyUsed = false;
			for (const CameraSlot& slot : m_Cameras)
			{
				if (slot.m_EnableRenderView && slot.m_RenderViewId == candidate)
				{
					alreadyUsed = true;
					break;
				}
			}
			if (!alreadyUsed)
			{
				return candidate;
			}
		}
		return RenderViewID::Unknown;
	}
}
