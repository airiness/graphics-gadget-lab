#pragma once
#include "GGLabFoundation/Async/ProgressChannel.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Core/Hash/KeyHash.h"
#include "GGLabRuntime/Core/Math/BoundingVolumes.h"
#include "GGLabRuntime/Core/Math/Color.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/StringId.h"
#include "GGLabFoundation/Base/TypedIndex.h"
#include "GGLabRuntime/Graphics/Asset/ArtifactContentDigest.h"
#include "GGLabRuntime/Graphics/Asset/AssetLifecycleTypes.h"
#include "GGLabRuntime/Graphics/Asset/ModelTypes.h"
#include "GGLabRuntime/Graphics/Asset/TextureImportTypes.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"
#include "GGLabRuntime/Graphics/MaterialTypes.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <string_view>
#include <tuple>
#include <vector>

namespace gglab
{
	enum class CommonRSRootParamIndex : uint32_t
	{
		SceneCB = 0,   // b0
		DrawConstants, // DrawParameters root constants, b1
		PassConstants, // Pass-specific root constants, b2
		ObjectSB,	   // g_Objects, t1
		MaterialSB,	   // g_Materials, t2
		ViewSB,		   // g_Views, t3
		LightSB,	   // g_Lights, t4

		Count
	};

	enum class LightType : uint32_t
	{
		Directional,
		Spot,
		Point,
	};

	enum class InputLayoutID : uint32_t
	{
		P3,			  // Position(3)
		P3T2,		  // Position(3), TexCoord(2)
		P3N3,		  // Position(3), Normal(3)
		P3N3T2,		  // Position(3), Normal(3), TexCoord(2)
		P3N3T2T2Tan4, // Position(3), Normal(3), TexCoord0(2), TexCoord1(2), Tangent(4)
		P3C4,		  // Position(3), Color(4)
		MeshPositionUVs, // Position/UV view of P3N3T2T2Tan4; declared inputs depend on shader format.

		None,
		Count = None
	};
}
