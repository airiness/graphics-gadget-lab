#pragma once
#include <cstdint>
#include <limits>
#include "Color.h"
#include "StringId.h"
#include "TypedIndex.h"
#include "EnumFlags.h"
#include "DX12Buffer.h"
#include "DX12DescriptorTypes.h"

namespace gglab
{
	class DX12Texture;
	class DX12Buffer;

	enum class CommonRSRootParamIndex : uint32_t
	{
		FrameCB = 0,	// b0
		ObjectCB,		// g_ObjectIndex, b1
		ObjectSB,		// g_Objects, t1
		MaterialSB,		// g_Materials, t2

		Count
	};

	enum class ModelType : uint32_t
	{
		Invalid,
		GlTF,
		Procedural,
	};

	enum class LightType : uint32_t
	{
		Directional,
		Spot,
		Point,
	};

	enum class MaterialFlags : uint32_t
	{
		None = 0u,
		DoubleSided = 1u << 0,
	};
	GGLAB_ENUM_FLAGS(MaterialFlags);

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

	// RootSignatureID
	GGLAB_DEFINE_TYPED_INDEX(RootSignatureID, uint64_t);

	// ShaderID
	GGLAB_DEFINE_TYPED_INDEX(ShaderID, uint32_t);

	// TextureID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(TextureID, uint32_t);
	enum class ReservedTextureIDIndex: uint32_t
	{
		BaseColorWhite,
		MissingTextureChecker,
		NormalFlat,
		DefaultMetallicRoughness,
		OcclusionWhite,
		EmissiveBlack,
		ErrorRed,
		UVTest,

		Count,

		ReservedCount = 64u
	};
	static_assert(utils::ToIndex(ReservedTextureIDIndex::Count) < utils::ToIndex(ReservedTextureIDIndex::ReservedCount),
		"ReservedTextureID::Count must be less than ReservedTextureID::ReservedCount");
	inline constexpr TextureID::ValueType ReservedTextureCount = utils::ToIndex(ReservedTextureIDIndex::ReservedCount);
	constexpr TextureID ToTextureId(ReservedTextureIDIndex index) noexcept
	{
		return TextureID{ utils::ToIndex(index) };
	}
	constexpr bool IsReservedTextureId(TextureID id) noexcept
	{
		return id.IsValid() && id.Value() < ReservedTextureCount;
	}

	// MeshID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MeshID, uint32_t);
	inline constexpr MeshID ProceduralCubeMeshID{ 0u };
	inline constexpr MeshID::ValueType ReservedMeshCount = 5u;

	// MaterialID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MaterialID, uint32_t);
	inline constexpr MaterialID ProceduralCubeMaterialID{ 0u };
	inline constexpr MaterialID::ValueType ReservedMaterialCount = 5u;

	// ModelID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(ModelID, uint32_t);
	inline constexpr ModelID ProceduralCubeModelID{ 0u };
	inline constexpr ModelID::ValueType ReservedModelCount = 5u;

	struct Texture
	{
		TextureID m_Id{};
		DX12DescriptorID m_DescriptorId{};
		bool m_IsUploaded = false;
		StringID m_Name{};
		std::unique_ptr<DX12Texture> m_Texture;
	};

	struct Material
	{
		MaterialID m_Id{};
		StringID m_Name{};

		TextureID m_BaseColorTex{};
		TextureID m_MetallicRoughnessTex{};
		TextureID m_NormalTex{};
		TextureID m_OcclusionTex{};
		TextureID m_EmissiveTex{};

		Color m_BaseColor = color::White;
		float m_MetallicFactor = 0.0f;
		float m_RoughnessFactor = 1.0f;
		float m_NormalScale = 1.0f;
		float m_OcclusionStrength = 1.0f;
		Color m_EmissiveColor = color::Black;

		MaterialFlags m_Flags;
	};

	struct Mesh
	{
		MeshID m_Id{};
		StringID m_Name{};

		std::unique_ptr<DX12Buffer> m_VertexBuffer;
		std::unique_ptr<DX12Buffer> m_IndexBuffer;

		D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView{};
		D3D12_INDEX_BUFFER_VIEW m_IndexBufferView{};

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		bool m_IsUploaded = false;

	};

	struct ModelMesh
	{
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};
	};

	struct Model
	{
		ModelID m_Id{};
		StringID m_Name;
		ModelType m_Type = ModelType::Invalid;
		std::vector<ModelMesh> m_MeshInstance;
	};
}