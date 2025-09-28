#pragma once
#include <SimpleMath.h>
using namespace DirectX::SimpleMath;

namespace gglab::color
{
	inline constexpr Color White = Color(1.f, 1.f, 1.f, 1.f);
	inline constexpr Color Black = Color(0.f, 0.f, 0.f, 1.f);
	inline constexpr Color Red = Color(1.f, 0.f, 0.f, 1.f);
	inline constexpr Color Green = Color(0.f, 1.f, 0.f, 1.f);
	inline constexpr Color Blue = Color(0.f, 0.f, 1.f, 1.f);
	inline constexpr Color Yellow = Color(1.f, 1.f, 0.f, 1.f);
	inline constexpr Color Cyan = Color(0.f, 1.f, 1.f, 1.f);
	inline constexpr Color Magenta = Color(1.f, 0.f, 1.f, 1.f);
	inline constexpr Color Transparent = Color(0.f, 0.f, 0.f, 0.f);
	inline constexpr Color Gray = Color(0.5f, 0.5f, 0.5f, 1.f);
	inline constexpr Color DarkGray = Color(0.25f, 0.25f, 0.25f, 1.f);
	inline constexpr Color LightGray = Color(0.75f, 0.75f, 0.75f, 1.f);
	inline constexpr Color Orange = Color(1.f, 0.5f, 0.f, 1.f);
	inline constexpr Color Purple = Color(0.5f, 0.f, 0.5f, 1.f);
	inline constexpr Color Brown = Color(0.6f, 0.4f, 0.2f, 1.f);
	inline constexpr Color Pink = Color(1.f, 0.75f, 0.8f, 1.f);
	inline constexpr Color Gold = Color(1.f, 0.84f, 0.f, 1.f);
	inline constexpr Color Silver = Color(0.75f, 0.75f, 0.75f, 1.f);
	inline constexpr Color Bronze = Color(0.8f, 0.5f, 0.2f, 1.f);
}