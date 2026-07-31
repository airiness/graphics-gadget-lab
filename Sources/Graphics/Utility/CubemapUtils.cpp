#include "Core/Precompiled.h"
#include "Graphics/Utility/CubemapUtils.h"

namespace gglab
{
	Vector3 GetCubemapFaceDirection(CubemapFace face) noexcept
	{
		switch (face)
		{
		case CubemapFace::PositiveX:
			return Vector3::UnitX;
		case CubemapFace::NegativeX:
			return -Vector3::UnitX;
		case CubemapFace::PositiveY:
			return Vector3::UnitY;
		case CubemapFace::NegativeY:
			return -Vector3::UnitY;
		case CubemapFace::PositiveZ:
			return Vector3::UnitZ;
		case CubemapFace::NegativeZ:
			return -Vector3::UnitZ;
		default:
			return Vector3::UnitZ;
		}
	}

	Vector3 GetCubemapFaceUp(CubemapFace face) noexcept
	{
		switch (face)
		{
		case CubemapFace::PositiveY:
			return -Vector3::UnitZ;
		case CubemapFace::NegativeY:
			return Vector3::UnitZ;
		default:
			return Vector3::UnitY;
		}
	}

	Vector3 GetCubemapFaceRight(CubemapFace face) noexcept
	{
		return GetCubemapFaceUp(face).Cross(GetCubemapFaceDirection(face)).Normalized();
	}

	CubemapFaceBasis GetCubemapFaceBasis(CubemapFace face) noexcept
	{
		const Vector3 direction = GetCubemapFaceDirection(face);
		const Vector3 up = GetCubemapFaceUp(face);
		return {
			.m_Direction = direction,
			.m_Up = up,
			.m_Right = up.Cross(direction).Normalized(),
		};
	}

	Vector3 CubemapFaceUvToDirection(CubemapFace face, const Vector2& uv) noexcept
	{
		const CubemapFaceBasis basis = GetCubemapFaceBasis(face);
		return (basis.m_Direction + (uv.m_X * 2.0f - 1.0f) * basis.m_Right -
			(uv.m_Y * 2.0f - 1.0f) * basis.m_Up)
			.Normalized();
	}
}
