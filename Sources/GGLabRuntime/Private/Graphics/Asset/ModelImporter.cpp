#include "GGLabRuntime/Graphics/Asset/ModelImporter.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabFoundation/IO/PathUtils.h"
#include "GGLabFoundation/Base/TypeUtils.h"
#include "Graphics/Asset/Interop/AssimpMathInterop.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
				// Assimp's glTF2 importer publishes core occlusionTexture as LIGHTMAP.
				return aiTextureType_LIGHTMAP;
			case MaterialTextureSlot::Emissive:
				return aiTextureType_EMISSIVE;
			case MaterialTextureSlot::Clearcoat:
			case MaterialTextureSlot::ClearcoatRoughness:
			case MaterialTextureSlot::ClearcoatNormal:
				return aiTextureType_CLEARCOAT;
			case MaterialTextureSlot::Anisotropy:
				return aiTextureType_ANISOTROPY;
			case MaterialTextureSlot::SheenColor:
			case MaterialTextureSlot::SheenRoughness:
				return aiTextureType_SHEEN;
			default:
				return aiTextureType_NONE;
			}
		}

		[[nodiscard]] unsigned int ToAssimpTextureIndex(MaterialTextureSlot slot) noexcept
		{
			switch (slot)
			{
			case MaterialTextureSlot::ClearcoatRoughness: return 1u;
			case MaterialTextureSlot::ClearcoatNormal: return 2u;
			case MaterialTextureSlot::SheenRoughness: return 1u;
			default: return 0u;
			}
		}

		using Json = nlohmann::json;

		[[nodiscard]] const Json* FindObjectField(const Json& parent, const char* name) noexcept
		{
			if (!parent.is_object()) return nullptr;
			const auto found = parent.find(name);
			return found != parent.end() && found->is_object() ? &*found : nullptr;
		}

		[[nodiscard]] const Json* FindMaterialTextureInfo(
			const Json& material, MaterialTextureSlot slot) noexcept
		{
			switch (slot)
			{
			case MaterialTextureSlot::BaseColor:
				if (const Json* pbr = FindObjectField(material, "pbrMetallicRoughness"))
					return FindObjectField(*pbr, "baseColorTexture");
				break;
			case MaterialTextureSlot::MetallicRoughness:
				if (const Json* pbr = FindObjectField(material, "pbrMetallicRoughness"))
					return FindObjectField(*pbr, "metallicRoughnessTexture");
				break;
			case MaterialTextureSlot::Normal:
				return FindObjectField(material, "normalTexture");
			case MaterialTextureSlot::Occlusion:
				return FindObjectField(material, "occlusionTexture");
			case MaterialTextureSlot::Emissive:
				return FindObjectField(material, "emissiveTexture");
			case MaterialTextureSlot::Clearcoat:
			case MaterialTextureSlot::ClearcoatRoughness:
			case MaterialTextureSlot::ClearcoatNormal:
				if (const Json* extensions = FindObjectField(material, "extensions"))
				{
					if (const Json* coat = FindObjectField(*extensions, "KHR_materials_clearcoat"))
					{
						const char* name = slot == MaterialTextureSlot::Clearcoat
							? "clearcoatTexture" : slot == MaterialTextureSlot::ClearcoatRoughness
								? "clearcoatRoughnessTexture" : "clearcoatNormalTexture";
						return FindObjectField(*coat, name);
					}
				}
				break;
			case MaterialTextureSlot::Anisotropy:
				if (const Json* extensions = FindObjectField(material, "extensions"))
				{
					if (const Json* anisotropy = FindObjectField(*extensions, "KHR_materials_anisotropy"))
					{
						return FindObjectField(*anisotropy, "anisotropyTexture");
					}
				}
				break;
			case MaterialTextureSlot::SheenColor:
			case MaterialTextureSlot::SheenRoughness:
				if (const Json* extensions = FindObjectField(material, "extensions"))
				{
					if (const Json* sheen = FindObjectField(*extensions, "KHR_materials_sheen"))
					{
						return FindObjectField(*sheen, slot == MaterialTextureSlot::SheenColor
							? "sheenColorTexture" : "sheenRoughnessTexture");
					}
				}
				break;
			default:
				break;
			}
			return nullptr;
		}

		[[nodiscard]] bool ReadTextureTransform(const Json& textureInfo,
			ImportedMaterialTextureBinding& binding, std::string& error) noexcept
		{
			const Json* extensions = FindObjectField(textureInfo, "extensions");
			if (extensions)
			{
				const auto declared = extensions->find("KHR_texture_transform");
				if (declared != extensions->end() && !declared->is_object())
				{
					error = "KHR_texture_transform must be an object.";
					return false;
				}
			}
			const Json* transform = extensions ? FindObjectField(*extensions, "KHR_texture_transform") : nullptr;
			if (!transform) return true;

			auto readPair = [&](const char* name, Vector2& destination) noexcept
			{
				const auto found = transform->find(name);
				if (found == transform->end()) return true;
				if (!found->is_array() || found->size() != 2 || !(*found)[0].is_number() ||
					!(*found)[1].is_number()) return false;
				destination = Vector2((*found)[0].get<float>(), (*found)[1].get<float>());
				return std::isfinite(destination.m_X) && std::isfinite(destination.m_Y);
			};
			if (!readPair("offset", binding.m_UVOffset) || !readPair("scale", binding.m_UVScale))
			{
				error = "Invalid KHR_texture_transform offset or scale.";
				return false;
			}
			if (const auto rotation = transform->find("rotation"); rotation != transform->end())
			{
				if (!rotation->is_number() || !std::isfinite(rotation->get<float>()))
				{
					error = "Invalid KHR_texture_transform rotation.";
					return false;
				}
				binding.m_UVRotation = rotation->get<float>();
			}
			if (const auto texCoord = transform->find("texCoord"); texCoord != transform->end())
			{
				if (!texCoord->is_number_unsigned() || texCoord->get<uint64_t>() > 1u)
				{
					error = "KHR_texture_transform requires unsupported TEXCOORD set.";
					return false;
				}
				binding.m_TexCoordIndex = texCoord->get<uint32_t>();
			}
			return true;
		}

		[[nodiscard]] bool ReadTextureScalar(const Json& textureInfo, const char* name,
			float& destination, std::string& error) noexcept
		{
			const auto found = textureInfo.find(name);
			if (found == textureInfo.end()) return true;
			if (!found->is_number() || !std::isfinite(found->get<float>()))
			{
				error = std::format("Invalid glTF texture {}.", name);
				return false;
			}
			destination = found->get<float>();
			return true;
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

		// Assimp does not preserve KHR_texture_transform's texCoord override. Read the
		// material JSON once at the import boundary; shading still consumes one
		// ImportedMaterial representation.
		std::ifstream sourceStream(canonicalPath, std::ios::binary);
		const Json gltf = sourceStream ? Json::parse(sourceStream, nullptr, false) : Json{};
		if (!gltf.is_object())
		{
			result.m_Error = "Model source is not a valid glTF JSON object.";
			return result;
		}
		const auto materials = gltf.find("materials");
		const Json* sourceMaterials = materials != gltf.end() && materials->is_array() ? &*materials : nullptr;
		for (const char* field : { "extensionsUsed", "extensionsRequired" })
		{
			const auto extensions = gltf.find(field);
			if (extensions == gltf.end()) continue;
			if (!extensions->is_array())
			{
				result.m_Error = std::format("glTF {} must be an array.", field);
				return result;
			}
			for (const Json& entry : *extensions)
			{
				if (!entry.is_string())
				{
					result.m_Error = std::format("glTF {} contains a non-string entry.", field);
					return result;
				}
				const std::string name = entry.get<std::string>();
				if (!name.starts_with("KHR_materials_")) continue;
				if (name == "KHR_materials_ior" || name == "KHR_materials_clearcoat" ||
					name == "KHR_materials_anisotropy" || name == "KHR_materials_sheen") continue;
				if (std::string_view(field) == "extensionsRequired")
				{
					result.m_Error = std::format("Required material extension '{}' is not yet supported.", name);
					return result;
				}
				GGLAB_LOG_GRAPHICS_WARN(
					"Optional material extension '{}' uses the core glTF fallback.", name);
			}
		}

		Assimp::Importer importer;
		constexpr uint32_t importFlags =
			aiProcess_ConvertToLeftHanded | aiProcess_Triangulate | aiProcess_GenSmoothNormals |
			aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices |
			aiProcess_ImproveCacheLocality |
			aiProcess_SortByPType | aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph;
		progress.Report(
			0.08f, "Parsing model with Assimp", canonicalPath.filename().generic_string());
		const aiScene* scene = importer.ReadFile(canonicalPath.string(), 0);
		if (!scene)
		{
			result.m_Error = std::format("Assimp failed to load model '{}': {}",
				canonicalPath.string(), importer.GetErrorString());
			return result;
		}
		// Assimp's MakeLeftHanded pass reflects authored bitangents and also negates
		// them. glTF normal maps require only the spatial reflection: preserve their
		// +Y-up tangent basis by cancelling that extra negation before processing.
		// Do this before CalcTangentSpace so generated tangents remain untouched.
		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
		{
			aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!mesh->HasTangentsAndBitangents()) continue;
			for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
			{
				mesh->mBitangents[vertexIndex] *= -1.0f;
			}
		}
		scene = importer.ApplyPostProcessing(importFlags);
		if (!scene)
		{
			result.m_Error = std::format("Assimp failed to process model '{}': {}",
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
		// glTF material indices are positional. Preserve them through Assimp so
		// per-binding source semantics can be merged without a name heuristic.
		if (scene->mNumMaterials != (sourceMaterials ? sourceMaterials->size() : 0u) + 1u)
		{
			result.m_Error = "Assimp material indices do not match the glTF material table.";
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
				const unsigned int textureIndex = ToAssimpTextureIndex(slot);
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
				if (source->GetTexture(textureType, textureIndex, &texturePath, &mapping, &uvIndex, &blend,
					&operation, mapMode) != aiReturn_SUCCESS)
				{
					if ((textureType == aiTextureType_CLEARCOAT ||
						textureType == aiTextureType_ANISOTROPY ||
						textureType == aiTextureType_SHEEN) && sourceMaterials &&
						materialIndex < sourceMaterials->size() &&
						FindMaterialTextureInfo((*sourceMaterials)[materialIndex], slot))
					{
						result.m_Error = std::format(
							"Assimp did not preserve a {} texture binding.",
							textureType == aiTextureType_CLEARCOAT ? "clearcoat" :
							textureType == aiTextureType_ANISOTROPY ? "anisotropy" : "sheen");
						return result;
					}
					continue;
				}
				GGLAB_UNUSED(
					source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(textureType, textureIndex), magFilter));
				GGLAB_UNUSED(
					source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(textureType, textureIndex), minFilter));

				const auto canonicalTexturePath = utils::Canonical(directory / texturePath.C_Str());
				ImportedMaterialTextureBinding& binding = destination.m_TextureBindings[slotIndex];
				binding.m_TextureIndex =
					RegisterTextureSource(model, canonicalTexturePath, semantic);
				binding.m_SamplerKey = MakeSamplerKey(mapMode, magFilter, minFilter, settings);
				if (uvIndex > 1)
				{
					result.m_Error = std::format(
						"Texture '{}' requests unsupported TEXCOORD{}.",
						canonicalTexturePath.string(), uvIndex);
					return result;
				}
				binding.m_TexCoordIndex = uvIndex;
				if (sourceMaterials && materialIndex < sourceMaterials->size())
				{
					if (const Json* textureInfo =
						FindMaterialTextureInfo((*sourceMaterials)[materialIndex], slot))
					{
						if (!ReadTextureTransform(*textureInfo, binding, result.m_Error))
						{
							return result;
						}
					}
				}
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
			if (sourceMaterials && materialIndex < sourceMaterials->size())
			{
				const Json& material = (*sourceMaterials)[materialIndex];
				const auto extensionsEntry = material.find("extensions");
				if (extensionsEntry != material.end() && !extensionsEntry->is_object())
				{
					result.m_Error = "glTF material extensions must be an object.";
					return result;
				}
				if (const Json* extensions = FindObjectField(material, "extensions"))
				{
					const auto anisotropyExtension = extensions->find("KHR_materials_anisotropy");
					if (anisotropyExtension != extensions->end())
					{
						if (!anisotropyExtension->is_object())
						{
							result.m_Error = "KHR_materials_anisotropy must be an object.";
							return result;
						}
						const auto strength = anisotropyExtension->find("anisotropyStrength");
						if (strength != anisotropyExtension->end())
						{
							if (!strength->is_number() || !std::isfinite(strength->get<float>()) ||
								strength->get<float>() < 0.0f || strength->get<float>() > 1.0f)
							{
								result.m_Error = "KHR_materials_anisotropy.anisotropyStrength must be in [0, 1].";
								return result;
							}
							destination.m_Properties.m_AnisotropyStrength = strength->get<float>();
						}
						const auto rotation = anisotropyExtension->find("anisotropyRotation");
						if (rotation != anisotropyExtension->end())
						{
							if (!rotation->is_number() || !std::isfinite(rotation->get<float>()))
							{
								result.m_Error = "KHR_materials_anisotropy.anisotropyRotation must be finite.";
								return result;
							}
							destination.m_Properties.m_AnisotropyRotation = rotation->get<float>();
						}
						const auto texture = anisotropyExtension->find("anisotropyTexture");
						if (texture != anisotropyExtension->end())
						{
							const auto index = texture->is_object()
								? texture->find("index") : Json::const_iterator{};
							if (!texture->is_object() || index == texture->end() ||
								!index->is_number_unsigned())
							{
								result.m_Error = "KHR_materials_anisotropy.anisotropyTexture requires a texture index.";
								return result;
							}
						}
						float importedStrength = 0.0f;
						float importedRotation = 0.0f;
						if (source->Get(AI_MATKEY_ANISOTROPY_FACTOR, importedStrength) != aiReturn_SUCCESS ||
							source->Get(AI_MATKEY_ANISOTROPY_ROTATION, importedRotation) != aiReturn_SUCCESS ||
							std::abs(importedStrength - destination.m_Properties.m_AnisotropyStrength) > 0.0001f ||
							std::abs(importedRotation - destination.m_Properties.m_AnisotropyRotation) > 0.0001f)
						{
							result.m_Error = "Assimp did not preserve KHR_materials_anisotropy factors.";
							return result;
						}
					}
					const auto sheenExtension = extensions->find("KHR_materials_sheen");
					if (sheenExtension != extensions->end())
					{
						if (!sheenExtension->is_object())
						{
							result.m_Error = "KHR_materials_sheen must be an object.";
							return result;
						}
						const auto color = sheenExtension->find("sheenColorFactor");
						if (color != sheenExtension->end())
						{
							if (!color->is_array() || color->size() != 3u)
							{
								result.m_Error = "KHR_materials_sheen.sheenColorFactor must contain three values.";
								return result;
							}
							float channels[3]{};
							for (size_t channel = 0; channel < 3u; ++channel)
							{
								const Json& value = (*color)[channel];
								if (!value.is_number() || !std::isfinite(value.get<float>()) ||
									value.get<float>() < 0.0f || value.get<float>() > 1.0f)
								{
									result.m_Error = "KHR_materials_sheen.sheenColorFactor must be in [0, 1].";
									return result;
								}
								channels[channel] = value.get<float>();
							}
							destination.m_Properties.m_SheenColor = Color(
								channels[0], channels[1], channels[2], 1.0f);
						}
						const auto roughness = sheenExtension->find("sheenRoughnessFactor");
						if (roughness != sheenExtension->end())
						{
							if (!roughness->is_number() || !std::isfinite(roughness->get<float>()) ||
								roughness->get<float>() < 0.0f || roughness->get<float>() > 1.0f)
							{
								result.m_Error = "KHR_materials_sheen.sheenRoughnessFactor must be in [0, 1].";
								return result;
							}
							destination.m_Properties.m_SheenRoughness = roughness->get<float>();
						}
						for (const char* name : { "sheenColorTexture", "sheenRoughnessTexture" })
						{
							const auto texture = sheenExtension->find(name);
							if (texture == sheenExtension->end()) continue;
							const auto index = texture->is_object()
								? texture->find("index") : Json::const_iterator{};
							if (!texture->is_object() || index == texture->end() ||
								!index->is_number_unsigned())
							{
								result.m_Error = std::format(
									"KHR_materials_sheen.{} requires a texture index.", name);
								return result;
							}
						}
						const Color& expected = destination.m_Properties.m_SheenColor;
						if (expected.m_R > 0.0f || expected.m_G > 0.0f || expected.m_B > 0.0f)
						{
							aiColor3D importedColor{};
							float importedRoughness = 0.0f;
							if (source->Get(AI_MATKEY_SHEEN_COLOR_FACTOR, importedColor) != aiReturn_SUCCESS ||
								source->Get(AI_MATKEY_SHEEN_ROUGHNESS_FACTOR, importedRoughness) != aiReturn_SUCCESS ||
								std::abs(importedColor.r - expected.m_R) > 0.0001f ||
								std::abs(importedColor.g - expected.m_G) > 0.0001f ||
								std::abs(importedColor.b - expected.m_B) > 0.0001f ||
								std::abs(importedRoughness - destination.m_Properties.m_SheenRoughness) > 0.0001f)
							{
								result.m_Error = "Assimp did not preserve KHR_materials_sheen factors.";
								return result;
							}
						}
					}
					const auto clearcoatExtension = extensions->find("KHR_materials_clearcoat");
					if (clearcoatExtension != extensions->end())
					{
						if (!clearcoatExtension->is_object())
						{
							result.m_Error = "KHR_materials_clearcoat must be an object.";
							return result;
						}
						auto readCoatFactor = [&](const char* name, float& value) noexcept
						{
							const auto found = clearcoatExtension->find(name);
							if (found == clearcoatExtension->end()) return true;
							if (!found->is_number() || !std::isfinite(found->get<float>()) ||
								found->get<float>() < 0.0f || found->get<float>() > 1.0f)
							{
								result.m_Error = std::format("KHR_materials_clearcoat.{} must be in [0, 1].", name);
								return false;
							}
							value = found->get<float>();
							return true;
						};
						if (!readCoatFactor("clearcoatFactor", destination.m_Properties.m_ClearcoatFactor) ||
							!readCoatFactor("clearcoatRoughnessFactor", destination.m_Properties.m_ClearcoatRoughness))
						{
							return result;
						}
						for (const char* name : { "clearcoatTexture", "clearcoatRoughnessTexture",
							"clearcoatNormalTexture" })
						{
							const auto texture = clearcoatExtension->find(name);
							if (texture == clearcoatExtension->end()) continue;
							const auto index = texture->is_object() ? texture->find("index") : Json::const_iterator{};
							if (!texture->is_object() || index == texture->end() ||
								!index->is_number_unsigned())
							{
								result.m_Error = std::format("KHR_materials_clearcoat.{} requires a texture index.", name);
								return result;
							}
						}
						if (destination.m_Properties.m_ClearcoatFactor > 0.0f)
						{
							float importedFactor = 0.0f;
							float importedRoughness = 0.0f;
							if (source->Get(AI_MATKEY_CLEARCOAT_FACTOR, importedFactor) != aiReturn_SUCCESS ||
								source->Get(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, importedRoughness) != aiReturn_SUCCESS ||
								std::abs(importedFactor - destination.m_Properties.m_ClearcoatFactor) > 0.0001f ||
								std::abs(importedRoughness - destination.m_Properties.m_ClearcoatRoughness) > 0.0001f)
							{
								result.m_Error = "Assimp did not preserve KHR_materials_clearcoat factors.";
								return result;
							}
						}
					}
					const auto iorExtension = extensions->find("KHR_materials_ior");
					if (iorExtension != extensions->end())
					{
						if (!iorExtension->is_object())
						{
							result.m_Error = "KHR_materials_ior must be an object.";
							return result;
						}
						const auto iorValue = iorExtension->find("ior");
						if (iorValue != iorExtension->end())
						{
							const float authoredIor = iorValue->is_number()
								? iorValue->get<float>() : 0.0f;
							if (!std::isfinite(authoredIor) || authoredIor < 1.0f)
							{
								result.m_Error = "KHR_materials_ior.ior must be a finite number >= 1.";
								return result;
							}
							float importedIor = 0.0f;
							if (source->Get(AI_MATKEY_REFRACTI, importedIor) != aiReturn_SUCCESS ||
								!std::isfinite(importedIor) || importedIor < 1.0f ||
								std::abs(importedIor - authoredIor) >
									std::max(0.0001f, authoredIor * 0.0001f))
							{
								result.m_Error = "Assimp did not preserve KHR_materials_ior.ior.";
								return result;
							}
							destination.m_Properties.m_Ior = importedIor;
						}
					}
				}
				if (const Json* normal = FindMaterialTextureInfo(material, MaterialTextureSlot::Normal))
				{
					if (!ReadTextureScalar(*normal, "scale", destination.m_Properties.m_NormalScale,
						result.m_Error)) return result;
				}
				if (const Json* coatNormal =
					FindMaterialTextureInfo(material, MaterialTextureSlot::ClearcoatNormal))
				{
					if (!ReadTextureScalar(*coatNormal, "scale",
						destination.m_Properties.m_ClearcoatNormalScale, result.m_Error)) return result;
				}
				// Assimp's glTF2 importer writes occlusion strength under
				// "$tex.file.strength", which its public glTF macro does not query.
				if (const Json* occlusion =
					FindMaterialTextureInfo(material, MaterialTextureSlot::Occlusion))
				{
					if (!ReadTextureScalar(*occlusion, "strength",
						destination.m_Properties.m_OcclusionStrength, result.m_Error)) return result;
				}
			}

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
