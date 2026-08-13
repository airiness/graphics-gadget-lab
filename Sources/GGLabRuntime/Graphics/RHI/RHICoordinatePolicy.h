#pragma once

#include <cstdint>

namespace gglab
{
	enum class RHIClipDepthRange : uint8_t
	{
		ZeroToOne,
	};

	enum class RHICoordinateOrigin : uint8_t
	{
		UpperLeft,
	};

	enum class RHIFrontFaceDefinition : uint8_t
	{
		AfterViewportTransform,
	};

	struct RHICoordinatePolicy
	{
		RHIClipDepthRange m_ClipDepthRange = RHIClipDepthRange::ZeroToOne;
		RHICoordinateOrigin m_ViewportOrigin = RHICoordinateOrigin::UpperLeft;
		RHICoordinateOrigin m_TextureUVOrigin = RHICoordinateOrigin::UpperLeft;
		RHIFrontFaceDefinition m_FrontFaceDefinition =
			RHIFrontFaceDefinition::AfterViewportTransform;
		bool m_BackendAppliesReversedZ = false;
	};

	inline constexpr RHICoordinatePolicy GGLabCoordinatePolicy{};
}
