#pragma once
#include "Graphics/Color.h"
#include "Core/StringId.h"
#include "Core/TypedIndex.h"
#include "Core/EnumFlags.h"
#include "Graphics/DX12/DX12Buffer.h"
#include "Graphics/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Core/Utility/TypeUtils.h"

#include <cstdint>
#include <limits>

#include <SimpleMath.h>

namespace gglab
{
	class DX12Texture;
	class DX12Buffer;

	enum class CommonRSRootParamIndex : uint32_t
	{
		SceneCB = 0,	// b0
		ObjectCB,		// g_ObjectIndex, b1
		ObjectSB,		// g_Objects, t1
		MaterialSB,		// g_Materials, t2
		ViewSB,			// g_Views, t3

		Count
	};

	enum class ModelType : uint32_t
	{
		Invalid,
		GlTF,
		Procedural,
	};

	enum class AlphaMode : uint32_t
	{
		Opaque,
		Mask,
		Blend,
	};

	enum class AlphaCutoffMode : uint32_t
	{
		Disabled,
		AlphaToCoverage,
		AlphaCutoff
	};

	enum class LightType : uint32_t
	{
		Directional,
		Spot,
		Point,
	};

	enum class TextureColorSpace : uint8_t
	{
		Linear,
		SRGB
	};

	enum class TextureSemantic : uint32_t
	{
		BaseColor,
		Emissive,
		Normal,
		MetallicRoughness,
		Occlusion,
		UVTest,
		GenericColor,
		GenericData,
		Unknown
	};

	[[nodiscard]] constexpr TextureColorSpace GetTextureColorSpaceFromSemantic(TextureSemantic semantic) noexcept
	{
		switch (semantic)
		{
		case TextureSemantic::BaseColor:
		case TextureSemantic::Emissive:
		case TextureSemantic::UVTest:
		case TextureSemantic::GenericColor:
			return TextureColorSpace::SRGB;
		default:
			return TextureColorSpace::Linear;
		}
	}

	enum class MaterialFlags : uint32_t
	{
		None = 0u,
		DoubleSided = 1u << 0,
	};
	GGLAB_ENUM_FLAGS(MaterialFlags);

	enum class MaterialTextureSlot : uint32_t
	{
		BaseColor,
		MetallicRoughness,
		Normal,
		Occlusion,
		Emissive,

		Count
	};

	constexpr TextureSemantic GetMaterialTextureSlotSemantic(MaterialTextureSlot slot) noexcept
	{
		switch (slot)
		{
		case MaterialTextureSlot::BaseColor:
			return TextureSemantic::BaseColor;
		case MaterialTextureSlot::MetallicRoughness:
			return TextureSemantic::MetallicRoughness;
		case MaterialTextureSlot::Normal:
			return TextureSemantic::Normal;
		case MaterialTextureSlot::Occlusion:
			return TextureSemantic::Occlusion;
		case MaterialTextureSlot::Emissive:
			return TextureSemantic::Emissive;
		default:
			return TextureSemantic::Unknown;
		}
	}

	enum class InputLayoutID : uint32_t
	{
		P3,				// Position(3)
		P3T2,			// Position(3), TexCoord(2)
		P3N3,			// Position(3), Normal(3)
		P3N3T2,			// Position(3), Normal(3), TexCoord(2)

		// TODO: Add Tangent

		None,
		Count = None
	};

	// RenderViewID definition
	enum class RenderViewID : uint32_t
	{
		Main,

		Count,
		Unknown = Count
	};

	// RenderBucket definition
	enum class RenderBucket : uint32_t
	{
		Opaque,
		AlphaTest,
		Transparent,

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

	// SamplerID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(SamplerID, uint32_t);

	// MeshID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MeshID, uint32_t);
	inline constexpr MeshID ProceduralCubeMeshID{ 0u };
	inline constexpr MeshID::ValueType ReservedMeshCount = 8u;

	// MaterialID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MaterialID, uint32_t);
	inline constexpr MaterialID ProceduralCubeMaterialID{ 0u };
	inline constexpr MaterialID::ValueType ReservedMaterialCount = 8u;

	// ModelID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(ModelID, uint32_t);
	inline constexpr ModelID ProceduralCubeModelID{ 0u };
	inline constexpr ModelID::ValueType ReservedModelCount = 8u;

	struct Texture
	{
		TextureID m_Id{};
		DX12DescriptorID m_DescriptorId{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		StringID m_Name{};
		std::unique_ptr<DX12Texture> m_Texture;
		bool m_IsUploaded = false;
	};

	struct Sampler
	{
		SamplerID m_Id{};
		DX12DescriptorID m_DescriptorId{};
	};

	struct MaterialTextureBinding
	{
		TextureID m_TextureId{};
		SamplerID m_SamplerId{};
		uint32_t m_TexCoordIndex = 0; // TODO: support glTF textureInfo.texCoord.
	};

	struct Material
	{
		MaterialID m_Id{};

		MaterialTextureBinding m_BaseColorBinding{};
		MaterialTextureBinding m_EmissiveBinding{};
		MaterialTextureBinding m_MetallicRoughnessBinding{};
		MaterialTextureBinding m_NormalBinding{};
		MaterialTextureBinding m_OcclusionBinding{};

		Color m_BaseColor = color::White;
		Color m_EmissiveColor = color::Black;
		float m_MetallicFactor = 0.0f;
		float m_RoughnessFactor = 1.0f;
		float m_NormalScale = 1.0f;
		float m_OcclusionStrength = 1.0f;

		MaterialFlags m_Flags = MaterialFlags::None;
		AlphaMode m_AlphaMode = AlphaMode::Opaque;
		AlphaCutoffMode m_AlphaCutoffMode = AlphaCutoffMode::Disabled;

		float m_AlphaCutoff = 0.5f;

		StringID m_Name{};
	};

	struct Mesh
	{
		MeshID m_Id{};

		bool m_IsUploaded = false;
		bool m_HasBounds = false;

		StringID m_Name{};

		std::unique_ptr<DX12Buffer> m_VertexBuffer;
		std::unique_ptr<DX12Buffer> m_IndexBuffer;

		D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView{};
		D3D12_INDEX_BUFFER_VIEW m_IndexBufferView{};

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		DirectX::BoundingSphere m_BoundingSphere{};
		DirectX::BoundingBox m_BoundingBox{};
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
