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
#include <initializer_list>
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
		constexpr int GltfRepeat = 10497;
		constexpr int GltfClampToEdge = 33071;
		constexpr int GltfMirroredRepeat = 33648;

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
			default:
				return aiTextureType_NONE;
			}
		}

		[[nodiscard]] bool IsMaterialExtensionTextureSlot(MaterialTextureSlot slot) noexcept
		{
			switch (slot)
			{
			case MaterialTextureSlot::Clearcoat:
			case MaterialTextureSlot::ClearcoatRoughness:
			case MaterialTextureSlot::ClearcoatNormal:
			case MaterialTextureSlot::Anisotropy:
				return true;
			default:
				return false;
			}
		}

		using Json = nlohmann::json;

		struct TextureBindingSource
		{
			aiString m_Path;
			unsigned int m_UVIndex = 0;
			aiTextureMapMode m_MapMode[3] = {
				aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap,
			};
			int m_MagFilter = GltfLinear;
			int m_MinFilter = GltfLinearMipmapLinear;
		};

		[[nodiscard]] const Json* FindObjectField(const Json& parent, const char* name) noexcept
		{
			if (!parent.is_object()) return nullptr;
			const auto found = parent.find(name);
			return found != parent.end() && found->is_object() ? &*found : nullptr;
		}

		[[nodiscard]] const Json* FindArrayField(const Json& parent, const char* name) noexcept
		{
			if (!parent.is_object()) return nullptr;
			const auto found = parent.find(name);
			return found != parent.end() && found->is_array() ? &*found : nullptr;
		}

		[[nodiscard]] std::vector<size_t> CollectAssimpMaterialOrder(
			const Json& gltf, size_t materialCount)
		{
			// The pinned glTF2 Asset lazily loads materials on the first mesh
			// primitive reference. Its dense indices follow that encounter order.
			const Json* nodes = FindArrayField(gltf, "nodes");
			const Json* meshes = FindArrayField(gltf, "meshes");
			std::vector<bool> visitedNodes(nodes ? nodes->size() : 0u, false);
			std::vector<bool> visitedMeshes(meshes ? meshes->size() : 0u, false);
			std::vector<bool> visitedMaterials(materialCount, false);
			std::vector<size_t> order;
			const auto visitMesh = [&](size_t index)
			{
				if (!meshes || index >= meshes->size() || visitedMeshes[index]) return;
				visitedMeshes[index] = true;
				if (const Json* primitives = FindArrayField((*meshes)[index], "primitives"))
				{
					for (const Json& primitive : *primitives)
					{
						if (!primitive.is_object()) continue;
						const auto material = primitive.find("material");
						if (material == primitive.end() || !material->is_number_unsigned()) continue;
						const size_t materialIndex = material->get<size_t>();
						if (materialIndex >= materialCount || visitedMaterials[materialIndex]) continue;
						visitedMaterials[materialIndex] = true;
						order.push_back(materialIndex);
					}
				}
			};
			const auto visitNode = [&](auto&& self, size_t index) -> void
			{
				if (!nodes || index >= nodes->size() || visitedNodes[index]) return;
				visitedNodes[index] = true;
				const Json& node = (*nodes)[index];
				if (const Json* children = FindArrayField(node, "children"))
				{
					for (const Json& child : *children)
					{
						if (child.is_number_unsigned()) self(self, child.get<size_t>());
					}
				}
				if (!node.is_object()) return;
				const auto mesh = node.find("mesh");
				if (mesh != node.end() && mesh->is_number_unsigned())
				{
					visitMesh(mesh->get<size_t>());
				}
			};
			if (const Json* scenes = FindArrayField(gltf, "scenes"); scenes && !scenes->empty())
			{
				size_t sceneIndex = 0u;
				const auto scene = gltf.find("scene");
				if (scene != gltf.end() && scene->is_number_unsigned())
				{
					sceneIndex = scene->get<size_t>();
				}
				if (sceneIndex < scenes->size())
				{
					if (const Json* roots = FindArrayField((*scenes)[sceneIndex], "nodes"))
					{
						for (const Json& root : *roots)
						{
							if (root.is_number_unsigned()) visitNode(visitNode, root.get<size_t>());
						}
					}
				}
			}
			if (const Json* skins = FindArrayField(gltf, "skins"))
			{
				for (const Json& skin : *skins)
				{
					if (const Json* joints = FindArrayField(skin, "joints"))
					{
						for (const Json& joint : *joints)
						{
							if (joint.is_number_unsigned()) visitNode(visitNode, joint.get<size_t>());
						}
					}
				}
			}
			if (const Json* animations = FindArrayField(gltf, "animations"))
			{
				for (const Json& animation : *animations)
				{
					if (const Json* channels = FindArrayField(animation, "channels"))
					{
						for (const Json& channel : *channels)
						{
							if (const Json* target = FindObjectField(channel, "target"))
							{
								const auto node = target->find("node");
								if (node != target->end() && node->is_number_unsigned())
								{
									visitNode(visitNode, node->get<size_t>());
								}
							}
						}
					}
				}
			}
			return order;
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

		[[nodiscard]] bool ReadMaterialExtension(const Json& extensions, const char* name,
			const Json*& extension, std::string& error) noexcept
		{
			const auto found = extensions.find(name);
			if (found == extensions.end()) return true;
			if (!found->is_object())
			{
				error = std::format("{} must be an object.", name);
				return false;
			}
			extension = &*found;
			return true;
		}

		[[nodiscard]] bool ReadExtensionScalar(const Json& extension, const char* name,
			std::string_view extensionName, float& destination, std::string& error) noexcept
		{
			const auto found = extension.find(name);
			if (found == extension.end()) return true;
			if (!found->is_number() || !std::isfinite(found->get<float>()))
			{
				error = std::format("{}.{} must be a finite number.", extensionName, name);
				return false;
			}
			destination = found->get<float>();
			return true;
		}

		[[nodiscard]] bool ReadExtensionUnitFactor(const Json& extension, const char* name,
			std::string_view extensionName, float& destination, std::string& error) noexcept
		{
			if (!ReadExtensionScalar(extension, name, extensionName, destination, error)) return false;
			if (destination < 0.0f || destination > 1.0f)
			{
				error = std::format("{}.{} must be in [0, 1].", extensionName, name);
				return false;
			}
			return true;
		}

		[[nodiscard]] bool ValidateExtensionTextureInfo(const Json& extension,
			std::string_view extensionName, std::initializer_list<const char*> names,
			std::string& error) noexcept
		{
			for (const char* name : names)
			{
				const auto found = extension.find(name);
				if (found != extension.end() && !found->is_object())
				{
					error = std::format("{}.{} must be an object.", extensionName, name);
					return false;
				}
			}
			// Texture/image/sampler indices are checked once by the binding reader.
			return true;
		}

		[[nodiscard]] bool ReadGltfMaterialInputs(const Json& material,
			MaterialProperties& properties, std::string& error) noexcept
		{
			// Source JSON owns Material 2.0 extension values and core texture scale/strength.
			// Assimp owns core factors and geometry; vendor capability probes belong in tests.
			const auto extensions = material.find("extensions");
			if (extensions != material.end())
			{
				if (!extensions->is_object())
				{
					error = "glTF material extensions must be an object.";
					return false;
				}
				const Json* ior = nullptr;
				const Json* coat = nullptr;
				const Json* anisotropy = nullptr;
				if (!ReadMaterialExtension(*extensions, "KHR_materials_ior", ior, error) ||
					!ReadMaterialExtension(*extensions, "KHR_materials_clearcoat", coat, error) ||
					!ReadMaterialExtension(*extensions, "KHR_materials_anisotropy", anisotropy, error)) return false;
				if (ior)
				{
					if (!ReadExtensionScalar(*ior, "ior", "KHR_materials_ior", properties.m_Ior, error)) return false;
					// Zero is glTF's infinite-Fresnel mode; Assimp may normalize it.
					if (properties.m_Ior != 0.0f && properties.m_Ior < 1.0f)
					{
						error = "KHR_materials_ior.ior must be 0 or a finite number >= 1.";
						return false;
					}
				}
				if (coat &&
					(!ReadExtensionUnitFactor(*coat, "clearcoatFactor", "KHR_materials_clearcoat",
						properties.m_ClearcoatFactor, error) ||
						!ReadExtensionUnitFactor(*coat, "clearcoatRoughnessFactor", "KHR_materials_clearcoat",
							properties.m_ClearcoatRoughness, error) ||
						!ValidateExtensionTextureInfo(*coat, "KHR_materials_clearcoat",
							{ "clearcoatTexture", "clearcoatRoughnessTexture", "clearcoatNormalTexture" }, error))) return false;
				if (anisotropy &&
					(!ReadExtensionUnitFactor(*anisotropy, "anisotropyStrength", "KHR_materials_anisotropy",
						properties.m_AnisotropyStrength, error) ||
						!ReadExtensionScalar(*anisotropy, "anisotropyRotation", "KHR_materials_anisotropy",
							properties.m_AnisotropyRotation, error) ||
						!ValidateExtensionTextureInfo(*anisotropy, "KHR_materials_anisotropy",
							{ "anisotropyTexture" }, error))) return false;
			}
			if (const Json* normal = FindMaterialTextureInfo(material, MaterialTextureSlot::Normal))
			{
				if (!ReadTextureScalar(*normal, "scale", properties.m_NormalScale, error)) return false;
			}
			if (const Json* normal = FindMaterialTextureInfo(material, MaterialTextureSlot::ClearcoatNormal))
			{
				if (!ReadTextureScalar(*normal, "scale", properties.m_ClearcoatNormalScale, error)) return false;
			}
			if (const Json* occlusion = FindMaterialTextureInfo(material, MaterialTextureSlot::Occlusion))
			{
				if (!ReadTextureScalar(*occlusion, "strength", properties.m_OcclusionStrength, error)) return false;
			}
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

		[[nodiscard]] const Json* ReadIndexedObject(const Json& gltf, const char* arrayName,
			const Json& source, const char* indexName, std::string& error) noexcept
		{
			const auto index = source.find(indexName);
			const Json* objects = FindArrayField(gltf, arrayName);
			if (index == source.end() || !index->is_number_unsigned() || !objects ||
				index->get<uint64_t>() >= objects->size() ||
				!(*objects)[index->get<size_t>()].is_object())
			{
				error = std::format("Invalid glTF {} index '{}'.", arrayName, indexName);
				return nullptr;
			}
			return &(*objects)[index->get<size_t>()];
		}

		[[nodiscard]] bool ReadGltfTextureBindingSource(const Json& gltf, const Json& textureInfo,
			TextureBindingSource& destination, std::string& error) noexcept
		{
			const Json* texture = ReadIndexedObject(gltf, "textures", textureInfo, "index", error);
			if (!texture) return false;
			const Json* image = ReadIndexedObject(gltf, "images", *texture, "source", error);
			if (!image) return false;
			const auto uri = image->find("uri");
			if (uri == image->end() || !uri->is_string() || uri->get_ref<const std::string&>().empty() ||
				uri->get_ref<const std::string&>().starts_with("data:"))
			{
				error = "glTF material extension texture binding requires an external image URI.";
				return false;
			}
			destination.m_Path = aiString(uri->get_ref<const std::string&>());
			if (const auto texCoord = textureInfo.find("texCoord"); texCoord != textureInfo.end())
			{
				if (!texCoord->is_number_unsigned() || texCoord->get<uint64_t>() > 1u)
				{
					error = "glTF texture binding requires unsupported TEXCOORD set.";
					return false;
				}
				destination.m_UVIndex = texCoord->get<unsigned int>();
			}
			if (!texture->contains("sampler")) return true;
			const Json* sampler = ReadIndexedObject(gltf, "samplers", *texture, "sampler", error);
			if (!sampler) return false;
			auto readSamplerValue = [&](const char* name, int& value,
				std::initializer_list<int> allowed) noexcept
			{
				const auto found = sampler->find(name);
				if (found == sampler->end()) return true;
				if (!found->is_number_unsigned() ||
					std::ranges::find(allowed, found->get<uint64_t>()) == allowed.end())
				{
					error = std::format("Invalid glTF sampler {}.", name);
					return false;
				}
				value = found->get<int>();
				return true;
			};
			int wrapS = GltfRepeat;
			int wrapT = GltfRepeat;
			if (!readSamplerValue("wrapS", wrapS, { GltfRepeat, GltfClampToEdge, GltfMirroredRepeat }) ||
				!readSamplerValue("wrapT", wrapT, { GltfRepeat, GltfClampToEdge, GltfMirroredRepeat }) ||
				!readSamplerValue("magFilter", destination.m_MagFilter, { GltfNearest, GltfLinear }) ||
				!readSamplerValue("minFilter", destination.m_MinFilter,
					{ GltfNearest, GltfLinear, GltfNearestMipmapNearest, GltfLinearMipmapNearest,
					GltfNearestMipmapLinear, GltfLinearMipmapLinear })) return false;
			const auto mapMode = [](int wrap) noexcept
			{
				return wrap == GltfClampToEdge ? aiTextureMapMode_Clamp :
					wrap == GltfMirroredRepeat ? aiTextureMapMode_Mirror : aiTextureMapMode_Wrap;
			};
			destination.m_MapMode[0] = mapMode(wrapS);
			destination.m_MapMode[1] = mapMode(wrapT);
			return true;
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

		// Read source-owned material inputs once at the import boundary. Shading
		// still consumes one ImportedMaterial representation.
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
					name == "KHR_materials_anisotropy") continue;
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
		const std::vector<size_t> materialOrder = CollectAssimpMaterialOrder(
			gltf, sourceMaterials ? sourceMaterials->size() : 0u);
		// The pinned Assimp glTF2 importer lazily loads referenced materials in
		// first-use order, then appends one default material. Optional names do
		// not identify a source entry, even when they happen to be unique.
		if (scene->mNumMaterials != materialOrder.size() + 1u)
		{
			result.m_Error = "Assimp material indices do not match referenced glTF materials.";
			return result;
		}
		std::vector<const Json*> materialSources(scene->mNumMaterials, nullptr);
		if (sourceMaterials)
		{
			for (size_t index = 0; index < materialOrder.size(); ++index)
			{
				materialSources[index] = &(*sourceMaterials)[materialOrder[index]];
			}
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
			const Json* sourceMaterial = materialSources[materialIndex];
			if (sourceMaterial &&
				!ReadGltfMaterialInputs(*sourceMaterial, destination.m_Properties, result.m_Error)) return result;

			for (uint32_t slotIndex = 0; slotIndex < utils::ToIndex(MaterialTextureSlot::Count);
				++slotIndex)
			{
				const auto slot = static_cast<MaterialTextureSlot>(slotIndex);
				const TextureSemantic semantic = GetMaterialTextureSlotSemantic(slot);
				TextureBindingSource textureSource;
				const Json* textureInfo = sourceMaterial ? FindMaterialTextureInfo(*sourceMaterial, slot) : nullptr;
				if (IsMaterialExtensionTextureSlot(slot))
				{
					// Extension bindings always come from source JSON, including disabled
					// layers. Assimp may omit bindings when their factor is zero.
					if (!textureInfo) continue;
					if (!ReadGltfTextureBindingSource(gltf, *textureInfo, textureSource, result.m_Error)) return result;
				}
				else
				{
					const aiTextureType textureType = ToAssimpTextureType(slot);
					if (source->GetTexture(textureType, 0, &textureSource.m_Path, nullptr,
						&textureSource.m_UVIndex, nullptr, nullptr, textureSource.m_MapMode) != aiReturn_SUCCESS) continue;
					GGLAB_UNUSED(source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(textureType, 0),
						textureSource.m_MagFilter));
					GGLAB_UNUSED(source->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(textureType, 0),
						textureSource.m_MinFilter));
				}

				const auto canonicalTexturePath = utils::Canonical(directory / textureSource.m_Path.C_Str());
				ImportedMaterialTextureBinding& binding = destination.m_TextureBindings[slotIndex];
				binding.m_TextureIndex =
					RegisterTextureSource(model, canonicalTexturePath, semantic);
				binding.m_SamplerKey = MakeSamplerKey(textureSource.m_MapMode,
					textureSource.m_MagFilter, textureSource.m_MinFilter, settings);
				if (textureSource.m_UVIndex > 1)
				{
					result.m_Error = std::format(
						"Texture '{}' requests unsupported TEXCOORD{}.",
						canonicalTexturePath.string(), textureSource.m_UVIndex);
					return result;
				}
				binding.m_TexCoordIndex = textureSource.m_UVIndex;
				if (textureInfo)
				{
					if (!ReadTextureTransform(*textureInfo, binding, result.m_Error))
					{
						return result;
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
