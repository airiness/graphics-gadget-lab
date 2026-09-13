#pragma once
#include <cstdint>

namespace gglab
{
	// RenderViewID definition
	enum class RenderViewID : uint32_t
	{
		Main,
		DirectionalShadow,
		DebugCamera0,
		DebugCamera1,
		DebugCamera2,

		Count,
		Unknown = Count
	};

	[[nodiscard]] constexpr bool IsDebugCameraRenderViewID(RenderViewID viewId) noexcept
	{
		return viewId == RenderViewID::DebugCamera0 || viewId == RenderViewID::DebugCamera1 ||
			viewId == RenderViewID::DebugCamera2;
	}

	enum class RenderViewVisibilityMode : uint8_t
	{
		Self,
		MainCamera,
		IntersectionWithMainCamera,
		None,
	};

	// RenderBucket definition
	enum class RenderBucket : uint32_t
	{
		Opaque,
		AlphaTest,
		Transparent,

		Count
	};
}
