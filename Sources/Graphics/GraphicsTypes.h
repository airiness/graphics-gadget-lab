#pragma once
#include "Core/Math/BoundingVolumes.h"
#include "Core/Math/Color.h"
#include "Core/Math/Matrix.h"
#include "Core/StringId.h"
#include "Core/TypedIndex.h"
#include "Core/EnumFlags.h"
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIResource.h"
#include "Core/Utility/TypeUtils.h"

#include <cstdint>
#include <limits>

namespace gglab
{
	enum class CommonRSRootParamIndex : uint32_t
	{
		SceneCB = 0,	// b0
		DrawConstants,	// DrawParameters root constants, b1
		PassConstants,	// Pass-specific root constants, b2
		ObjectSB,		// g_Objects, t1
		MaterialSB,		// g_Materials, t2
		ViewSB,			// g_Views, t3
		LightSB,		// g_Lights, t4

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

	inline constexpr uint32_t CubemapFaceCount = 6u;
	enum class CubemapFace : uint8_t
	{
		// Matches the D3D TextureCube array-slice order.
		PositiveX,
		NegativeX,
		PositiveY,
		NegativeY,
		PositiveZ,
		NegativeZ,

		Count
	};
	static_assert(static_cast<uint32_t>(CubemapFace::Count) == CubemapFaceCount);

	enum class MaterialFlags : uint32_t
	{
		None = 0u,
		DoubleSided = 1u << 0,
	};
	GGLAB_ENUM_FLAGS(MaterialFlags);

	enum class MaterialDebugView : uint32_t
	{
		Lit,
		BaseColor,
		Metallic,
		Roughness,
		Normal,
	};

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
		P3N3T2T2Tan4,	// Position(3), Normal(3), TexCoord0(2), TexCoord1(2), Tangent(4)
		P3C4,			// Position(3), Color(4)

		None,
		Count = None
	};

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
		return viewId == RenderViewID::DebugCamera0 ||
			viewId == RenderViewID::DebugCamera1 ||
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
	inline constexpr MeshID::ValueType ReservedMeshCount = 8u;

	// MaterialID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(MaterialID, uint32_t);
	inline constexpr MaterialID ProceduralPrimitiveMaterialID{ 0u };
	inline constexpr MaterialID ProceduralCubeMaterialID = ProceduralPrimitiveMaterialID;
	inline constexpr MaterialID::ValueType ReservedMaterialCount = 8u;

	class RuntimeMaterialKey
	{
	public:
		RuntimeMaterialKey() noexcept = default;
		explicit RuntimeMaterialKey(std::string_view name) noexcept : m_Id(name) {}
		explicit constexpr RuntimeMaterialKey(uint64_t value) noexcept : m_Id(value) {}

		[[nodiscard]] bool IsValid() const noexcept { return m_Id.Value() != 0; }
		[[nodiscard]] uint64_t Value() const noexcept { return m_Id.Value(); }
		friend constexpr auto operator<=>(const RuntimeMaterialKey&, const RuntimeMaterialKey&) = default;

	private:
		StringID m_Id{};
	};

	enum class RenderMaterialDomain : uint8_t
	{
		Asset,
		Runtime,
	};

	struct RenderMaterialKey
	{
		static RenderMaterialKey FromAsset(MaterialID id) noexcept
		{
			return {
				.m_Value = static_cast<uint64_t>(id.Value()),
				.m_Domain = RenderMaterialDomain::Asset,
			};
		}

		static RenderMaterialKey FromRuntime(RuntimeMaterialKey key) noexcept
		{
			return {
				.m_Value = key.Value(),
				.m_Domain = RenderMaterialDomain::Runtime,
			};
		}

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Domain == RenderMaterialDomain::Asset ?
				m_Value != MaterialID::InvalidValue : m_Value != 0;
		}

		friend constexpr auto operator<=>(const RenderMaterialKey&, const RenderMaterialKey&) = default;

		uint64_t m_Value = MaterialID::InvalidValue;
		RenderMaterialDomain m_Domain = RenderMaterialDomain::Asset;
	};

	// ModelID
	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(ModelID, uint32_t);
	inline constexpr ModelID ProceduralCubeModelID{ 0u };
	inline constexpr ModelID ProceduralSphereModelID{ 1u };
	inline constexpr ModelID::ValueType ReservedModelCount = 8u;

	struct Texture
	{
		TextureID m_Id{};
		RHITextureViewHandle m_Srv{};
		TextureSemantic m_Semantic = TextureSemantic::GenericColor;
		StringID m_Name{};
		RHITextureHandle m_Texture;
		bool m_IsUploaded = false;
	};

	struct Sampler
	{
		SamplerID m_Id{};
		RHISamplerHandle m_Sampler{};
	};

	struct MaterialTextureBinding
	{
		TextureID m_TextureId{};
		SamplerID m_SamplerId{};
		uint32_t m_TexCoordIndex = 0;
	};

	struct MaterialProperties
	{
		MaterialTextureBinding m_BaseColorBinding{};
		MaterialTextureBinding m_EmissiveBinding{};
		MaterialTextureBinding m_MetallicRoughnessBinding{};
		MaterialTextureBinding m_NormalBinding{};
		MaterialTextureBinding m_OcclusionBinding{};

		Color m_BaseColor = Color::White;
		Color m_EmissiveColor = Color::Black;
		float m_MetallicFactor = 0.0f;
		float m_RoughnessFactor = 1.0f;
		float m_NormalScale = 1.0f;
		float m_OcclusionStrength = 1.0f;

		MaterialFlags m_Flags = MaterialFlags::None;
		AlphaMode m_AlphaMode = AlphaMode::Opaque;
		AlphaCutoffMode m_AlphaCutoffMode = AlphaCutoffMode::Disabled;
		float m_AlphaCutoff = 0.5f;
		MaterialDebugView m_DebugView = MaterialDebugView::Lit;
	};

	struct Material : MaterialProperties
	{
		MaterialID m_Id{};

		StringID m_Name{};
	};

	struct Mesh
	{
		MeshID m_Id{};

		bool m_IsUploaded = false;
		bool m_HasBounds = false;

		StringID m_Name{};

		RHIBufferOwner m_VertexBuffer;
		RHIBufferOwner m_IndexBuffer;

		RHIVertexBufferBinding m_VertexBufferBinding{};
		RHIIndexBufferBinding m_IndexBufferBinding{};

		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;

		math::Sphere m_Sphere{};
		math::Aabb m_Aabb{};
	};

	struct ModelMesh
	{
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};
		Matrix m_LocalTransform = Matrix::Identity;
	};

	struct Model
	{
		ModelID m_Id{};
		StringID m_Name;
		ModelType m_Type = ModelType::Invalid;
		std::vector<ModelMesh> m_MeshInstance;
	};
}

namespace std
{
	template<>
	struct hash<gglab::RuntimeMaterialKey>
	{
		size_t operator()(gglab::RuntimeMaterialKey key) const noexcept
		{
			return std::hash<uint64_t>{}(key.Value());
		}
	};

	template<>
	struct hash<gglab::RenderMaterialKey>
	{
		size_t operator()(const gglab::RenderMaterialKey& key) const noexcept
		{
			const size_t valueHash = std::hash<uint64_t>{}(key.m_Value);
			const size_t domainHash = std::hash<uint8_t>{}(static_cast<uint8_t>(key.m_Domain));
			return valueHash ^ (domainHash + 0x9e3779b9u + (valueHash << 6) + (valueHash >> 2));
		}
	};
}
