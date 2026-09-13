#pragma once
#include "GGLabFoundation/Base/TypedIndex.h"

#include <cstdint>

namespace gglab
{
	// ResourceIndex for render graph.
	GGLAB_DEFINE_TYPED_INDEX(ResourceIndex, uint32_t);

	// RootSignatureID
	GGLAB_DEFINE_TYPED_INDEX(RootSignatureID, uint64_t);

	// ShaderID
	GGLAB_DEFINE_TYPED_INDEX(ShaderID, uint32_t);

	// TextureID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(TextureID, uint32_t);

	// SamplerID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(SamplerID, uint32_t);

	// MeshID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MeshID, uint32_t);
	inline constexpr MeshID ProceduralCubeMeshID{ 0u };
	inline constexpr MeshID ProceduralSphereMeshID{ 1u };
	inline constexpr MeshID ProceduralPlaneMeshID{ 2u };
	inline constexpr MeshID::ValueType ReservedMeshCount = 8u;
	[[nodiscard]] constexpr bool IsReservedMeshId(MeshID id) noexcept
	{
		return id.IsValid() && id.Value() < ReservedMeshCount;
	}

	// MaterialID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MaterialID, uint32_t);
	inline constexpr MaterialID ProceduralPrimitiveMaterialID{ 0u };
	inline constexpr MaterialID ProceduralCubeMaterialID = ProceduralPrimitiveMaterialID;
	inline constexpr MaterialID::ValueType ReservedMaterialCount = 8u;

	// ModelID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(ModelID, uint32_t);
	inline constexpr ModelID ProceduralCubeModelID{ 0u };
	inline constexpr ModelID ProceduralSphereModelID{ 1u };
	inline constexpr ModelID ProceduralPlaneModelID{ 2u };
	inline constexpr ModelID::ValueType ReservedModelCount = 8u;
	[[nodiscard]] constexpr bool IsReservedModelId(ModelID id) noexcept
	{
		return id.IsValid() && id.Value() < ReservedModelCount;
	}
}
