#pragma once
#include <cstdint>

namespace gglab
{
	// RenderViewID definition
	enum class RenderViewID : uint32_t
	{
		Main,
		DirectionalShadow, // Shadow view kind; not a camera queue or GPU view index.
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

	// Pixel extent of one resolution domain.
	struct ViewExtent
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		bool operator==(const ViewExtent&) const noexcept = default;
	};

	// Resolution domains of a camera view. Geometry is
	// rasterized and shaded at the render extent; the temporal output, post-temporal
	// composition and the back buffer use the display extent.
	struct ViewResolution
	{
		ViewExtent m_Render{};
		ViewExtent m_Display{};

		[[nodiscard]] constexpr bool IsNative() const noexcept
		{
			return m_Render == m_Display;
		}

		bool operator==(const ViewResolution&) const noexcept = default;
	};

	// Renders at the display extent. Until temporal upscaling defines a render scale this
	// is the only resolution a view receives, so both domains are equal.
	[[nodiscard]] constexpr ViewResolution ResolveNativeViewResolution(
		ViewExtent display) noexcept
	{
		return ViewResolution{ .m_Render = display, .m_Display = display };
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
