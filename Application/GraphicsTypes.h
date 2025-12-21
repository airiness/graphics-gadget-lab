#pragma once
#include <cstdint>
#include <limits>
#include "Color.h"
#include "StringId.h"
#include "TypedIndex.h"
#include "EnumFlags.h"
#include "DX12Buffer.h"
#include "DX12Descriptor.h"

namespace gglab
{
	class DX12Texture;
	class DX12Buffer;

	enum class CommonRSRootParamIndex : uint32_t
	{
		FrameCB = 0,	// b0
		ObjectCB,	// g_ObjectIndex, b1
		ObjectSB,	// g_Objects, t1
		MaterialSB,	// g_Materials, t2
		TextureDescriptorTable,	// g_BaseColorTex, t0
		RootParamCount
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

	struct ExternalResourceIndex
	{
		using Rep = ResourceIndex::ValueType;
		enum class Type : Rep
		{
			Texture = 0,
			Buffer = 1,
		};

		static constexpr Rep ExternalBit = (Rep{ 1 } << (std::numeric_limits<Rep>::digits - 1));
		static constexpr Rep TypeBit = (ExternalBit >> 1);

		static constexpr Rep ExternalMask = ExternalBit;
		static constexpr Rep PayloadMask = ~ExternalMask;
		static constexpr Rep TypeMask = TypeBit;
		static constexpr Rep IdMask = PayloadMask & ~TypeMask;

		static constexpr ResourceIndex Base() noexcept { return ResourceIndex(ExternalBit); }

		[[nodiscard]] static constexpr bool IsExternal(ResourceIndex index) noexcept
		{
			return index.IsValid() && ((index.Value() & ExternalMask) != 0);
		}

		[[nodiscard]] static constexpr Rep Payload(ResourceIndex index) noexcept
		{
			GGLAB_ASSERT_MSG(IsExternal(index), "Payload called on non-external ResourceIndex.");
			return (index.Value() & PayloadMask);
		}

		[[nodiscard]] static constexpr Type GetType(ResourceIndex index) noexcept
		{
			GGLAB_ASSERT_MSG(IsExternal(index), "GetType called on non-external ResourceIndex.");
			return ((Payload(index) & TypeMask) != 0) ? Type::Buffer : Type::Texture;
		}

		[[nodiscard]] static constexpr Rep GetId(ResourceIndex index) noexcept
		{
			GGLAB_ASSERT_MSG(IsExternal(index), "GetId called on non-external ResourceIndex.");
			return (Payload(index) & IdMask);
		}

		[[nodiscard]] static constexpr ResourceIndex MakeIndex(Type type, Rep id) noexcept
		{
			GGLAB_ASSERT_MSG((id & ~IdMask) == 0, "External id overflowed.");

			const Rep typeBits = (type == Type::Buffer) ? TypeBit : 0;
			const Rep payload = typeBits | (id & IdMask);
			const Rep value = ExternalBit | payload;

			GGLAB_ASSERT_MSG(value != ResourceIndex::InvalidValue, "External ResourceIndex overflowed to InvalidValue.");
			return ResourceIndex(value);
		}

		[[nodiscard]] static constexpr ResourceIndex MakeTexture(Rep id) noexcept
		{
			return MakeIndex(Type::Texture, id);
		}

		[[nodiscard]] static constexpr ResourceIndex MakeBuffer(Rep id) noexcept
		{
			return MakeIndex(Type::Buffer, id);
		}
	};

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

	GGLAB_DEFINE_TYPED_INDEX_WITH_COUNTER(ModelId, uint32_t);
	inline constexpr ModelId ProceduralCubeModelID{ 0u };
	inline constexpr ModelId ReservedModelID{ 5u };

	struct Texture
	{
		TextureId m_Id{};
		StringId m_Name{};
		std::unique_ptr<DX12Texture> m_Texture;
		DX12Descriptor m_Descriptor{};
		uint32_t m_DescriptorIndex = 0;
		bool m_IsUploaded = false;
	};

	struct Material
	{
		MaterialId m_Id{};
		StringId m_Name{};

		TextureId m_BaseColorTex{};
		TextureId m_MetallicRoughnessTex{};
		TextureId m_NormalTex{};
		TextureId m_OcclusionTex{};
		TextureId m_EmissiveTex{};

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
		MeshId m_Id{};
		StringId m_Name{};

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
		MeshId m_MeshId{};
		MaterialId m_MaterialId{};
	};

	struct Model
	{
		ModelId m_Id{};
		StringId m_Name;
		ModelType m_Type = ModelType::Invalid;
		std::vector<ModelMesh> m_MeshInstance;
	};
}