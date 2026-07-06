#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/DevelopGuiProjectionUtils.h"
#include "Core/Math/MathFunctions.h"

#include <algorithm>

namespace gglab::devtools
{
	namespace
	{
		[[nodiscard]] bool ProjectWorldPositionToNdc(
			const RenderView& view,
			const Vector3& worldPosition,
			Vector3& outNdc) noexcept
		{
			if (view.m_Width == 0 || view.m_Height == 0)
			{
				return false;
			}

			Vector4 clipPosition;
			Vector3::Transform(worldPosition, view.m_ViewProj, clipPosition);
			if (!math::IsFinite(clipPosition.m_X) ||
				!math::IsFinite(clipPosition.m_Y) ||
				!math::IsFinite(clipPosition.m_Z) ||
				!math::IsFinite(clipPosition.m_W) ||
				clipPosition.m_W <= 1.0e-5f)
			{
				return false;
			}

			const float invW = 1.0f / clipPosition.m_W;
			outNdc.m_X = clipPosition.m_X * invW;
			outNdc.m_Y = clipPosition.m_Y * invW;
			outNdc.m_Z = clipPosition.m_Z * invW;
			return
				math::IsFinite(outNdc.m_X) &&
				math::IsFinite(outNdc.m_Y) &&
				math::IsFinite(outNdc.m_Z);
		}

		[[nodiscard]] ImVec2 NdcToScreen(
			const Vector3& ndc,
			const ImGuiViewport& viewport) noexcept
		{
			return ImVec2(
				viewport.Pos.x + (ndc.m_X * 0.5f + 0.5f) * viewport.Size.x,
				viewport.Pos.y + (0.5f - ndc.m_Y * 0.5f) * viewport.Size.y);
		}
	}

	bool ProjectWorldPositionToScreen(
		const RenderView& view,
		const Vector3& worldPosition,
		const ImGuiViewport& viewport,
		ImVec2& outScreenPosition) noexcept
	{
		Vector3 ndc;
		if (!ProjectWorldPositionToNdc(view, worldPosition, ndc) ||
			ndc.m_X < -1.0f || ndc.m_X > 1.0f ||
			ndc.m_Y < -1.0f || ndc.m_Y > 1.0f ||
			ndc.m_Z < 0.0f || ndc.m_Z > 1.0f)
		{
			return false;
		}

		outScreenPosition = NdcToScreen(ndc, viewport);
		return true;
	}

	bool ProjectWorldPositionToScreen(
		const RenderView& view,
		const Vector3& worldPosition,
		ImVec2& outScreenPosition) noexcept
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (viewport == nullptr)
		{
			return false;
		}

		return ProjectWorldPositionToScreen(view, worldPosition, *viewport, outScreenPosition);
	}

	bool ProjectWorldPositionToScreenClamped(
		const RenderView& view,
		const Vector3& worldPosition,
		const ImGuiViewport& viewport,
		ImVec2& outScreenPosition,
		bool& outClamped) noexcept
	{
		outClamped = false;

		Vector3 ndc;
		if (!ProjectWorldPositionToNdc(view, worldPosition, ndc) ||
			ndc.m_Z < 0.0f || ndc.m_Z > 1.0f)
		{
			return false;
		}

		const Vector3 clampedNdc(
			std::clamp(ndc.m_X, -1.0f, 1.0f),
			std::clamp(ndc.m_Y, -1.0f, 1.0f),
			ndc.m_Z);
		outClamped =
			clampedNdc.m_X != ndc.m_X ||
			clampedNdc.m_Y != ndc.m_Y;

		outScreenPosition = NdcToScreen(clampedNdc, viewport);
		return true;
	}

	bool ProjectWorldPositionToScreenClamped(
		const RenderView& view,
		const Vector3& worldPosition,
		ImVec2& outScreenPosition,
		bool& outClamped) noexcept
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (viewport == nullptr)
		{
			outClamped = false;
			return false;
		}

		return ProjectWorldPositionToScreenClamped(
			view,
			worldPosition,
			*viewport,
			outScreenPosition,
			outClamped);
	}
}
