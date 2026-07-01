#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/DevelopGuiProjectionUtils.h"
#include "Core/Utility/MathUtils.h"

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
			if (!utils::IsFinite(clipPosition.x) ||
				!utils::IsFinite(clipPosition.y) ||
				!utils::IsFinite(clipPosition.z) ||
				!utils::IsFinite(clipPosition.w) ||
				clipPosition.w <= 1.0e-5f)
			{
				return false;
			}

			const float invW = 1.0f / clipPosition.w;
			outNdc.x = clipPosition.x * invW;
			outNdc.y = clipPosition.y * invW;
			outNdc.z = clipPosition.z * invW;
			return
				utils::IsFinite(outNdc.x) &&
				utils::IsFinite(outNdc.y) &&
				utils::IsFinite(outNdc.z);
		}

		[[nodiscard]] ImVec2 NdcToScreen(
			const Vector3& ndc,
			const ImGuiViewport& viewport) noexcept
		{
			return ImVec2(
				viewport.Pos.x + (ndc.x * 0.5f + 0.5f) * viewport.Size.x,
				viewport.Pos.y + (0.5f - ndc.y * 0.5f) * viewport.Size.y);
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
			ndc.x < -1.0f || ndc.x > 1.0f ||
			ndc.y < -1.0f || ndc.y > 1.0f ||
			ndc.z < 0.0f || ndc.z > 1.0f)
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
			ndc.z < 0.0f || ndc.z > 1.0f)
		{
			return false;
		}

		const Vector3 clampedNdc(
			std::clamp(ndc.x, -1.0f, 1.0f),
			std::clamp(ndc.y, -1.0f, 1.0f),
			ndc.z);
		outClamped =
			clampedNdc.x != ndc.x ||
			clampedNdc.y != ndc.y;

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
