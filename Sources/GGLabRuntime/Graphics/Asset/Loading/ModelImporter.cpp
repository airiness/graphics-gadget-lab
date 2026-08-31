#include "Graphics/Asset/Loading/ModelImporter.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "Graphics/Asset/Interop/AssimpMathInterop.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <ranges>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		constexpr float TangentLengthEpsilon = 1.0e-4f;
		constexpr float TangentLengthSqEpsilon = TangentLengthEpsilon * TangentLengthEpsilon;
		constexpr int GltfNearest = 9728;
		constexpr int GltfLinear = 9729;
		constexpr int GltfNearestMipmapNearest = 9984;
		constexpr int GltfLinearMipmapNearest = 9985;
		constexpr int GltfNearestMipmapLinear = 9986;
		constexpr int GltfLinearMipmapLinear = 9987;

		[[nodiscard]] aiTextureType ToAssimpTextureType(MaterialTextureSlot slot) noexcept
		{
			switch (slot)
			{
			case MaterialTextureSlot::BaseColor:
				return aiTextureType_BASE_COLOR;
			case MaterialTextureSlot::MetallicRoughness:
				return aiTextureType_GLTF_METALLIC_ROUGHNESS;
			case MaterialTextureSlot::Normal:
				return aiTextureType_NORMALS;
			case MaterialTextureSlot::Occlusion:
				return aiTextureType_AMBIENT_OCCLUSION;
			case MaterialTextureSlot::Emissive:
				return aiTextureType_EMISSIVE;
			default:
				return aiTextureType_NONE;
			}
		}

		[[nodiscard]] RHITextureAddressMode ToRHITextureAddressMode(aiTextureMapMode mode) noexcept
		{
			switch (mode)
			{
			case aiTextureMapMode_Wrap:
				return RHITextureAddressMode::Wrap;
			case aiTextureMapMode_Clamp:
				return RHITextureAddressMode::Clamp;
			case aiTextureMapMode_Mirror:
				return RHITextureAddressMode::Mirror;
			case aiTextureMapMode_Decal:
				return RHITextureAddressMode::Clamp;
			default:
				return RHITextureAddressMode::Wrap;
			}
		}

		[[nodiscard]] bool GltfMinFilterUsesMipmaps(int minFilter) noexcept
		{
			return minFilter >= GltfNearestMipmapNearest && minFilter <= GltfLinearMipmapLinear;
		}

		[[nodiscard]] bool GltfMinFilterIsLinear(int minFilter) noexcept
		{
			return minFilter == GltfLinear || minFilter == GltfLinearMipmapNearest ||
				minFilter == GltfLinearMipmapLinear;
		}

		[[nodiscard]] bool GltfMipFilterIsLinear(int minFilter) noexcept
		{
			return minFilter == GltfNearestMipmapLinear || minFilter == GltfLinearMipmapLinear;
		}

		[[nodiscard]] RHISamplerFilter MakeRHISamplerFilter(
			bool minLinear, bool magLinear, bool mipLinear) noexcept
		{
			const uint32_t index =
				(minLinear ? 4u : 0u) | (magLinear ? 2u : 0u) | (mipLinear ? 1u : 0u);
			constexpr RHISamplerFilter filters[] = {
				RHISamplerFilter::MinMagMipPoint,
				RHISamplerFilter::MinMagPointMipLinear,
				RHISamplerFilter::MinPointMagLinearMipPoint,
				RHISamplerFilter::MinPointMagMipLinear,
				RHISamplerFilter::MinLinearMagMipPoint,
				RHISamplerFilter::MinLinearMagPointMipLinear,
				RHISamplerFilter::MinMagLinearMipPoint,
				RHISamplerFilter::MinMagMipLinear,
			};
			return filters[index];
		}

		[[nodiscard]] SamplerKey MakeSamplerKey(const aiTextureMapMode mapMode[3], int magFilter,
			int minFilter, const ModelImportSettings& settings) noexcept
		{
			SamplerKey key{};
			const bool usesMipmaps = GltfMinFilterUsesMipmaps(minFilter);
			const bool minLinear = GltfMinFilterIsLinear(minFilter);
			const bool magLinear = magFilter != GltfNearest;
			const bool mipLinear = GltfMipFilterIsLinear(minFilter);
			const bool promoteToAnisotropic = settings.m_EnableAnisotropicFiltering &&
				minFilter == GltfLinearMipmapLinear && magLinear;

			key.m_Filter = promoteToAnisotropic
				? RHISamplerFilter::Anisotropic
				: MakeRHISamplerFilter(minLinear, magLinear, mipLinear);
			key.m_AddressU = ToRHITextureAddressMode(mapMode[0]);
			key.m_AddressV = ToRHITextureAddressMode(mapMode[1]);
			key.m_AddressW = ToRHITextureAddressMode(mapMode[2]);
			key.m_MipLODBias = 0.0f;
			key.m_MaxAnisotropy =
				promoteToAnisotropic ? std::clamp(settings.m_MaxAnisotropy, 1u, 16u) : 1u;
			key.m_CompareOp = RHICompareOp::Never;
			key.m_BorderColor[0] = 0.0f;
			key.m_BorderColor[1] = 0.0f;
			key.m_BorderColor[2] = 0.0f;
			key.m_BorderColor[3] = 0.0f;
			key.m_MinLOD = 0.0f;
			key.m_MaxLOD = usesMipmaps ? std::numeric_limits<float>::max() : 0.0f;
			return key;
		}

		[[nodiscard]] Vector4 MakeFallbackTangent(const Vector3& normal) noexcept
		{
			Vector3 n = normal;
			if (n.LengthSquared() <= TangentLengthSqEpsilon)
			{
				n = Vector3::UnitY;
			}
			else
			{
				n.Normalize();
			}

			const Vector3 up = std::abs(n.m_Y) < 0.999f ? Vector3::UnitY : Vector3::UnitZ;
			Vector3 tangent = up.Cross(n);
			if (tangent.LengthSquared() <= TangentLengthSqEpsilon)
			{
				tangent = Vector3::UnitX;
			}
			else
			{
				tangent.Normalize();
			}
			return Vector4(tangent.m_X, tangent.m_Y, tangent.m_Z, 1.0f);
		}

		void CollectModelMeshInstances(const aiNode& node, const aiMatrix4x4& parentTransform,
			const aiScene& scene, std::vector<ImportedModelMesh>& result) noexcept
		{
			const aiMatrix4x4 localToModel = parentTransform * node.mTransformation;
			for (uint32_t nodeMeshIndex = 0; nodeMeshIndex < node.mNumMeshes; ++nodeMeshIndex)
			{
				const uint32_t meshIndex = node.mMeshes[nodeMeshIndex];
				if (meshIndex >= scene.mNumMeshes)
				{
					GGLAB_LOG_GRAPHICS_WARN("Model node '{}' references invalid mesh index {}.",
						node.mName.C_Str(), meshIndex);
					continue;
				}
				result.push_back({
					.m_MeshIndex = meshIndex,
					.m_MaterialIndex = scene.mMeshes[meshIndex]->mMaterialIndex,
					.m_LocalTransform = math::interop::FromAssimp(localToModel),
					});
			}

			for (uint32_t childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
			{
				CollectModelMeshInstances(*node.mChildren[childIndex], localToModel, scene, result);
			}
		}

		[[nodiscard]] uint32_t RegisterTextureSource(ImportedModel& model,
			const std::filesystem::path& path, TextureSemantic semantic) noexcept
		{
			const TextureImportSettings importSettings = MakeTextureImportSettings(semantic);
			const auto existing = std::ranges::find_if(model.m_TextureSources,
				[&](const ImportedTextureSource& texture) noexcept
				{
					return texture.m_CanonicalPath == path &&
						texture.m_ImportSettings == importSettings;
				});
			if (existing != model.m_TextureSources.end())
			{
				return static_cast<uint32_t>(
					std::distance(model.m_TextureSources.begin(), existing));
			}

			ImportedTextureSource texture{};
			texture.m_CanonicalPath = path;
			texture.m_ImportSettings = importSettings;
			texture.m_Semantic = semantic;
			model.m_TextureSources.emplace_back(std::move(texture));
			return static_cast<uint32_t>(model.m_TextureSources.size() - 1);
		}
	}

	ModelImportResult ModelImporter::Import(const std::filesystem::path& path,
		const ModelImportSettings& settings, std::stop_token stopToken,
		const ProgressReporter& progress) noexcept
	{
		ModelImportResult result{};
		progress.Report(0.02f, "Validating model source", path.filename().generic_string());
		if (stopToken.stop_requested())
		{
			result.m_Error = "Model import was cancelled.";
			return result;
		}

		const auto canonicalPath = utils::Canonical(path);
		std::string extension = canonicalPath.extension().string();
		std::ranges::transform(extension, extension.begin(), [](unsigned char character) noexcept
			{ return static_cast<char>(std::tolower(character)); });
		if (extension != ".gltf")
		{
			result.m_Error = std::format("Unsupported model type '{}'.", extension);
			return result;
		}

		Assimp::Importer importer;
		constexpr uint32_t importFlags =
			aiProcess_ConvertToLeftHanded | aiProcess_Triangulate | aiProcess_GenSmoothNormals |
			aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
			aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials |
			aiProcess_SortByPType | aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph;
		progress.Report(
			0.08f, "Parsing model with Assimp", canonicalPath.filename().generic_string());
		const aiScene* scene = importer.ReadFile(canonicalPath.string(), importFlags);
		if (!scene)
		{
			result.m_Error = std::format("Assimp failed to load model '{}': {}",
				canonicalPath.string(), importer.GetErrorString());
			return result;
		}
		if (!scene->HasMeshes())
		{
			result.m_Error =
				std::format("Model file '{}' does not contain mesh data.", canonicalPath.string());
			return result;
		}
		if (!scene->mRootNode)
		{
			result.m_Error = std::format(
				"Model file '{}' does not contain a scene hierarchy.", canonicalPath.string());
			return result;
		}
		progress.Report(0.25f, "Model structure parsed",
			std::format("{} meshes, {} materials", scene->mNumMeshes, scene->mNumMaterials));

		ImportedModel& model = result.m_Model;
		model.m_CanonicalPath = canonicalPath;
		model.m_Name = canonicalPath.filename().generic_string();
		model.m_Type = ModelType::GlTF;
		model.m_Materials.resize(scene->mNumMaterials);
		const auto directory = canonicalPath.parent_path();

		for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
		{
			const float materialBegin = 0.25f + 0.35f * static_cast<float>(materialIndex) /
				std::max(scene->mNumMaterials, 1u);
			const float materialEnd = 0.25f + 0.35f * static_cast<float>(materialIndex + 1) /
				std::max(scene->mNumMaterials, 1u);
			progress.Report(materialBegin, "Processing model materials",
				std::format("{} of {}", materialIndex + 1, scene->mNumMaterials), materialIndex,
				scene->mNumMaterials);
			if (stopToken.stop_requested())
			{
				result.m_Error = "Model import was cancelled.";
				return result;
			}

			const aiMaterial* source = scene->mMaterials[materialIndex];
			ImportedMaterial& destination = model.m_Materials[materialIndex];
			destination.m_Name = source->GetName().C_Str();

			for (uint32_t slotIndex = 0; slotIndex < utils::ToIndex(MaterialTextureSlot::Count);
				++slotIndex)
			{
				const auto slot = static_cast<MaterialTextureSlot>(slotIndex);
				const TextureSemantic semantic = GetMaterialTextureSlotSemantic(slot);
				const aiTextureType textureType = ToAssimpTextureType(slot);
				if (textureType == aiTextureType_NONE)
				{
					continue;
				}

				aiString texturePath{};
				aiTextureMapping mapping = aiTextureMapping_UV;
				unsigned int uvIndex = 0;
				ai_real blend = 1.0f;
				aiTextureOp operation = aiTextureOp_Multiply;
				aiTextureMapMode mapMode[3] = {
					aiTextureMapMode_Wrap,
					aiTextureMapMode_Wrap,
					aiTextureMapMode_Wrap,
				};
				int magFilter = GltfLinear;
				int minFilter = GltfLinearMipmapLinear;
				if (source->GetTexture(textureType, 0, &texturePath, &mapping, &uvIndex, &blend,
					&operation, mapMode) != aiReturn_SUCCESS)
				{
					continue;
				}
				GGLAB_UNUSED(
					source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(textureType, 0), magFilter));
				GGLAB_UNUSED(
					source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(textureType, 0), minFilter));

				const auto canonicalTexturePath = utils::Canonical(directory / texturePath.C_Str());
				ImportedMaterialTextureBinding& binding = destination.m_TextureBindings[slotIndex];
				binding.m_TextureIndex =
					RegisterTextureSource(model, canonicalTexturePath, semantic);
				binding.m_SamplerKey = MakeSamplerKey(mapMode, magFilter, minFilter, settings);
				if (uvIndex > 1)
				{
					GGLAB_LOG_GRAPHICS_WARN(
						"Texture '{}' requests TEXCOORD{}, but only TEXCOORD0/1 are "
						"supported. Falling back to TEXCOORD0.",
						canonicalTexturePath.string(), uvIndex);
					uvIndex = 0;
				}
				binding.m_TexCoordIndex = uvIndex;
			}

			aiColor4D baseColor{};
			if (source->Get(AI_MATKEY_BASE_COLOR, baseColor) == aiReturn_SUCCESS)
			{
				destination.m_Properties.m_BaseColor =
					Color(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
			}
			GGLAB_UNUSED(
				source->Get(AI_MATKEY_METALLIC_FACTOR, destination.m_Properties.m_MetallicFactor));
			GGLAB_UNUSED(source->Get(
				AI_MATKEY_ROUGHNESS_FACTOR, destination.m_Properties.m_RoughnessFactor));

			aiColor3D emissiveColor{};
			if (source->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == aiReturn_SUCCESS)
			{
				destination.m_Properties.m_EmissiveColor =
					Color(emissiveColor.r, emissiveColor.g, emissiveColor.b, 1.0f);
			}

			aiString alphaMode{};
			if (source->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == aiReturn_SUCCESS)
			{
				const std::string_view mode = alphaMode.C_Str();
				if (mode == "MASK")
				{
					destination.m_Properties.m_AlphaMode = AlphaMode::Mask;
					destination.m_Properties.m_AlphaCutoffMode = AlphaCutoffMode::AlphaCutoff;
					GGLAB_UNUSED(source->Get(
						AI_MATKEY_GLTF_ALPHACUTOFF, destination.m_Properties.m_AlphaCutoff));
				}
				else if (mode == "BLEND")
				{
					destination.m_Properties.m_AlphaMode = AlphaMode::Blend;
				}
			}
			else
			{
				destination.m_Properties.m_AlphaCutoffMode = AlphaCutoffMode::Disabled;
			}

			int32_t doubleSided = 0;
			if (source->Get(AI_MATKEY_TWOSIDED, doubleSided) == aiReturn_SUCCESS &&
				doubleSided != 0)
			{
				destination.m_Properties.m_Flags |= MaterialFlags::DoubleSided;
			}
		}

		model.m_Meshes.resize(scene->mNumMeshes);
		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
		{
			progress.Report(
				0.60f + 0.33f * static_cast<float>(meshIndex) / std::max(scene->mNumMeshes, 1u),
				"Building model mesh data",
				std::format("{} of {}", meshIndex + 1, scene->mNumMeshes), meshIndex,
				scene->mNumMeshes);
			if (stopToken.stop_requested())
			{
				result.m_Error = "Model import was cancelled.";
				return result;
			}

			const aiMesh* source = scene->mMeshes[meshIndex];
			ImportedMesh& destination = model.m_Meshes[meshIndex];
			destination.m_Name = source->mName.C_Str();
			destination.m_MaterialIndex = source->mMaterialIndex;
			destination.m_Vertices.resize(source->mNumVertices);
			for (uint32_t vertexIndex = 0; vertexIndex < source->mNumVertices; ++vertexIndex)
			{
				Vertex& vertex = destination.m_Vertices[vertexIndex];
				const aiVector3D& position = source->mVertices[vertexIndex];
				vertex.m_Position = Vector3(position.x, position.y, position.z);

				if (source->HasNormals())
				{
					const aiVector3D& normal = source->mNormals[vertexIndex];
					vertex.m_Normal = Vector3(normal.x, normal.y, normal.z);
				}
				if (source->HasTextureCoords(0))
				{
					const aiVector3D& texCoord = source->mTextureCoords[0][vertexIndex];
					vertex.m_TexCoord0 = Vector2(texCoord.x, texCoord.y);
					vertex.m_TexCoord1 = vertex.m_TexCoord0;
				}
				if (source->HasTextureCoords(1))
				{
					const aiVector3D& texCoord = source->mTextureCoords[1][vertexIndex];
					vertex.m_TexCoord1 = Vector2(texCoord.x, texCoord.y);
				}

				if (source->HasTangentsAndBitangents())
				{
					Vector3 normal = vertex.m_Normal;
					if (normal.LengthSquared() <= TangentLengthSqEpsilon)
					{
						normal = Vector3::UnitY;
					}
					else
					{
						normal.Normalize();
					}

					const aiVector3D& sourceTangent = source->mTangents[vertexIndex];
					Vector3 tangent(sourceTangent.x, sourceTangent.y, sourceTangent.z);
					if (tangent.LengthSquared() <= TangentLengthSqEpsilon)
					{
						vertex.m_Tangent = MakeFallbackTangent(normal);
					}
					else
					{
						tangent.Normalize();
						const aiVector3D& sourceBitangent = source->mBitangents[vertexIndex];
						Vector3 bitangent(sourceBitangent.x, sourceBitangent.y, sourceBitangent.z);
						float handedness = 1.0f;
						if (bitangent.LengthSquared() > TangentLengthSqEpsilon)
						{
							bitangent.Normalize();
							handedness = tangent.Cross(bitangent).Dot(normal) < 0.0f ? -1.0f : 1.0f;
						}
						vertex.m_Tangent =
							Vector4(tangent.m_X, tangent.m_Y, tangent.m_Z, handedness);
					}
				}
				else
				{
					vertex.m_Tangent = MakeFallbackTangent(vertex.m_Normal);
				}
			}

			constexpr uint32_t IndicesPerFace = 3;
			destination.m_Indices.reserve(static_cast<size_t>(source->mNumFaces) * IndicesPerFace);
			for (uint32_t faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex)
			{
				const aiFace& face = source->mFaces[faceIndex];
				for (uint32_t index = 0; index < face.mNumIndices; ++index)
				{
					destination.m_Indices.push_back(face.mIndices[index]);
				}
			}

			if (!destination.m_Vertices.empty())
			{
				const Vector3* firstPosition =
					std::addressof(destination.m_Vertices.front().m_Position);
				destination.m_Aabb = math::CreateAabbFromPoints(
					destination.m_Vertices.size(), firstPosition, sizeof(Vertex));
				destination.m_Sphere = math::CreateSphere(destination.m_Aabb);
				destination.m_HasBounds = true;
			}
		}

		progress.Report(0.94f, "Collecting model scene hierarchy");
		CollectModelMeshInstances(*scene->mRootNode, aiMatrix4x4(), *scene, model.m_MeshInstances);
		if (model.m_MeshInstances.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("Model '{}' has no mesh instances in its node "
				"hierarchy; using identity transforms.",
				canonicalPath.string());
			for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
			{
				model.m_MeshInstances.push_back({
					.m_MeshIndex = meshIndex,
					.m_MaterialIndex = scene->mMeshes[meshIndex]->mMaterialIndex,
					});
			}
		}

		progress.Report(1.0f, "Model CPU import complete",
			std::format("{} meshes, {} instances, {} textures", model.m_Meshes.size(),
				model.m_MeshInstances.size(), model.m_TextureSources.size()));
		return result;
	}
}
