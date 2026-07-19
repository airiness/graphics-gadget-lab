#include "Core/Precompiled.h"
#include "Graphics/Asset/ModelImportArtifact.h"
#include "Core/Hash/Sha256.h"

#include <bit>

namespace gglab
{
	namespace
	{
		class ArtifactDigestWriter final
		{
		public:
			ArtifactDigestWriter() noexcept : m_Succeeded(m_Builder.IsValid()) {}

			void U8(uint8_t value) noexcept { m_Succeeded &= m_Builder.AddU8(value); }
			void U16(uint16_t value) noexcept { m_Succeeded &= m_Builder.AddU16(value); }
			void U32(uint32_t value) noexcept { m_Succeeded &= m_Builder.AddU32(value); }
			void U64(uint64_t value) noexcept { m_Succeeded &= m_Builder.AddU64(value); }
			void Bool(bool value) noexcept { U8(value ? 1u : 0u); }
			void Float(float value) noexcept { U32(std::bit_cast<uint32_t>(value)); }
			void String(std::string_view value) noexcept
			{
				U64(static_cast<uint64_t>(value.size()));
				m_Succeeded &= m_Builder.AddStringUtf8(value);
			}
			void Bytes(std::span<const std::byte> value) noexcept
			{
				U64(static_cast<uint64_t>(value.size()));
				m_Succeeded &= m_Builder.AddBytes(value);
			}

			[[nodiscard]] ArtifactContentDigest Finish() noexcept
			{
				if (!m_Succeeded)
				{
					return {};
				}
				ArtifactContentDigest digest{};
				digest.m_Value = m_Builder.Finish().m_Value;
				return digest;
			}

		private:
			Sha256Builder m_Builder;
			bool m_Succeeded = false;
		};

		void AddVector(ArtifactDigestWriter& writer, const Vector2& value) noexcept
		{
			writer.Float(value.m_X);
			writer.Float(value.m_Y);
		}

		void AddVector(ArtifactDigestWriter& writer, const Vector3& value) noexcept
		{
			writer.Float(value.m_X);
			writer.Float(value.m_Y);
			writer.Float(value.m_Z);
		}

		void AddVector(ArtifactDigestWriter& writer, const Vector4& value) noexcept
		{
			writer.Float(value.m_X);
			writer.Float(value.m_Y);
			writer.Float(value.m_Z);
			writer.Float(value.m_W);
		}

		void AddColor(ArtifactDigestWriter& writer, const Color& value) noexcept
		{
			writer.Float(value.m_R);
			writer.Float(value.m_G);
			writer.Float(value.m_B);
			writer.Float(value.m_A);
		}

		void AddMaterialBinding(
			ArtifactDigestWriter& writer,
			const MaterialTextureBinding& binding) noexcept
		{
			writer.U32(binding.m_TextureId.Value());
			writer.U32(binding.m_SamplerId.Value());
			writer.U32(binding.m_TexCoordIndex);
		}

		void AddMaterialProperties(
			ArtifactDigestWriter& writer,
			const MaterialProperties& properties) noexcept
		{
			AddMaterialBinding(writer, properties.m_BaseColorBinding);
			AddMaterialBinding(writer, properties.m_EmissiveBinding);
			AddMaterialBinding(writer, properties.m_MetallicRoughnessBinding);
			AddMaterialBinding(writer, properties.m_NormalBinding);
			AddMaterialBinding(writer, properties.m_OcclusionBinding);
			AddColor(writer, properties.m_BaseColor);
			AddColor(writer, properties.m_EmissiveColor);
			writer.Float(properties.m_MetallicFactor);
			writer.Float(properties.m_RoughnessFactor);
			writer.Float(properties.m_NormalScale);
			writer.Float(properties.m_OcclusionStrength);
			writer.U32(static_cast<uint32_t>(properties.m_Flags));
			writer.U32(static_cast<uint32_t>(properties.m_AlphaMode));
			writer.U32(static_cast<uint32_t>(properties.m_AlphaCutoffMode));
			writer.Float(properties.m_AlphaCutoff);
			writer.U32(static_cast<uint32_t>(properties.m_DebugView));
		}

		void AddSampler(ArtifactDigestWriter& writer, const SamplerKey& sampler) noexcept
		{
			writer.U8(static_cast<uint8_t>(sampler.m_Filter));
			writer.U8(static_cast<uint8_t>(sampler.m_AddressU));
			writer.U8(static_cast<uint8_t>(sampler.m_AddressV));
			writer.U8(static_cast<uint8_t>(sampler.m_AddressW));
			writer.Float(sampler.m_MipLODBias);
			writer.U32(sampler.m_MaxAnisotropy);
			writer.U32(static_cast<uint32_t>(sampler.m_CompareOp));
			for (float color : sampler.m_BorderColor)
			{
				writer.Float(color);
			}
			writer.Float(sampler.m_MinLOD);
			writer.Float(sampler.m_MaxLOD);
		}

		void AddTextureData(
			ArtifactDigestWriter& writer,
			const TextureAssetData& textureData) noexcept
		{
			writer.U32(static_cast<uint32_t>(textureData.m_ResourceFormat));
			writer.U32(static_cast<uint32_t>(textureData.m_ViewFormat));
			writer.U32(static_cast<uint32_t>(textureData.m_SrvDimension));
			writer.U32(textureData.m_Extent.m_Width);
			writer.U32(textureData.m_Extent.m_Height);
			writer.U32(textureData.m_Extent.m_Depth);
			writer.U16(textureData.m_ArraySize);
			writer.U16(textureData.m_MipLevels);
			writer.U8(static_cast<uint8_t>(textureData.m_ColorSpace));
			writer.U64(static_cast<uint64_t>(textureData.m_Subresources.size()));
			for (const TextureAssetSubresource& subresource : textureData.m_Subresources)
			{
				writer.U64(subresource.m_DataOffset);
				writer.U64(subresource.m_DataSize);
				writer.U64(subresource.m_RowPitch);
				writer.U64(subresource.m_SlicePitch);
				writer.U32(subresource.m_Width);
				writer.U32(subresource.m_Height);
				writer.U32(subresource.m_Depth);
				writer.U32(subresource.m_MipLevel);
				writer.U32(subresource.m_ArraySlice);
			}
			writer.Bytes(textureData.m_Pixels);
		}
	}

	uint64_t ModelImportArtifact::GetAllocatedBytes() const noexcept
	{
		uint64_t bytes = sizeof(ModelImportArtifact) +
			static_cast<uint64_t>(m_Model.m_CanonicalPath.native().capacity()) *
				sizeof(std::filesystem::path::value_type) +
			static_cast<uint64_t>(m_Model.m_Name.capacity()) +
			static_cast<uint64_t>(m_Model.m_Textures.capacity()) * sizeof(ImportedTexture) +
			static_cast<uint64_t>(m_Model.m_Materials.capacity()) * sizeof(ImportedMaterial) +
			static_cast<uint64_t>(m_Model.m_Meshes.capacity()) * sizeof(ImportedMesh) +
			static_cast<uint64_t>(m_Model.m_MeshInstances.capacity()) * sizeof(ImportedModelMesh);
		for (const ImportedTexture& texture : m_Model.m_Textures)
		{
			bytes += static_cast<uint64_t>(texture.m_CanonicalPath.native().capacity()) *
				sizeof(std::filesystem::path::value_type);
			bytes += static_cast<uint64_t>(texture.m_Data.m_Pixels.capacity()) * sizeof(std::byte);
			bytes += static_cast<uint64_t>(texture.m_Data.m_Subresources.capacity()) *
				sizeof(TextureAssetSubresource);
		}
		for (const ImportedMaterial& material : m_Model.m_Materials)
		{
			bytes += static_cast<uint64_t>(material.m_Name.capacity());
		}
		for (const ImportedMesh& mesh : m_Model.m_Meshes)
		{
			bytes += static_cast<uint64_t>(mesh.m_Name.capacity());
			bytes += static_cast<uint64_t>(mesh.m_Vertices.capacity()) * sizeof(Vertex);
			bytes += static_cast<uint64_t>(mesh.m_Indices.capacity()) * sizeof(uint32_t);
		}
		return bytes;
	}

	ArtifactContentDigest ComputeModelImportArtifactContentDigest(
		const ImportedModel& model) noexcept
	{
		if (model.m_Meshes.empty())
		{
			return {};
		}

		ArtifactDigestWriter writer;
		writer.String(model.m_CanonicalPath.generic_string());
		writer.String(model.m_Name);
		writer.U32(static_cast<uint32_t>(model.m_Type));

		writer.U64(static_cast<uint64_t>(model.m_Textures.size()));
		for (const ImportedTexture& texture : model.m_Textures)
		{
			writer.String(texture.m_CanonicalPath.generic_string());
			writer.U32(static_cast<uint32_t>(texture.m_ImportSettings.m_Semantic));
			writer.U8(static_cast<uint8_t>(texture.m_ImportSettings.m_MipPolicy));
			writer.U32(static_cast<uint32_t>(texture.m_Semantic));
			AddTextureData(writer, texture.m_Data);
		}

		writer.U64(static_cast<uint64_t>(model.m_Materials.size()));
		for (const ImportedMaterial& material : model.m_Materials)
		{
			writer.String(material.m_Name);
			AddMaterialProperties(writer, material.m_Properties);
			for (const ImportedMaterialTextureBinding& binding : material.m_TextureBindings)
			{
				writer.U32(binding.m_TextureIndex);
				AddSampler(writer, binding.m_SamplerKey);
				writer.U32(binding.m_TexCoordIndex);
			}
		}

		writer.U64(static_cast<uint64_t>(model.m_Meshes.size()));
		for (const ImportedMesh& mesh : model.m_Meshes)
		{
			writer.String(mesh.m_Name);
			writer.U32(mesh.m_MaterialIndex);
			AddVector(writer, mesh.m_Sphere.m_Center);
			writer.Float(mesh.m_Sphere.m_Radius);
			AddVector(writer, mesh.m_Aabb.m_Center);
			AddVector(writer, mesh.m_Aabb.m_Extents);
			writer.Bool(mesh.m_HasBounds);
			writer.U64(static_cast<uint64_t>(mesh.m_Vertices.size()));
			for (const Vertex& vertex : mesh.m_Vertices)
			{
				AddVector(writer, vertex.m_Position);
				AddVector(writer, vertex.m_Normal);
				AddVector(writer, vertex.m_TexCoord0);
				AddVector(writer, vertex.m_TexCoord1);
				AddVector(writer, vertex.m_Tangent);
			}
			writer.U64(static_cast<uint64_t>(mesh.m_Indices.size()));
			for (uint32_t index : mesh.m_Indices)
			{
				writer.U32(index);
			}
		}

		writer.U64(static_cast<uint64_t>(model.m_MeshInstances.size()));
		for (const ImportedModelMesh& instance : model.m_MeshInstances)
		{
			writer.U32(instance.m_MeshIndex);
			writer.U32(instance.m_MaterialIndex);
			for (const auto& row : instance.m_LocalTransform.m_M)
			{
				for (float value : row)
				{
					writer.Float(value);
				}
			}
		}

		ArtifactContentDigest digest = writer.Finish();
		if (!digest.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to compute a SHA-256 model import artifact digest.");
		}
		return digest;
	}

	ModelImportArtifactHandle CreateModelImportArtifact(ImportedModel&& model) noexcept
	{
		ArtifactContentDigest digest = ComputeModelImportArtifactContentDigest(model);
		if (!digest.IsValid())
		{
			return {};
		}
		return std::make_shared<const ModelImportArtifact>(ModelImportArtifact{
			.m_Model = std::move(model),
			.m_ContentDigest = digest,
		});
	}
}
