#pragma once
#include "GGLabRuntime/Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Core/Math/Vector.h"

namespace gglab
{
	struct CubemapFaceBasis
	{
		Vector3 m_Direction;
		Vector3 m_Up;
		Vector3 m_Right;
	};

	// Uses the same face orientation as the shader source Common/Cubemap.hlsli.
	[[nodiscard]] CubemapFaceBasis GetCubemapFaceBasis(CubemapFace face) noexcept;
	[[nodiscard]] Vector3 GetCubemapFaceDirection(CubemapFace face) noexcept;
	[[nodiscard]] Vector3 GetCubemapFaceUp(CubemapFace face) noexcept;
	[[nodiscard]] Vector3 GetCubemapFaceRight(CubemapFace face) noexcept;
	[[nodiscard]] Vector3 CubemapFaceUvToDirection(CubemapFace face, const Vector2& uv) noexcept;
}
