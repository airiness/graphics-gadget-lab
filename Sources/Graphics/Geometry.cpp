#include "Core/Precompiled.h"
#include "Graphics/Geometry.h"
#include "Core/Math/MathConstants.h"
#include "Scene/Components.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/SamplerRegistry.h"
#include "Core/World.h"

namespace gglab
{
	namespace primitive
	{
		entt::entity PrimitiveBase::CreatePrimitive(
			const CreateInfo& info,
			ModelID modelId,
			MeshID meshId,
			std::string_view name,
			VertexBuilder buildVertices,
			IndexBuilder buildIndices) noexcept
		{
			GGLAB_ASSERT_NOT_NULL(info.m_AssetManager);
			GGLAB_ASSERT_NOT_NULL(info.m_SamplerRegistry);
			GGLAB_ASSERT_NOT_NULL(info.m_World);

			auto& assetManager = *info.m_AssetManager;
			if (assetManager.GetMaterial(ProceduralPrimitiveMaterialID) == nullptr)
			{
				auto material = std::make_unique<Material>();
				material->m_Id = ProceduralPrimitiveMaterialID;
				material->m_Name = StringID("ProceduralPrimitiveMaterial");
				material->m_BaseColorBinding.m_TextureId =
					ToTextureId(ReservedTextureIDIndex::BaseColorWhite);
				material->m_BaseColorBinding.m_SamplerId =
					info.m_SamplerRegistry->GetPresetSamplerId(SamplerPreset::AnisotropicClamp);
				assetManager.AddProceduralMaterial(std::move(material));
			}

			if (assetManager.GetMesh(meshId) == nullptr)
			{
				auto mesh = std::make_unique<Mesh>();
				mesh->m_Id = meshId;
				mesh->m_Name = StringID(name);
				AssetManager::MeshUploadData uploadData{};
				uploadData.m_VerticesData = buildVertices();
				uploadData.m_IndicesData = buildIndices();
				assetManager.AddProceduralMesh(std::move(mesh), uploadData);
			}

			if (assetManager.GetModel(modelId) == nullptr)
			{
				auto model = std::make_unique<Model>();
				model->m_Id = modelId;
				model->m_Type = ModelType::Procedural;
				model->m_Name = StringID(name);
				model->m_MeshInstance.push_back({
					.m_MeshId = meshId,
					.m_MaterialId = ProceduralPrimitiveMaterialID,
				});
				assetManager.AddProceduralModel(std::move(model));
			}

			auto& registry = info.m_World->GetRegistry();
			const entt::entity entity = registry.create();
			registry.emplace<components::TransformComponent>(entity, info.m_Transform);
			registry.emplace<components::ModelComponent>(entity, modelId);
			if (info.m_MaterialInstance)
			{
				GGLAB_ASSERT_MSG(info.m_MaterialInstance->m_Key.IsValid(),
					"A procedural primitive material instance requires a stable key.");
				registry.emplace<components::MaterialInstanceComponent>(
					entity,
					*info.m_MaterialInstance);
			}
			return entity;
		}

		entt::entity Cube::Create(const CreateInfo& info) noexcept
		{
			return CreatePrimitive(
				info,
				ProceduralCubeModelID,
				ProceduralCubeMeshID,
				"ProceduralCube",
				&GetVerticesData,
				&GetIndicesData);
		}

		std::vector<Vertex> Cube::GetVerticesData() noexcept
		{
			constexpr std::array<std::array<Vector3, VertexCountPerFace>, FaceCount> facePositions =
			{ {
				// Front face (-Z)
				{{{-1.0f, -1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}}},
				// Back face (+Z)
				{{{ 1.0f, -1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {-1.0f, -1.0f,  1.0f}}},
				// Top face (+Y)
				{{{-1.0f,  1.0f, -1.0f}, {-1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f,  1.0f}, { 1.0f,  1.0f, -1.0f}}},
				// Bottom face (-Y)
				{{{-1.0f, -1.0f,  1.0f}, {-1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f, -1.0f}, { 1.0f, -1.0f,  1.0f}}},
				// Left face (-X)
				{{{-1.0f, -1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {-1.0f,  1.0f, -1.0f}, {-1.0f, -1.0f, -1.0f}}},
				// Right face (+X)
				{{{ 1.0f, -1.0f, -1.0f}, { 1.0f,  1.0f, -1.0f}, { 1.0f,  1.0f,  1.0f}, { 1.0f, -1.0f,  1.0f}}},
			} };

			constexpr std::array<Vector2, VertexCountPerFace> texCoords =
			{ {
				{ 0.0f, 1.0f },
				{ 0.0f, 0.0f },
				{ 1.0f, 0.0f },
				{ 1.0f, 1.0f },
			} };

			constexpr std::array<Vector3, FaceCount> faceNormals =
			{ {
				{ 0.0f,  0.0f, -1.0f }, // Front
				{ 0.0f,  0.0f,  1.0f }, // Back
				{ 0.0f,  1.0f,  0.0f }, // Top
				{ 0.0f, -1.0f,  0.0f }, // Bottom
				{-1.0f,  0.0f,  0.0f }, // Left
				{ 1.0f,  0.0f,  0.0f }, // Right
			} };

			constexpr std::array<Vector4, FaceCount> faceTangents =
			{ {
				{ 1.0f, 0.0f,  0.0f, 1.0f }, // Front
				{-1.0f, 0.0f,  0.0f, 1.0f }, // Back
				{ 1.0f, 0.0f,  0.0f, 1.0f }, // Top
				{ 1.0f, 0.0f,  0.0f, 1.0f }, // Bottom
				{ 0.0f, 0.0f, -1.0f, 1.0f }, // Left
				{ 0.0f, 0.0f,  1.0f, 1.0f }, // Right
			} };

			std::vector<Vertex> vertices;
			vertices.reserve(FaceCount * VertexCountPerFace);

			for (size_t face = 0; face < FaceCount; ++face)
			{
				const auto& n = faceNormals[face];
				const auto& p = facePositions[face];
				const auto& t = faceTangents[face];

				for (size_t i = 0; i < VertexCountPerFace; ++i)
				{
					vertices.push_back({ p[i], n, texCoords[i], texCoords[i], t });
				}
			}

			return vertices;
		}

		std::vector<uint32_t> Cube::GetIndicesData() noexcept
		{
			std::vector<uint32_t> indices;
			indices.reserve(36); // 6 faces * 2 triangles * 3 indices

			for (uint32_t face = 0; face < FaceCount; ++face)
			{
				uint32_t base = face * VertexCountPerFace;
				indices.push_back(base + 0);
				indices.push_back(base + 1);
				indices.push_back(base + 2);

				indices.push_back(base + 0);
				indices.push_back(base + 2);
				indices.push_back(base + 3);
			}

			return indices;
		}

		entt::entity Sphere::Create(const CreateInfo& info) noexcept
		{
			return CreatePrimitive(
				info,
				ProceduralSphereModelID,
				ProceduralSphereMeshID,
				"ProceduralSphere",
				&GetVerticesData,
				&GetIndicesData);
		}

		std::vector<Vertex> Sphere::GetVerticesData() noexcept
		{
			std::vector<Vertex> vertices;
			vertices.reserve((StackCount + 1) * (SliceCount + 1));
			for (uint32_t stack = 0; stack <= StackCount; ++stack)
			{
				const float v = static_cast<float>(stack) / static_cast<float>(StackCount);
				const float theta = v * math::Pi;
				const float sinTheta = std::sin(theta);
				const float cosTheta = std::cos(theta);
				for (uint32_t slice = 0; slice <= SliceCount; ++slice)
				{
					const float u = static_cast<float>(slice) / static_cast<float>(SliceCount);
					const float phi = u * math::TwoPi;
					const Vector3 normal(
						sinTheta * std::cos(phi),
						cosTheta,
						sinTheta * std::sin(phi));
					vertices.push_back({
						normal,
						normal,
						Vector2(u, v),
						Vector2(u, v),
						Vector4(-std::sin(phi), 0.0f, std::cos(phi), 1.0f),
					});
				}
			}
			return vertices;
		}

		std::vector<uint32_t> Sphere::GetIndicesData() noexcept
		{
			std::vector<uint32_t> indices;
			indices.reserve(StackCount * SliceCount * 6);
			const uint32_t rowStride = SliceCount + 1;
			for (uint32_t stack = 0; stack < StackCount; ++stack)
			{
				for (uint32_t slice = 0; slice < SliceCount; ++slice)
				{
					const uint32_t topLeft = stack * rowStride + slice;
					const uint32_t bottomLeft = topLeft + rowStride;
					indices.insert(indices.end(), {
						topLeft, topLeft + 1, bottomLeft,
						topLeft + 1, bottomLeft + 1, bottomLeft,
					});
				}
			}
			return indices;
		}
	}
}
