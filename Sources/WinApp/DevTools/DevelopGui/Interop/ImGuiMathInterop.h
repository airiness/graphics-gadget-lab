#pragma once
#include "GGLabRuntime/Core/Math/Color.h"
#include "GGLabRuntime/Core/Math/Vector.h"

#include <imgui.h>

namespace gglab::devtools::interop
{
	[[nodiscard]] constexpr ImVec2 ToImGui(const Vector2& value) noexcept
	{
		return ImVec2(value.m_X, value.m_Y);
	}

	[[nodiscard]] constexpr Vector2 FromImGui(const ImVec2& value) noexcept
	{
		return Vector2(value.x, value.y);
	}

	[[nodiscard]] constexpr ImVec4 ToImGui(const Vector4& value) noexcept
	{
		return ImVec4(value.m_X, value.m_Y, value.m_Z, value.m_W);
	}

	[[nodiscard]] constexpr Vector4 VectorFromImGui(const ImVec4& value) noexcept
	{
		return Vector4(value.x, value.y, value.z, value.w);
	}

	[[nodiscard]] constexpr ImVec4 ToImGui(const Color& value) noexcept
	{
		return ImVec4(value.m_R, value.m_G, value.m_B, value.m_A);
	}

	[[nodiscard]] constexpr Color ColorFromImGui(const ImVec4& value) noexcept
	{
		return Color(value.x, value.y, value.z, value.w);
	}
}
