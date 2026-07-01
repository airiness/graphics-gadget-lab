#pragma once
#include "Graphics/RenderView.h"

#include <imgui.h>

namespace gglab::devtools
{
	[[nodiscard]] bool ProjectWorldPositionToScreen(
		const RenderView& view,
		const Vector3& worldPosition,
		const ImGuiViewport& viewport,
		ImVec2& outScreenPosition) noexcept;

	[[nodiscard]] bool ProjectWorldPositionToScreen(
		const RenderView& view,
		const Vector3& worldPosition,
		ImVec2& outScreenPosition) noexcept;

	[[nodiscard]] bool ProjectWorldPositionToScreenClamped(
		const RenderView& view,
		const Vector3& worldPosition,
		const ImGuiViewport& viewport,
		ImVec2& outScreenPosition,
		bool& outClamped) noexcept;

	[[nodiscard]] bool ProjectWorldPositionToScreenClamped(
		const RenderView& view,
		const Vector3& worldPosition,
		ImVec2& outScreenPosition,
		bool& outClamped) noexcept;
}
