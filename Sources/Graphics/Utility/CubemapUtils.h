#pragma once
#include "Core/Math/MathTypes.h"
#include "Graphics/GraphicsTypes.h"

#include <algorithm>
#include <array>

namespace gglab
{
	[[nodiscard]] constexpr std::array<CubemapFace, CubemapFaceCount> GetCubemapFaceOrder() noexcept
	{
		return
		{
			CubemapFace::PositiveX,
			CubemapFace::NegativeX,
			CubemapFace::PositiveY,
			CubemapFace::NegativeY,
			CubemapFace::PositiveZ,
			CubemapFace::NegativeZ,
		};
	}

	[[nodiscard]] inline Vector3 GetCubemapFaceDirection(CubemapFace face) noexcept
	{
		switch (face)
		{
		case CubemapFace::PositiveX: return Vector3::UnitX;
		case CubemapFace::NegativeX: return -Vector3::UnitX;
		case CubemapFace::PositiveY: return Vector3::UnitY;
		case CubemapFace::NegativeY: return -Vector3::UnitY;
		case CubemapFace::PositiveZ: return Vector3::UnitZ;
		case CubemapFace::NegativeZ: return -Vector3::UnitZ;
		default: return Vector3::UnitZ;
		}
	}

	[[nodiscard]] inline Vector3 GetCubemapFaceUp(CubemapFace face) noexcept
	{
		// Left-handed capture basis. This keeps generated cubemap faces aligned with D3D TextureCube sampling.
		switch (face)
		{
		case CubemapFace::PositiveY: return -Vector3::UnitZ;
		case CubemapFace::NegativeY: return Vector3::UnitZ;
		default: return Vector3::UnitY;
		}
	}

	[[nodiscard]] inline Matrix BuildCubemapViewMatrix(CubemapFace face, const Vector3& origin) noexcept
	{
		const Vector3 direction = GetCubemapFaceDirection(face);
		const Vector3 up = GetCubemapFaceUp(face);
		return DirectX::XMMatrixLookAtLH(origin, origin + direction, up);
	}

	[[nodiscard]] inline Matrix BuildCubemapProjectionMatrix(float nearZ, float farZ) noexcept
	{
		nearZ = std::max(0.0001f, nearZ);
		farZ = std::max(nearZ + 0.0001f, farZ);
		return DirectX::XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2, 1.0f, nearZ, farZ);
	}
}
