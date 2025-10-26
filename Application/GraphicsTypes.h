#pragma once
#include <cstdint>
#include <limits>
#include "TypedIndex.h"

namespace gglab
{
	enum class CommonRSRootParamIndex : uint32_t
	{
		GlobalCB = 0,
		ObjectCB,
		MaterialCB,
		TextureDescriptorTable,
		RootParamCount
	};

	enum class ModelType : uint32_t
	{
		ModelType_Invalid,
		ModelType_glTF,
		ModelType_Procedural,
	};

	enum class LightType : uint32_t
	{
		LightType_Directional,
		LightType_Spot,
		LightType_Point,
	};

	enum class InputLayoutId : uint32_t
	{
		P3,				// Position(3)
		P3T2,			// Position(3), TexCoord(2)
		P3N3,			// Position(3), Normal(3)
		P3N3T2,			// Position(3), Normal(3), TexCoord(2)

		Count
	};

	// ResourceIndex for render graph.
	GGLAB_DEFINE_TYPED_INDEX(ResourceIndex, uint32_t);

	// RootSignatureId
	GGLAB_DEFINE_TYPED_INDEX(RootSignatureId, uint64_t);
	
	// ShaderId
	GGLAB_DEFINE_TYPED_INDEX(ShaderId, uint32_t);

	// TextureId
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(TextureId, uint32_t);
	inline constexpr TextureId ReservedTextureID{ 5u };

	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MeshId, uint32_t);
	inline constexpr MeshId ProceduralCubeMeshID{ 0u };
	inline constexpr MeshId ReservedMeshID{ 5u };

	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MaterialId, uint32_t);
	inline constexpr MaterialId ProceduralCubeMaterialID{ 0u };
	inline constexpr MaterialId ReservedMaterialID{ 5u };
}