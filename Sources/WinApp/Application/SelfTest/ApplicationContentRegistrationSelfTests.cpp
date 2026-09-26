#include "Application/SelfTest/ApplicationContentRegistrationSelfTests.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Application/Demo/CoastalAtriumReferenceViews.h"
#include "Application/Lab/LightingContractReferenceViews.h"
#include "GGLabFoundation/Platform/Win/Win32TaskWorkerLifecycle.h"
#include "GGLabTestCore/SelfTest.h"
#include "GGLabRuntime/Core/Math/Transform.h"
#include "GGLabRuntime/Graphics/Asset/ModelImporter.h"
#include "GGLabRuntime/Graphics/Asset/AssetPaths.h"
#include "GGLabRuntime/Graphics/Asset/TextureLoader.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <string_view>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		std::vector<TextureAssetData> CheckImportedTextures(SelfTestContext& context,
			const ImportedModel& model) noexcept
		{
			std::vector<TextureAssetData> textures;
			for (const auto& source : model.m_TextureSources)
				textures.push_back(TextureLoader::LoadTextureData(source.m_CanonicalPath, source.m_ImportSettings));
			for (size_t index = 0; index < textures.size(); ++index)
			{
				const auto& source = model.m_TextureSources[index];
				const auto& texture = textures[index];
				const bool color = source.m_Semantic == TextureSemantic::BaseColor;
				context.Check(texture.IsValid() && texture.m_MipLevels > 1 &&
					texture.m_ColorSpace == (color ? TextureColorSpace::SRGB : TextureColorSpace::Linear) &&
					texture.m_ViewFormat == (color ? RHIFormat::R8G8B8A8UnormSrgb : RHIFormat::R8G8B8A8Unorm) &&
					source.m_CanonicalPath.parent_path() == model.m_CanonicalPath.parent_path() / "Textures",
					std::format("{} resolves beside its glTF and decodes with semantic view format and mipmaps",
						source.m_CanonicalPath.filename().string()));
			}
			return textures;
		}

		void CheckLightingContractContent(SelfTestContext& context) noexcept
		{
			const auto imported = ModelImporter::Import(ResolveAssetPath(GetApplicationSelfTestAssetRoot(),
				"Models/GGLabLightingContract/GGLabLightingContract.gltf"), {});
			context.Check(imported.Succeeded(), std::format("Lighting contract imports: {}", imported.m_Error));
			if (!imported.Succeeded()) return;
			const auto& model = imported.m_Model;
			context.Check(model.m_TextureSources.empty(),
				"Lighting contract imports without texture dependencies");
			struct MaterialReference
			{
				std::string_view m_Name;
				float m_Base;
				float m_Metallic;
				float m_Roughness;
			};
			const std::array materials{
				MaterialReference{ "MAT_Reflectance_02", 0.02f, 0.0f, 1.0f },
				MaterialReference{ "MAT_Reflectance_18", 0.18f, 0.0f, 1.0f },
				MaterialReference{ "MAT_Reflectance_50", 0.50f, 0.0f, 1.0f },
				MaterialReference{ "MAT_Reflectance_90", 0.90f, 0.0f, 1.0f },
				MaterialReference{ "MAT_Sphere_Matte", 0.18f, 0.0f, 1.0f },
				MaterialReference{ "MAT_Sphere_RoughDielectric", 0.18f, 0.0f, 0.5f },
				MaterialReference{ "MAT_Sphere_SmoothDielectric", 0.18f, 0.0f, 0.05f },
				MaterialReference{ "MAT_Sphere_Metallic", 0.18f, 1.0f, 0.1f },
				MaterialReference{ "MAT_Ground", 0.08f, 0.0f, 1.0f },
			};
			const auto matchesMaterial = [](const ImportedMaterial& material, const MaterialReference& reference) noexcept
				{
					const auto& properties = material.m_Properties;
					bool valid = std::abs(properties.m_MetallicFactor - reference.m_Metallic) < 0.00001f &&
						std::abs(properties.m_RoughnessFactor - reference.m_Roughness) < 0.00001f &&
						properties.m_AlphaMode == AlphaMode::Opaque && properties.m_BaseColor[3] == 1.0f;
					for (size_t channel = 0; channel < 3; ++channel)
						valid &= std::abs(properties.m_BaseColor[channel] - reference.m_Base) < 0.00001f &&
							properties.m_EmissiveColor[channel] == 0.0f;
					for (const auto& binding : material.m_TextureBindings)
						valid &= binding.m_TextureIndex == ImportedMaterialTextureBinding::InvalidTextureIndex;
					return valid;
				};
			// Assimp merges equivalent materials and meshes. Numeric inputs and spatial
			// geometry remain authoritative; authored names/counts need not survive.
			for (const auto& reference : materials)
			{
				context.Check(std::ranges::any_of(model.m_Materials, [&](const auto& material) noexcept
					{ return matchesMaterial(material, reference); }),
					std::format("{} retains an equivalent linear, opaque, untextured material", reference.m_Name));
			}

			struct MeshReference
			{
				std::string_view m_Name;
				Vector3 m_Center;
				Vector3 m_Size;
				Vector3 m_Normal;
				size_t m_MaterialReference;
				size_t m_TriangleCount;
			};
			const float diagonal = std::sqrt(0.5f);
			const std::array meshes{
				MeshReference{ "Card_Reflectance_02_Mesh", { -3.3f, 2.0f, 0.0f }, { 1.8f, 1.8f, 0.0f }, -Vector3::UnitZ, 0, 2 },
				MeshReference{ "Card_Reflectance_18_Mesh", { -1.1f, 2.0f, 0.0f }, { 1.8f, 1.8f, 0.0f }, -Vector3::UnitZ, 1, 2 },
				MeshReference{ "Card_Reflectance_50_Mesh", { 1.1f, 2.0f, 0.0f }, { 1.8f, 1.8f, 0.0f }, -Vector3::UnitZ, 2, 2 },
				MeshReference{ "Card_Reflectance_90_Mesh", { 3.3f, 2.0f, 0.0f }, { 1.8f, 1.8f, 0.0f }, -Vector3::UnitZ, 3, 2 },
				MeshReference{ "Sphere_Matte_Mesh", { 12.7f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 4, 3968 },
				MeshReference{ "Sphere_RoughDielectric_Mesh", { 14.9f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 5, 3968 },
				MeshReference{ "Sphere_SmoothDielectric_Mesh", { 17.1f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 6, 3968 },
				MeshReference{ "Sphere_Metallic_Mesh", { 19.3f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 7, 3968 },
				MeshReference{ "Receiver_Horizontal_Mesh", { 29.4f, 1.4f, 0.0f }, { 1.8f, 0.0f, 1.8f }, Vector3::UnitY, 1, 2 },
				MeshReference{ "Receiver_Vertical_Mesh", { 32.0f, 1.4f, 0.0f }, { 1.8f, 1.8f, 0.0f }, -Vector3::UnitZ, 1, 2 },
				MeshReference{ "Receiver_Tilt45_Mesh", { 34.6f, 1.4f, 0.0f }, { 1.8f, 1.8f * diagonal, 1.8f * diagonal }, { 0.0f, diagonal, -diagonal }, 1, 2 },
				MeshReference{ "Ground_Chart_Mesh", { 0.0f, -0.05f, 1.0f }, { 12.0f, 0.1f, 8.0f }, Vector3::Zero, 8, 12 },
				MeshReference{ "Ground_Spheres_Mesh", { 16.0f, -0.05f, 1.0f }, { 12.0f, 0.1f, 8.0f }, Vector3::Zero, 8, 12 },
				MeshReference{ "Ground_Angles_Mesh", { 32.0f, -0.05f, 1.0f }, { 12.0f, 0.1f, 8.0f }, Vector3::Zero, 8, 12 },
				MeshReference{ "Scale_OneMeter_Mesh", { 21.0f, 0.5f, 3.0f }, { 1.0f, 1.0f, 1.0f }, Vector3::Zero, 1, 12 },
			};
			std::array<size_t, 15> triangleCounts{};
			std::array<Vector3, 15> lower;
			std::array<Vector3, 15> upper;
			std::array<bool, 15> shapeValid;
			shapeValid.fill(true);
			const float infinity = std::numeric_limits<float>::infinity();
			lower.fill(Vector3(infinity, infinity, infinity));
			upper.fill(Vector3(-infinity, -infinity, -infinity));
			bool allTrianglesClassified = true;
			for (const auto& instance : model.m_MeshInstances)
			{
				if (instance.m_MeshIndex >= model.m_Meshes.size() || instance.m_MaterialIndex >= model.m_Materials.size())
				{
					allTrianglesClassified = false;
					continue;
				}
				const auto& mesh = model.m_Meshes[instance.m_MeshIndex];
				const auto& material = model.m_Materials[instance.m_MaterialIndex];
				const auto normalMatrix = math::CreateNormalMatrix(instance.m_LocalTransform);
				allTrianglesClassified &= mesh.m_Indices.size() % 3 == 0;
				for (size_t index = 0; index + 2 < mesh.m_Indices.size(); index += 3)
				{
					std::array<Vector3, 3> positions;
					std::array<Vector3, 3> normals;
					bool indicesValid = true;
					for (size_t corner = 0; corner < 3; ++corner)
					{
						const auto vertexIndex = mesh.m_Indices[index + corner];
						if (vertexIndex >= mesh.m_Vertices.size()) { indicesValid = false; break; }
						const auto& vertex = mesh.m_Vertices[vertexIndex];
						positions[corner] = math::TransformPoint(vertex.m_Position, instance.m_LocalTransform);
						normals[corner] = math::TransformDirection(vertex.m_Normal, normalMatrix);
					}
					if (!indicesValid) { allTrianglesClassified = false; continue; }
					size_t matchedShapes = 0;
					for (size_t shape = 0; shape < meshes.size(); ++shape)
					{
						const auto& reference = meshes[shape];
						const auto half = reference.m_Size * 0.5f;
						const bool contains = std::ranges::all_of(positions, [&](const Vector3& position) noexcept
							{
								const auto d = position - reference.m_Center;
								return std::abs(d.m_X) <= half.m_X + 0.0001f &&
									std::abs(d.m_Y) <= half.m_Y + 0.0001f && std::abs(d.m_Z) <= half.m_Z + 0.0001f;
							});
						// The meter cube bottom touches the ground; material identity separates those coplanar faces.
						if (!contains || !matchesMaterial(material, materials[reference.m_MaterialReference])) continue;
						++matchedShapes;
						++triangleCounts[shape];
						for (size_t corner = 0; corner < 3; ++corner)
						{
							const auto& position = positions[corner];
							lower[shape].m_X = std::min(lower[shape].m_X, position.m_X);
							lower[shape].m_Y = std::min(lower[shape].m_Y, position.m_Y);
							lower[shape].m_Z = std::min(lower[shape].m_Z, position.m_Z);
							upper[shape].m_X = std::max(upper[shape].m_X, position.m_X);
							upper[shape].m_Y = std::max(upper[shape].m_Y, position.m_Y);
							upper[shape].m_Z = std::max(upper[shape].m_Z, position.m_Z);
							if (reference.m_Normal.LengthSquared() > 0.0f)
								shapeValid[shape] &= (normals[corner] - reference.m_Normal).Length() < 0.0001f;
							if (reference.m_Name.starts_with("Sphere_"))
								shapeValid[shape] &= std::abs((position - reference.m_Center).Length() - 0.8f) < 0.0001f;
						}
					}
					allTrianglesClassified &= matchedShapes == 1;
				}
			}
			context.Check(allTrianglesClassified, "Every imported triangle belongs to exactly one authored lighting fixture shape");
			for (size_t shape = 0; shape < meshes.size(); ++shape)
			{
				const auto& reference = meshes[shape];
				context.Check(shapeValid[shape] && triangleCounts[shape] == reference.m_TriangleCount &&
					((lower[shape] + upper[shape]) * 0.5f - reference.m_Center).Length() < 0.0001f &&
					(upper[shape] - lower[shape] - reference.m_Size).Length() < 0.0001f,
					std::format("{} preserves triangles, material, placement, dimensions and reference normals", reference.m_Name));
			}

			Camera camera(Camera::CreateInfo{ .m_Width = 1280, .m_Height = 720 });
			CameraController controller(CameraController::CreateInfo{});
			CameraRig rig;
			rig.AttachMainCamera(camera, controller);
			const bool registered = rig.SetReferenceViews(
				{ LightingContractReferenceViews.begin(), LightingContractReferenceViews.end() });
			context.Check(registered, "Lighting contract reference views register");
			if (!registered) return;
			for (const auto& reference : LightingContractReferenceViews)
			{
				camera.SetManualEV100(4.0f);
				camera.SetExposureCompensationEV(2.0f);
				const bool restored = rig.RestoreReferenceView(reference.m_Id);
				const auto target = math::TransformPoint(reference.m_Target, camera.GetViewMatrix());
				context.Check(restored && (camera.GetPosition() - reference.m_Position).Length() < 0.0001f &&
					std::abs(target.m_X) < 0.0001f && std::abs(target.m_Y) < 0.0001f && target.m_Z > 0.0f &&
					camera.GetFov() == reference.m_VerticalFovDegrees && camera.GetNear() == 0.1f &&
					camera.GetFar() == 100.0f && camera.GetManualEV100() == 0.0f && camera.GetExposureCompensationEV() == 0.0f,
					std::format("{} restores its authored pose, projection and zero-EV reference", reference.m_Id));
			}
		}

		void CheckTextureContractContent(SelfTestContext& context) noexcept
		{
			const auto imported = ModelImporter::Import(ResolveAssetPath(GetApplicationSelfTestAssetRoot(),
				"Models/GGLabTextureContract/GGLabTextureContract.gltf"), {});
			context.Check(imported.Succeeded(), std::format("Texture contract imports: {}", imported.m_Error));
			if (!imported.Succeeded()) return;
			const auto& model = imported.m_Model;
			const auto textures = CheckImportedTextures(context, model);
			context.Check(textures.size() == 5, "Texture board resolves all five external diagnostic images");
			const auto pixel = [&](std::string_view filename, uint32_t x, uint32_t y)
				{
					std::array<int, 3> value{ -1, -1, -1 };
					for (size_t i = 0; i < textures.size(); ++i)
					{
						const auto& texture = textures[i];
						if (model.m_TextureSources[i].m_CanonicalPath.filename().string() != filename ||
							!texture.IsValid() || texture.m_Subresources.empty()) continue;
						const auto& subresource = texture.m_Subresources.front();
						if (x >= subresource.m_Width || y >= subresource.m_Height) continue;
						const size_t offset = static_cast<size_t>(subresource.m_DataOffset + y * subresource.m_RowPitch + x * 4);
						if (offset + 3 > texture.m_Pixels.size()) continue;
						for (size_t channel = 0; channel < 3; ++channel)
							value[channel] = std::to_integer<int>(texture.m_Pixels[offset + channel]);
					}
					return value;
				};
			context.Check(pixel("Direction.png", 64, 64) == std::array{ 230, 50, 40 } &&
				pixel("Direction.png", 224, 64) == std::array{ 45, 200, 65 } &&
				pixel("Direction.png", 64, 224) == std::array{ 45, 95, 230 } &&
				pixel("Direction.png", 224, 224) == std::array{ 230, 200, 40 },
				"Direction texture retains red/green top and blue/yellow bottom without pixel conversion");
			const auto findMaterial = [&](std::string_view name) -> const ImportedMaterial*
				{
					const auto it = std::ranges::find(model.m_Materials, name, &ImportedMaterial::m_Name);
					return it == model.m_Materials.end() ? nullptr : &*it;
				};
			const auto* grayTexture = findMaterial("PROBE_SRGB_Texture");
			const auto* grayReference = findMaterial("PROBE_SRGB_Factor");
			context.Check(grayTexture && grayReference && pixel("SRGB128.png", 0, 0) == std::array{ 128, 128, 128 } &&
				grayTexture->m_Properties.m_BaseColor[0] == 0.5f &&
				std::abs(grayReference->m_Properties.m_BaseColor[0] - 0.10793025f) < 0.000001f,
				"sRGB 128 with linear factor 0.5 preserves the independent 0.10793025 linear reference");
			bool packedChannels = true;
			const std::array normalPixels{ std::array{ 204, 128, 230 }, std::array{ 51, 128, 230 },
				std::array{ 128, 204, 230 }, std::array{ 128, 51, 230 } };
			for (size_t corner = 0; corner < 4; ++corner)
			{
				const uint32_t x = corner % 2 == 0 ? 64 : 192;
				const uint32_t y = corner < 2 ? 64 : 192;
				const auto mr = pixel("MetallicRoughness.png", x, y);
				const auto decoy = pixel("MetallicRoughnessDecoy.png", x, y);
				const int roughness = corner % 2 == 0 ? 64 : 192;
				const int metallic = corner < 2 ? 0 : 255;
				const auto* reference = findMaterial(std::format("PROBE_MR_Factor_{}", corner));
				packedChannels &= mr[1] == roughness && mr[2] == metallic &&
					decoy[1] == roughness && decoy[2] == metallic && mr[0] + decoy[0] == 255 &&
					pixel("NormalDirections.png", x, y) == normalPixels[corner] && reference &&
					std::abs(reference->m_Properties.m_RoughnessFactor - roughness / 255.0f * 0.5f) < 0.000001f &&
					std::abs(reference->m_Properties.m_MetallicFactor - metallic / 255.0f * 0.5f) < 0.000001f;
			}
			const auto* scaled = findMaterial("PROBE_MR_Scaled");
			context.Check(packedChannels && scaled && scaled->m_Properties.m_RoughnessFactor == 0.5f &&
				scaled->m_Properties.m_MetallicFactor == 0.5f,
				"Linear normal/MR bytes, ignored R contrast and independent multiplied factor references survive import");
			bool bindingsValid = true;
			for (const auto& material : model.m_Materials)
			{
				bindingsValid &= material.m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Occlusion)].m_TextureIndex ==
					ImportedMaterialTextureBinding::InvalidTextureIndex;
				for (size_t slot = 0; slot < material.m_TextureBindings.size(); ++slot)
				{
					const auto& binding = material.m_TextureBindings[slot];
					if (binding.m_TextureIndex == ImportedMaterialTextureBinding::InvalidTextureIndex) continue;
					bindingsValid &= binding.m_TextureIndex < model.m_TextureSources.size() && binding.m_TexCoordIndex == 0 &&
						binding.m_SamplerKey.m_AddressU == RHITextureAddressMode::Wrap &&
						binding.m_SamplerKey.m_AddressV == RHITextureAddressMode::Wrap;
					if (binding.m_TextureIndex < model.m_TextureSources.size())
						bindingsValid &= model.m_TextureSources[binding.m_TextureIndex].m_Semantic ==
							GetMaterialTextureSlotSemantic(static_cast<MaterialTextureSlot>(slot));
				}
			}
			context.Check(bindingsValid, "Diagnostic bindings use UV0/repeat with matching semantics and no implicit occlusion");
			std::array<size_t, 4> probeVertices{};
			bool basisAndUVValid = true;
			std::string basisFailure;
			for (const auto& instance : model.m_MeshInstances)
			{
				const auto& mesh = model.m_Meshes[instance.m_MeshIndex];
				for (const auto& vertex : mesh.m_Vertices)
				{
					const Vector3 p = math::TransformPoint(vertex.m_Position, instance.m_LocalTransform);
					if (p.m_X > -0.19f || p.m_Y < 1.39f) continue;
					const bool right = p.m_X > -2.2f;
					const bool top = p.m_Y > 3.4f;
					++probeVertices[(top ? 0 : 2) + (right ? 1 : 0)];
					const float u = (p.m_X - (right ? -2.0f : -4.2f)) / 1.8f;
					const float repeat = top && right ? 2.0f : 1.0f;
					const float expectedU = !top && right ? 1.0f - u : u * repeat;
					const float expectedV = 1.0f - (p.m_Y - (top ? 3.6f : 1.4f)) / 1.8f * repeat;
					const Vector3 n = math::TransformDirection(vertex.m_Normal, math::CreateNormalMatrix(instance.m_LocalTransform));
					const Vector3 t = math::TransformDirection(
						{ vertex.m_Tangent.m_X, vertex.m_Tangent.m_Y, vertex.m_Tangent.m_Z }, instance.m_LocalTransform);
					const Vector3 b = n.Cross(t) * vertex.m_Tangent.m_W;
					const bool valid = std::abs(vertex.m_TexCoord0.m_X - expectedU) < 0.0001f &&
						std::abs(vertex.m_TexCoord0.m_Y - expectedV) < 0.0001f &&
						(n + Vector3::UnitZ).Length() < 0.0001f &&
						(t - ((!top && right) ? -Vector3::UnitX : Vector3::UnitX)).Length() < 0.0001f &&
						(b - Vector3::UnitY).Length() < 0.0001f;
					if (!valid && basisFailure.empty())
						basisFailure = std::format("; at ({}, {}): UV ({}, {}) expected ({}, {}), N ({}, {}, {}), "
							"T ({}, {}, {}), B ({}, {}, {})", p.m_X, p.m_Y,
							vertex.m_TexCoord0.m_X, vertex.m_TexCoord0.m_Y, expectedU, expectedV,
							n.m_X, n.m_Y, n.m_Z, t.m_X, t.m_Y, t.m_Z, b.m_X, b.m_Y, b.m_Z);
					basisAndUVValid &= valid;
				}
			}
			context.Check(basisAndUVValid && std::ranges::all_of(probeVertices, [](size_t count) { return count >= 4; }),
				"Imported UVs preserve orientation/repeat and mirrored tangents preserve normal-map +Y up" + basisFailure);
		}

		void CheckCoastalAtriumReferenceViews(SelfTestContext& context) noexcept
		{
			Camera camera(Camera::CreateInfo{ .m_Width = 1920, .m_Height = 1080 });
			CameraController controller(CameraController::CreateInfo{});
			CameraRig rig;
			rig.AttachMainCamera(camera, controller);
			const bool registered = rig.SetReferenceViews(
				{ CoastalAtriumReferenceViews.begin(), CoastalAtriumReferenceViews.end() });
			context.Check(registered, "Three coastal atrium reference cameras register in runtime coordinates");
			if (!registered) return;
			for (const auto& reference : CoastalAtriumReferenceViews)
			{
				const bool restored = rig.RestoreReferenceView(reference.m_Id);
				const Vector3 targetInView = math::TransformPoint(reference.m_Target, camera.GetViewMatrix());
				context.Check(restored && (camera.GetPosition() - reference.m_Position).LengthSquared() == 0.0f &&
					std::abs(targetInView.m_X) < 0.0001f && std::abs(targetInView.m_Y) < 0.0001f &&
					targetInView.m_Z > 0.0f && camera.GetFov() == reference.m_VerticalFovDegrees &&
					camera.GetNear() == 0.1f && camera.GetFar() == 150.0f,
					std::format("{} looks at its authored target with the intended perspective projection", reference.m_Id));
				const auto view = camera.GetViewMatrix().ToArray();
				const auto projection = camera.GetProjMatrix().ToArray();
				camera.SetYawPitch(0.5f, 0.2f);
				camera.SetFov(75.0f);
				camera.SetNearFar(0.5f, 500.0f);
				camera.SetExposureCompensationEV(2.0f);
				controller.Update(camera, CameraInput{ .m_Front = true }, 0.1f);
				const auto serial = camera.GetTemporalResetSerial();
				const bool restoredAgain = rig.RestoreReferenceView(reference.m_Id);
				controller.Update(camera, CameraInput{}, 0.1f);
				camera.Update();
				context.Check(restoredAgain && camera.GetViewMatrix().ToArray() == view &&
					camera.GetProjMatrix().ToArray() == projection && camera.GetExposureCompensationEV() == 0.0f &&
					camera.GetTemporalResetSerial() == serial + 1 && rig.GetLastRestoredReferenceId() == reference.m_Id,
					std::format("{} restores identical matrices after movement and lens edits; runtime yaw/pitch {:.9g}, {:.9g}; "
						"vertical FOV {:.9g} deg; aspect {:.9g}", reference.m_Id,
						camera.GetYaw(), camera.GetPitch(), camera.GetFov(), camera.GetAspect()));
			}
		}

		void CheckCoastalAtriumContent(SelfTestContext& context) noexcept
		{
			const auto path = ResolveAssetPath(GetApplicationSelfTestAssetRoot(),
				"Models/GGLabCoastalAtrium/GGLabCoastalAtrium.gltf");
			const auto imported = ModelImporter::Import(path, {});
			context.Check(imported.Succeeded(),
				std::format("Coastal atrium glTF and external buffer import: {}", imported.m_Error));
			if (!imported.Succeeded())
			{
				return;
			}
			const auto& model = imported.m_Model;
			context.Check(model.m_TextureSources.size() == 9 &&
				std::ranges::all_of(model.m_Materials, [](const ImportedMaterial& material) noexcept
					{
						return material.m_Properties.m_AlphaMode == AlphaMode::Opaque;
					}), "Atrium uses nine original textures with opaque materials");
			CheckImportedTextures(context, model);
			for (const auto name : { "MAT_Concrete", "MAT_Paving", "MAT_Structure" })
			{
				const auto material = std::ranges::find(model.m_Materials, name, &ImportedMaterial::m_Name);
				bool valid = material != model.m_Materials.end();
				if (valid)
				{
					valid = material->m_Properties.m_RoughnessFactor == 1.0f &&
						material->m_Properties.m_MetallicFactor == (std::string_view(name) == "MAT_Structure" ? 1.0f : 0.0f);
					for (const auto slot : { MaterialTextureSlot::BaseColor, MaterialTextureSlot::Normal, MaterialTextureSlot::MetallicRoughness })
					{
						const auto& binding = material->m_TextureBindings[static_cast<size_t>(slot)];
						valid &= binding.m_TextureIndex < model.m_TextureSources.size() && binding.m_TexCoordIndex == 0;
					}
				}
				context.Check(valid, std::format("{} imports base color, normal and MR with the intended factors", name));
			}

			// Probe imported world triangles, independent of Assimp mesh merging or names.
			std::vector<std::array<Vector3, 3>> triangles;
			bool bounded = true;
			bool texturedBasisValid = true;
			for (const auto& instance : model.m_MeshInstances)
			{
				const auto& mesh = model.m_Meshes[instance.m_MeshIndex];
				const auto& material = model.m_Materials[instance.m_MaterialIndex];
				if (material.m_Name != "MAT_OceanPlaceholder")
				{
					for (const auto& vertex : mesh.m_Vertices)
					{
						const Vector3 t(vertex.m_Tangent.m_X, vertex.m_Tangent.m_Y, vertex.m_Tangent.m_Z);
						texturedBasisValid &= std::isfinite(vertex.m_TexCoord0.m_X) && std::isfinite(vertex.m_TexCoord0.m_Y) &&
							std::abs(t.LengthSquared() - 1.0f) < 0.001f && std::abs(t.Dot(vertex.m_Normal)) < 0.001f &&
							std::abs(vertex.m_Tangent.m_W) == 1.0f;
					}
				}
				for (size_t index = 0; index + 2 < mesh.m_Indices.size(); index += 3)
				{
					std::array<Vector3, 3> triangle;
					for (size_t corner = 0; corner < 3; ++corner)
					{
						auto& point = triangle[corner];
						point = math::TransformPoint(mesh.m_Vertices[mesh.m_Indices[index + corner]].m_Position,
							instance.m_LocalTransform);
						bounded &= std::abs(point.m_X) <= 36.001f &&
							point.m_Y >= -0.801f && point.m_Y <= 6.951f &&
							point.m_Z >= -37.001f && point.m_Z <= 27.001f;
					}
					triangles.push_back(triangle);
				}
			}
			context.Check(bounded && triangles.size() == 1046,
				std::format("Atrium keeps a bounded 72 by 64 meter footprint and 1046 triangles (actual: {})",
					triangles.size()));
			context.Check(texturedBasisValid, "Textured atrium preserves finite UVs and an orthonormal tangent basis");
			const auto nearestHit = [&](const Vector3& origin, const Vector3& direction) noexcept
				{
					float nearest = std::numeric_limits<float>::infinity();
					for (const auto& triangle : triangles)
					{
						const Vector3 edge1 = triangle[1] - triangle[0];
						const Vector3 edge2 = triangle[2] - triangle[0];
						const Vector3 p = direction.Cross(edge2);
						const float determinant = edge1.Dot(p);
						if (std::abs(determinant) < 0.000001f)
						{
							continue;
						}
						const Vector3 offset = origin - triangle[0];
						const float u = offset.Dot(p) / determinant;
						const Vector3 q = offset.Cross(edge1);
						const float v = direction.Dot(q) / determinant;
						const float distance = edge2.Dot(q) / determinant;
						if (u >= -0.00001f && v >= -0.00001f && u + v <= 1.00001f && distance > 0.0001f)
						{
							nearest = std::min(nearest, distance);
						}
					}
					return nearest;
				};
			const auto isWithinTolerance = [](float actual, float expected) noexcept
				{ return std::abs(actual - expected) < 0.001f; };
			context.Check(isWithinTolerance(nearestHit({ 0.0f, 4.0f, 0.0f }, -Vector3::UnitY), 1.6f) &&
				isWithinTolerance(nearestHit({ 0.0f, 4.0f, -12.0f }, -Vector3::UnitY), 3.1f) &&
				isWithinTolerance(nearestHit({ 30.0f, 4.0f, 0.0f }, -Vector3::UnitY), 4.05f),
				"Courtyard, coastal platform and ocean preserve authored elevations in Y-up meters");
			bool stairsValid = true;
			for (int step = 0; step < 10; ++step)
			{
				stairsValid &= isWithinTolerance(nearestHit({ 1.2f, 4.0f, -10.5f + (step + 0.5f) * 0.35f },
					-Vector3::UnitY), 4.0f - (0.9f + (step + 1) * 0.15f));
			}
			context.Check(stairsValid, "Ten stair treads connect the two levels with 0.15 meter risers");
			context.Check(nearestHit({ -10.0f, 4.0f, -3.0f }, Vector3::UnitX) > 2.0f &&
				nearestHit({ -10.0f, 4.0f, 2.0f }, Vector3::UnitX) > 2.0f,
				"Door and window openings remain unobstructed after import");
			context.Check(isWithinTolerance(nearestHit({ -10.0f, 4.0f, 0.0f }, Vector3::UnitX), 0.85f) &&
				isWithinTolerance(nearestHit({ -8.0f, 4.0f, 0.0f }, -Vector3::UnitX), 0.6f) &&
				isWithinTolerance(nearestHit({ -10.0f, 3.0f, 2.0f }, Vector3::UnitX), 0.85f) &&
				isWithinTolerance(nearestHit({ -10.0f, 6.0f, 2.0f }, Vector3::UnitX), 0.85f) &&
				isWithinTolerance(nearestHit({ -11.0f, 4.0f, 0.0f }, Vector3::UnitY), 2.6f) &&
				isWithinTolerance(nearestHit({ -11.0f, 4.0f, 0.0f }, -Vector3::UnitX), 1.9f),
				"Corridor preserves wall thickness, sill, lintel, roof and exterior enclosure");
			context.Check(isWithinTolerance(nearestHit({ 6.7f, 8.0f, -4.675f }, -Vector3::UnitY), 1.72f) &&
				isWithinTolerance(nearestHit({ 6.7f, 8.0f, -4.4f }, -Vector3::UnitY), 5.6f),
				"Pergola retains solid thin slats and open gaps for shadow inspection");
		}

		void CheckIslandContent(SelfTestContext& context) noexcept
		{
			const auto path = ResolveAssetPath(GetApplicationSelfTestAssetRoot(),
				"Models/GGLabIslandPrototype/GGLabIslandPrototype.gltf");
			const auto imported = ModelImporter::Import(path, {});
			context.Check(imported.Succeeded(),
				std::format("Island glTF and external buffer import: {}", imported.m_Error));
			if (!imported.Succeeded())
			{
				return;
			}
			const auto& model = imported.m_Model;
			context.Check(model.m_TextureSources.empty(), "Island imports without textures");
			struct ExpectedPoint
			{
				Vector3 m_Position;
				std::string_view m_Material;
			};
			std::vector<ExpectedPoint> expectedPoints;
			struct ExpectedBox
			{
				Vector3 m_Center;
				Vector3 m_HalfExtent;
				float m_RotationDegrees;
				std::string_view m_Material;
			};
			// Independent authored world corners in runtime (X, Z, Y), in meters.
			// Match geometry rather than mesh names: Assimp may merge equal-material meshes.
			const ExpectedBox boxes[] = {
				{ { 0.0f, 0.1f, 0.0f }, { 5.0f, 0.1f, 5.0f }, 0.0f, "MAT_GreyRough" },
				{ { 0.0f, 2.2f, 4.9f }, { 5.0f, 2.0f, 0.1f }, 0.0f, "MAT_GreyRough" },
				{ { -1.8f, 1.2f, 0.5f }, { 0.5f, 1.0f, 0.5f }, 35.0f, "MAT_RedRough" },
				{ { -1.144678f, 1.7f, 0.958861f }, { 0.3f, 0.125f, 0.125f },
					35.0f, "MAT_GreyRough" },
				{ { 2.0f, 0.45f, -1.2f }, { 1.0f, 0.25f, 0.5f }, -25.0f, "MAT_Metallic" },
			};
			for (const auto& box : boxes)
			{
				const float angle = box.m_RotationDegrees * std::numbers::pi_v<float> / 180.0f;
				for (const float xSign : { -1.0f, 1.0f })
				{
					for (const float ySign : { -1.0f, 1.0f })
					{
						for (const float zSign : { -1.0f, 1.0f })
						{
							const float x = xSign * box.m_HalfExtent.m_X;
							const float z = zSign * box.m_HalfExtent.m_Z;
							expectedPoints.push_back({ box.m_Center + Vector3(
								x * std::cos(angle) - z * std::sin(angle),
								ySign * box.m_HalfExtent.m_Y,
								x * std::sin(angle) + z * std::cos(angle)), box.m_Material });
						}
					}
				}
			}
			for (uint32_t side = 0; side < 12; ++side)
			{
				const float angle = side * std::numbers::pi_v<float> / 6.0f;
				for (const float height : { -0.8f, 0.0f })
				{
					expectedPoints.push_back({ { 7.75f * std::cos(angle), height,
						7.0f * std::sin(angle) }, "MAT_GreyRough" });
				}
			}
			for (const float x : { -20.0f, 20.0f })
			{
				for (const float z : { -20.0f, 20.0f })
				{
					expectedPoints.push_back({ { x, -0.45f, z }, "MAT_SmoothDielectric" });
				}
			}
			std::vector<ExpectedPoint> actualPoints;
			size_t triangleCount = 0;
			bool normalsValid = true;
			for (const auto& instance : model.m_MeshInstances)
			{
				const auto& mesh = model.m_Meshes[instance.m_MeshIndex];
				const auto& material = model.m_Materials[instance.m_MaterialIndex];
				for (const auto& vertex : mesh.m_Vertices)
				{
					actualPoints.push_back({ math::TransformPoint(vertex.m_Position,
						instance.m_LocalTransform), material.m_Name });
				}
				triangleCount += mesh.m_Indices.size() / 3;
				const Matrix normalMatrix = math::CreateNormalMatrix(instance.m_LocalTransform);
				for (size_t index = 0; index + 2 < mesh.m_Indices.size(); index += 3)
				{
					const auto& a = mesh.m_Vertices[mesh.m_Indices[index]];
					const auto& b = mesh.m_Vertices[mesh.m_Indices[index + 1]];
					const auto& c = mesh.m_Vertices[mesh.m_Indices[index + 2]];
					Vector3 geometricNormal = math::TransformDirection(b.m_Position - a.m_Position,
						instance.m_LocalTransform).Cross(math::TransformDirection(
							c.m_Position - a.m_Position, instance.m_LocalTransform));
					geometricNormal.Normalize();
					Vector3 normal = math::TransformDirection(a.m_Normal, normalMatrix);
					normal.Normalize();
					normalsValid &= std::abs(normal.Dot(geometricNormal)) > 0.999f;
				}
			}
			const auto containsPoint = [](const auto& points, const ExpectedPoint& point) noexcept
				{
					return std::ranges::any_of(points, [&](const ExpectedPoint& candidate) noexcept
						{
							return candidate.m_Material == point.m_Material &&
								(candidate.m_Position - point.m_Position).Length() < 0.001f;
						});
				};
			context.Check(std::ranges::all_of(expectedPoints,
				[&](const auto& point) noexcept { return containsPoint(actualPoints, point); }) &&
				std::ranges::all_of(actualPoints,
					[&](const auto& point) noexcept { return containsPoint(expectedPoints, point); }),
				"All seven authored shapes preserve world corners and material assignments after mesh merging");
			context.Check(triangleCount == 106,
				std::format("Island preserves 106 triangles across {} optimized instances (actual: {})",
					model.m_MeshInstances.size(), triangleCount));
			context.Check(normalsValid,
				"Transformed normals agree with triangle planes, including nonuniform scale");
			struct ExpectedMaterial
			{
				std::string_view m_Name;
				Color m_Color;
				float m_Metallic;
				float m_Roughness;
			};
			const ExpectedMaterial expectedMaterials[] = {
				{ "MAT_GreyRough", { 0.48f, 0.48f, 0.48f, 1.0f }, 0.0f, 0.8f },
				{ "MAT_RedRough", { 0.65f, 0.035f, 0.025f, 1.0f }, 0.0f, 0.7f },
				{ "MAT_Metallic", { 0.55f, 0.58f, 0.62f, 1.0f }, 1.0f, 0.23f },
				{ "MAT_SmoothDielectric", { 0.015f, 0.055f, 0.105f, 1.0f }, 0.0f, 0.1f },
			};
			for (const auto& expected : expectedMaterials)
			{
				const auto material = std::ranges::find(model.m_Materials, expected.m_Name, &ImportedMaterial::m_Name);
				bool matches = material != model.m_Materials.end();
				if (matches)
				{
					const auto& properties = material->m_Properties;
					matches = std::abs(properties.m_MetallicFactor - expected.m_Metallic) < 0.0001f &&
						std::abs(properties.m_RoughnessFactor - expected.m_Roughness) < 0.0001f;
					for (size_t channel = 0; channel < 4; ++channel)
					{
						matches &= std::abs(properties.m_BaseColor[channel] - expected.m_Color[channel]) < 0.0001f;
					}
				}
				context.Check(matches,
					std::format("{} preserves linear RGBA and metallic/roughness factors", expected.m_Name));
			}
		}
	}

	void RunApplicationContentRegistrationSelfTests(SelfTestContext& context) noexcept
	{
		const ApplicationContentRegistration desktop = CreateDesktopApplicationContent();
		const ApplicationContentSelection desktopSelection = ResolveApplicationContentSelection(
			desktop, DesktopLabHostDemoId, DesktopDefaultLabId);
		context.Check(desktop.IsValid() && desktop.m_Demos.size() == 5 &&
			desktop.m_Labs.size() == 19 && desktopSelection.Succeeded() &&
			std::ranges::any_of(desktop.m_Labs, [](const LabRegistration& lab) noexcept
				{ return lab.m_Descriptor.m_Id == LabId("gglab.lab.temporal_aa"); }),
			"Windows desktop composition includes five Demo entries and nineteen Labs");
		const ApplicationContentSelection islandSelection = ResolveApplicationContentSelection(
			desktop, DesktopIslandDemoId, DesktopDefaultLabId);
		context.Check(islandSelection.Succeeded() &&
			std::ranges::any_of(desktop.m_Demos, [](const ApplicationDemoRegistration& demo) noexcept
				{ return demo.m_Id == DesktopPlaygroundDemoId; }),
			"Island is selectable alongside the original Playground content");
		context.Check(ResolveApplicationContentSelection(
			desktop, DesktopCoastalAtriumDemoId, DesktopDefaultLabId).Succeeded(),
			"Coastal atrium is selectable alongside the import prototype");
		context.Check(ResolveApplicationContentSelection(
			desktop, DesktopLabHostDemoId, "gglab.lab.texture_contract").Succeeded(),
			"Texture contract is selectable through LabHost");
		context.Check(!ResolveApplicationContentSelection(
			desktop, "Demo.Playground.TextureContract", DesktopDefaultLabId).Succeeded(),
			"Texture contract is not registered as a standalone Demo");
		const auto rendererDemands = shader_programs::GetRendererInitialShaderProgramDemand();
		context.Check(std::ranges::find(
			rendererDemands, shader_programs::TemporalAAReprojectionCompute) != rendererDemands.end(),
			"Renderer artifact demand includes the production Temporal AA compute program");

		const auto checkSelectedDemand = [&context, &desktop](
			std::string_view labId, size_t expectedCount, std::string_view message) noexcept
			{
				const ApplicationContentSelection selection = ResolveApplicationContentSelection(
					desktop, DesktopLabHostDemoId, labId);
				ShaderProgramDemandSet demands;
				const bool succeeded = selection.Succeeded() &&
					demands.AddRange(shader_programs::GetRendererInitialShaderProgramDemand()) &&
					AppendSelectedContentShaderProgramDemand(selection, demands);
				context.Check(succeeded && demands.GetPrograms().size() == expectedCount, message);
			};
		checkSelectedDemand("gglab.lab.render_graph_compute", 37,
			"Render-graph compute selection contributes four stable shader demands");
		checkSelectedDemand("gglab.lab.coordinate_conformance", 37,
			"Coordinate conformance selection contributes four stable shader demands");
		checkSelectedDemand("gglab.lab.napa_voxel", 35,
			"Napa voxel selection contributes two stable shader demands");
		checkSelectedDemand("gglab.lab.texture_contract", 33,
			"Texture contract uses the production renderer's shader demands");
		checkSelectedDemand("gglab.lab.lighting_contract", 33,
			"Lighting contract is selectable through LabHost with production shader demands");
		CheckLightingContractContent(context);
		CheckIslandContent(context);
		CheckCoastalAtriumReferenceViews(context);
		// Keep one COM apartment alive across WIC decoder use, as runtime asset workers do.
		std::thread textureWorker([&]
			{
				const auto workerContext = win32::Win32TaskWorkerLifecycle{}.CreateContext(0);
				CheckCoastalAtriumContent(context);
				CheckTextureContractContent(context);
			});
		textureWorker.join();
	}
}
