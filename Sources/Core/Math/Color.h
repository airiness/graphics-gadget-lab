#pragma once
#include "Core/Math/Vector.h"

namespace gglab::math
{
	struct Color
	{
		constexpr Color() noexcept = default;
		constexpr explicit Color(float value) noexcept : m_R(value), m_G(value), m_B(value), m_A(value) {}
		constexpr Color(float r, float g, float b, float a) noexcept : m_R(r), m_G(g), m_B(b), m_A(a) {}

		float R() const noexcept { return m_R; }
		float G() const noexcept { return m_G; }
		float B() const noexcept { return m_B; }
		float A() const noexcept { return m_A; }
		constexpr operator Vector4() const noexcept { return Vector4(m_R, m_G, m_B, m_A); }

		float& operator[](size_t index) noexcept { return (&m_R)[index]; }
		const float& operator[](size_t index) const noexcept { return (&m_R)[index]; }

		float m_R = 0.0f;
		float m_G = 0.0f;
		float m_B = 0.0f;
		float m_A = 1.0f;
	};
}
