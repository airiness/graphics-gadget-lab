#include "Application/SelfTest/ApplicationContentRegistrationSelfTests.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Application/Demo/CoastalSceneCameraPaths.h"
#include "Application/Demo/CoastalSceneReferenceViews.h"
#include "Application/Lab/LightingContractReferenceViews.h"
#include "Application/Lab/AtmosphereRangeReferenceViews.h"
#include "GGLabFoundation/Platform/Win/Win32TaskWorkerLifecycle.h"
#include "GGLabTestCore/SelfTest.h"
#include "GGLabRuntime/Core/Math/Transform.h"
#include "GGLabRuntime/Graphics/Asset/ModelImporter.h"
#include "GGLabRuntime/Graphics/Asset/AssetPaths.h"
#include "GGLabRuntime/Graphics/Asset/TextureLoader.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/CameraRig.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
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
#include <optional>
#include <string_view>
#include <string>
#include <system_error>
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

		void CheckAtmosphereRangeContent(SelfTestContext& context) noexcept
		{
			const auto imported = ModelImporter::Import(ResolveAssetPath(GetApplicationSelfTestAssetRoot(),
				"Models/GGLabAtmosphereRange/GGLabAtmosphereRange.gltf"), {});
			context.Check(imported.Succeeded(), std::format("Atmosphere range imports: {}", imported.m_Error));
			if (!imported.Succeeded()) return;
			const auto& model = imported.m_Model;
			context.Check(model.m_TextureSources.empty(), "Atmosphere range has no texture dependencies");
			constexpr std::array distances{ 25.0f, 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f };
			constexpr std::array slopes{ -0.65f, -0.39f, -0.13f, 0.13f, 0.39f, 0.65f };
			constexpr std::array reflectances{ 0.02f, 0.18f, 0.90f };
			std::array<Vector3, 18> centers;
			std::array<Vector3, 18> lower;
			std::array<Vector3, 18> upper;
			std::array<size_t, 18> triangles{};
			const float infinity = std::numeric_limits<float>::infinity();
			lower.fill(Vector3(infinity, infinity, infinity));
			upper.fill(Vector3(-infinity, -infinity, -infinity));
			for (size_t target = 0; target < distances.size(); ++target)
			{
				const float depth = distances[target] / std::sqrt(1.04f + slopes[target] * slopes[target]);
				for (size_t patch = 0; patch < 3; ++patch)
					centers[target * 3 + patch] = Vector3(slopes[target] * depth + (float(patch) - 1.0f) * 2.0f,
						20.0f + 0.2f * depth, depth);
			}
			bool valid = true;
			for (const auto& instance : model.m_MeshInstances)
			{
				if (instance.m_MeshIndex >= model.m_Meshes.size() || instance.m_MaterialIndex >= model.m_Materials.size())
				{
					valid = false;
					continue;
				}
				const auto& mesh = model.m_Meshes[instance.m_MeshIndex];
				const auto& material = model.m_Materials[instance.m_MaterialIndex];
				const auto& properties = material.m_Properties;
				valid &= properties.m_AlphaMode == AlphaMode::Opaque && properties.m_BaseColor[3] == 1.0f &&
					properties.m_MetallicFactor == 0.0f && properties.m_RoughnessFactor == 1.0f;
				for (size_t channel = 0; channel < 3; ++channel)
					valid &= properties.m_EmissiveColor[channel] == 0.0f;
				const auto normalMatrix = math::CreateNormalMatrix(instance.m_LocalTransform);
				valid &= mesh.m_Indices.size() % 3 == 0;
				for (size_t index = 0; index + 2 < mesh.m_Indices.size(); index += 3)
				{
					std::array<Vector3, 3> points;
					bool indicesValid = true;
					for (size_t corner = 0; corner < 3; ++corner)
					{
						const auto vertexIndex = mesh.m_Indices[index + corner];
						if (vertexIndex >= mesh.m_Vertices.size()) { indicesValid = false; break; }
						const auto& vertex = mesh.m_Vertices[vertexIndex];
						points[corner] = math::TransformPoint(vertex.m_Position, instance.m_LocalTransform);
						valid &= (math::TransformDirection(vertex.m_Normal, normalMatrix) - Vector3(0, 0, -1)).Length() < 0.0001f;
					}
					if (!indicesValid) { valid = false; continue; }
					size_t matches = 0;
					for (size_t shape = 0; shape < centers.size(); ++shape)
					{
						const bool contains = std::ranges::all_of(points, [&](const auto& point) noexcept
							{
								const auto delta = point - centers[shape];
								return std::abs(delta.m_X) <= 1.0002f && std::abs(delta.m_Y) <= 3.0002f &&
									std::abs(delta.m_Z) < 0.0002f;
							});
						if (!contains) continue;
						++matches;
						++triangles[shape];
						for (size_t channel = 0; channel < 3; ++channel)
							valid &= std::abs(properties.m_BaseColor[channel] - reflectances[shape % 3]) < 0.00001f;
						for (const auto& point : points)
						{
							lower[shape].m_X = std::min(lower[shape].m_X, point.m_X);
							lower[shape].m_Y = std::min(lower[shape].m_Y, point.m_Y);
							lower[shape].m_Z = std::min(lower[shape].m_Z, point.m_Z);
							upper[shape].m_X = std::max(upper[shape].m_X, point.m_X);
							upper[shape].m_Y = std::max(upper[shape].m_Y, point.m_Y);
							upper[shape].m_Z = std::max(upper[shape].m_Z, point.m_Z);
						}
					}
					valid &= matches == 1;
				}
			}
			for (size_t shape = 0; shape < centers.size(); ++shape)
				valid &= triangles[shape] == 2 &&
					((lower[shape] + upper[shape]) * 0.5f - centers[shape]).Length() < 0.0002f &&
					(upper[shape] - lower[shape] - Vector3(2, 6, 0)).Length() < 0.0002f;
			context.Check(valid, "All range triangles preserve meter scale, equal dimensions, placement, normals and materials");
			Camera camera({ .m_Width = 1280, .m_Height = 720 });
			CameraController controller(CameraController::CreateInfo{});
			CameraRig rig;
			rig.AttachMainCamera(camera, controller);
			const bool registered = rig.SetReferenceViews({ AtmosphereRangeReferenceViews.begin(), AtmosphereRangeReferenceViews.end() });
			context.Check(registered, "Atmosphere range reference cameras register");
			if (!registered) return;
			for (size_t view = 0; view < AtmosphereRangeReferenceViews.size(); ++view)
			{
				const auto& reference = AtmosphereRangeReferenceViews[view];
				const bool restored = rig.RestoreReferenceView(reference.m_Id);
				const auto target = math::TransformPoint(reference.m_Target, camera.GetViewMatrix());
				context.Check(restored && (camera.GetPosition() - Vector3(0, 20, 0)).Length() < 0.0001f &&
					std::abs(target.m_X) < 0.0001f && std::abs(target.m_Y) < 0.0001f && target.m_Z > 0.0f &&
					camera.GetFov() == reference.m_VerticalFovDegrees && camera.GetNear() == 0.1f && camera.GetFar() == 2000.0f &&
					camera.GetManualEV100() == 15.0f && camera.GetExposureCompensationEV() == 0.0f,
					std::format("{} restores fixed observer, projection and EV15", reference.m_Id));
				if (view > 0)
				{
					const auto center = centers[(view - 1) * 3 + 1];
					const auto sample = math::TransformPoint(center, camera.GetViewMatrix());
					context.Check(std::abs((center - camera.GetPosition()).Length() - distances[view - 1]) < 0.0002f &&
						std::abs(sample.m_X) < 0.001f && std::abs(sample.m_Y) < 0.001f && sample.m_Z > 0.0f,
						"Reference sample is on the center ray at its specified Euclidean distance");
				}
			}
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
				MaterialReference{ "MAT_Sweep_Dielectric_R000", 0.18f, 0.0f, 0.0f },
				MaterialReference{ "MAT_Sweep_Dielectric_R005", 0.18f, 0.0f, 0.05f },
				MaterialReference{ "MAT_Sweep_Dielectric_R010", 0.18f, 0.0f, 0.1f },
				MaterialReference{ "MAT_Sweep_Dielectric_R025", 0.18f, 0.0f, 0.25f },
				MaterialReference{ "MAT_Sweep_Dielectric_R050", 0.18f, 0.0f, 0.5f },
				MaterialReference{ "MAT_Sweep_Dielectric_R100", 0.18f, 0.0f, 1.0f },
				MaterialReference{ "MAT_Sweep_Metallic_R000", 0.18f, 1.0f, 0.0f },
				MaterialReference{ "MAT_Sweep_Metallic_R005", 0.18f, 1.0f, 0.05f },
				MaterialReference{ "MAT_Sweep_Metallic_R010", 0.18f, 1.0f, 0.1f },
				MaterialReference{ "MAT_Sweep_Metallic_R025", 0.18f, 1.0f, 0.25f },
				MaterialReference{ "MAT_Sweep_Metallic_R050", 0.18f, 1.0f, 0.5f },
				MaterialReference{ "MAT_Sweep_Metallic_R100", 0.18f, 1.0f, 1.0f },
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
				MeshReference{ "SweepSphere_Dielectric_R000_Mesh", { 42.5f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 9, 3968 },
				MeshReference{ "SweepSphere_Dielectric_R005_Mesh", { 44.7f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 10, 3968 },
				MeshReference{ "SweepSphere_Dielectric_R010_Mesh", { 46.9f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 11, 3968 },
				MeshReference{ "SweepSphere_Dielectric_R025_Mesh", { 49.1f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 12, 3968 },
				MeshReference{ "SweepSphere_Dielectric_R050_Mesh", { 51.3f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 13, 3968 },
				MeshReference{ "SweepSphere_Dielectric_R100_Mesh", { 53.5f, 0.8f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 14, 3968 },
				MeshReference{ "SweepSphere_Metallic_R000_Mesh", { 42.5f, 3.2f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 15, 3968 },
				MeshReference{ "SweepSphere_Metallic_R005_Mesh", { 44.7f, 3.2f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 16, 3968 },
				MeshReference{ "SweepSphere_Metallic_R010_Mesh", { 46.9f, 3.2f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 17, 3968 },
				MeshReference{ "SweepSphere_Metallic_R025_Mesh", { 49.1f, 3.2f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 18, 3968 },
				MeshReference{ "SweepSphere_Metallic_R050_Mesh", { 51.3f, 3.2f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 19, 3968 },
				MeshReference{ "SweepSphere_Metallic_R100_Mesh", { 53.5f, 3.2f, 0.0f }, { 1.6f, 1.6f, 1.6f }, Vector3::Zero, 20, 3968 },
				MeshReference{ "Ground_RoughnessSweep_Mesh", { 48.0f, -0.05f, 1.0f }, { 16.0f, 0.1f, 8.0f }, Vector3::Zero, 8, 12 },
			};
			std::array<size_t, meshes.size()> triangleCounts{};
			std::array<Vector3, meshes.size()> lower;
			std::array<Vector3, meshes.size()> upper;
			std::array<bool, meshes.size()> shapeValid;
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
							if (reference.m_Name.starts_with("Sphere_") || reference.m_Name.starts_with("SweepSphere_"))
								shapeValid[shape] &= std::abs((position - reference.m_Center).Length() - 0.8f) < 0.0001f;
							if (reference.m_Name.starts_with("SweepSphere_"))
								shapeValid[shape] &= normals[corner].Dot((position - reference.m_Center) / 0.8f) > 0.998f;
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
			const auto physicalReferences = BuildLightingContractReferenceViews(true);
			const bool physicalRegistered = rig.SetReferenceViews({ physicalReferences.begin(), physicalReferences.end() });
			bool physicalRestored = physicalRegistered;
			for (const auto& reference : physicalReferences)
			{
				physicalRestored &= rig.RestoreReferenceView(reference.m_Id) && camera.GetManualEV100() == 15.0f &&
					reference.m_ProfileVersion == 2;
			}
			context.Check(physicalRestored && LightingContractReferenceViews.front().m_ManualEV100 == 0.0f,
				"Physical sun reference views restore daylight exposure without mutating the default zero-EV reference");
			for (const bool physicalSun : { false, true })
			{
				const auto extended = BuildLightingContractReferenceViews(physicalSun, true);
				bool restored = extended.size() == 6 &&
					rig.SetReferenceViews({ extended.begin(), extended.end() });
				for (const auto& reference : LightingContractMaterialReferenceViews)
				{
					restored &= rig.RestoreReferenceView(reference.m_Id) &&
						(camera.GetPosition() - reference.m_Position).Length() < 0.0001f &&
						camera.GetFov() == reference.m_VerticalFovDegrees &&
						camera.GetManualEV100() == (physicalSun ? 15.0f : 0.0f);
				}
				context.Check(restored, std::format(
					"Extended material views restore clearcoat and anisotropy with physical sun {}", physicalSun));
			}
			const auto original = BuildLightingContractReferenceViews(false);
			context.Check(original.size() == 4 && rig.SetReferenceViews({ original.begin(), original.end() }) &&
				!rig.RestoreReferenceView("CAM_Clearcoat") && !rig.RestoreReferenceView("CAM_Anisotropy"),
				"Returning to the original contract removes views for absent material stations");

		}

		void CheckMaterialReferenceImports(SelfTestContext& context) noexcept
		{
			const auto assetRoot = GetApplicationSelfTestAssetRoot();
			for (const auto path : {
				"Models/GGLabMaterialExportProbe/GGLabMaterialExportProbe.gltf",
				"Models/GGLabMaterialExportProbe/GGLabMaterialIOROnly.gltf" })
			{
				const auto imported = ModelImporter::Import(ResolveAssetPath(assetRoot, path), {});
				context.Check(imported.Succeeded(),
					std::format("Installed Blender material probe imports through Assimp ({}): {}",
						path, imported.m_Error));
				if (!imported.Succeeded()) continue;
				const auto& model = imported.m_Model;
				const bool hasIor = std::ranges::any_of(model.m_Materials,
					[](const ImportedMaterial& material) noexcept
					{ return std::abs(material.m_Properties.m_Ior - 1.33f) < 0.0001f; });
				const bool hasCoreInputs = std::ranges::any_of(model.m_Materials,
					[](const ImportedMaterial& material) noexcept
					{
						return std::abs(material.m_Properties.m_NormalScale - 0.35f) < 0.0001f &&
							std::abs(material.m_Properties.m_OcclusionStrength - 0.6f) < 0.0001f;
					});
				context.Check(hasIor && hasCoreInputs && !model.m_MeshInstances.empty() &&
					!model.m_TextureSources.empty(),
					"Blender IOR, core texture scalars, geometry and external images reach one imported model");
				const auto coat = std::ranges::find(model.m_Materials,
					"MAT_ClearcoatTexture", &ImportedMaterial::m_Name);
				const bool coatBindings = coat != model.m_Materials.end() &&
					coat->m_Properties.m_ClearcoatFactor == 1.0f &&
					coat->m_Properties.m_ClearcoatRoughness == 1.0f &&
					std::abs(coat->m_Properties.m_ClearcoatNormalScale - 0.42f) < 0.0001f &&
					coat->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Clearcoat)].m_TextureIndex !=
						ImportedMaterialTextureBinding::InvalidTextureIndex &&
					coat->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::ClearcoatRoughness)].m_TexCoordIndex == 1 &&
					coat->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::ClearcoatNormal)].m_TexCoordIndex == 1;
				context.Check(coatBindings,
					"Blender clearcoat channels, independent UV sets and normal scale survive Assimp import");
				const auto factor = std::ranges::find(model.m_Materials,
					"MAT_AnisotropyFactor", &ImportedMaterial::m_Name);
				const auto textured = std::ranges::find(model.m_Materials,
					"MAT_AnisotropyTexture", &ImportedMaterial::m_Name);
				context.Check(factor != model.m_Materials.end() &&
					textured != model.m_Materials.end() &&
					std::abs(factor->m_Properties.m_AnisotropyStrength - 0.7f) < 0.0001f &&
					std::abs(factor->m_Properties.m_AnisotropyRotation - 0.785398f) < 0.0001f &&
					textured->m_Properties.m_AnisotropyStrength == 1.0f &&
					textured->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Anisotropy)].m_TexCoordIndex == 1 &&
					textured->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Anisotropy)].m_TextureIndex !=
						ImportedMaterialTextureBinding::InvalidTextureIndex,
					"Blender anisotropy factors and textured UV1 direction survive Assimp import");
			}

			const auto lighting = ModelImporter::Import(ResolveAssetPath(assetRoot,
				"Models/GGLabLightingContractMaterialReferences/GGLabLightingContract.gltf"), {});
			context.Check(lighting.Succeeded(),
				std::format("Extended lighting contract imports: {}", lighting.m_Error));
			if (lighting.Succeeded())
			{
				const auto& model = lighting.m_Model;
				bool iorRow = true;
				std::string iorDetail;
				for (const float ior : { 1.0f, 1.33f, 1.5f, 1.7f, 2.0f })
				{
					const bool found = std::ranges::any_of(model.m_Materials,
						[ior](const ImportedMaterial& material) noexcept
						{
							const auto& properties = material.m_Properties;
							return std::abs(properties.m_Ior - ior) < 0.0001f &&
								std::abs(properties.m_RoughnessFactor - 0.25f) < 0.0001f &&
								properties.m_MetallicFactor == 0.0f;
						});
					iorRow &= found;
					iorDetail += std::format(" {}:{}", ior, found);
				}
				context.Check(iorRow, std::format(
					"Extended lighting contract retains five matched opaque dielectric IOR references "
					"(instances={}, materials={}, found={})",
					model.m_MeshInstances.size(), model.m_Materials.size(), iorDetail));
				const auto findCoat = [&](std::string_view name) -> const ImportedMaterial*
				{
					const auto found = std::ranges::find(model.m_Materials, name, &ImportedMaterial::m_Name);
					return found == model.m_Materials.end() ? nullptr : &*found;
				};
				const auto* off = findCoat("MAT_Clearcoat_Off");
				const auto* smooth = findCoat("MAT_Clearcoat_Smooth");
				const auto* rough = findCoat("MAT_Clearcoat_Rough");
				const auto* normal = findCoat("MAT_Clearcoat_NormalScale");
				context.Check(off && smooth && rough && normal &&
					off->m_Properties.m_ClearcoatFactor == 0.0f &&
					std::abs(smooth->m_Properties.m_ClearcoatFactor - 0.8f) < 0.0001f &&
					std::abs(smooth->m_Properties.m_ClearcoatRoughness - 0.08f) < 0.0001f &&
					std::abs(rough->m_Properties.m_ClearcoatRoughness - 0.32f) < 0.0001f &&
					std::abs(normal->m_Properties.m_ClearcoatNormalScale - 0.45f) < 0.0001f &&
					normal->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::ClearcoatNormal)].m_TextureIndex !=
						ImportedMaterialTextureBinding::InvalidTextureIndex,
					"Extended lighting contract preserves off, smooth, rough and coat-normal references");
				struct AnisotropyReference
				{
					const char* m_Name;
					float m_Strength;
					float m_Rotation;
					bool m_Mirrored;
					bool m_NormalMapped;
				};
				const std::array<AnisotropyReference, 6> references = { {
					{ "MAT_Anisotropy_Off", 0.0f, 0.0f, false, false },
					{ "MAT_Anisotropy_Along", 0.85f, 0.0f, false, false },
					{ "MAT_Anisotropy_Across", 0.85f, std::numbers::pi_v<float> * 0.5f, false, false },
					{ "MAT_Anisotropy_Diagonal", 0.85f, std::numbers::pi_v<float> * 0.25f, false, false },
					{ "MAT_Anisotropy_Mirrored", 0.85f, std::numbers::pi_v<float> * 0.25f, true, false },
					{ "MAT_Anisotropy_Normal", 0.85f, std::numbers::pi_v<float> * 0.25f, false, true },
				} };
				for (size_t index = 0; index < references.size(); ++index)
				{
					const auto& reference = references[index];
					const auto material = std::ranges::find(model.m_Materials, reference.m_Name, &ImportedMaterial::m_Name);
					bool valuesValid = material != model.m_Materials.end();
					if (valuesValid)
					{
						const auto& properties = material->m_Properties;
						const auto& normalBinding = material->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Normal)];
						valuesValid = std::abs(properties.m_AnisotropyStrength - reference.m_Strength) < 0.0001f &&
							std::abs(properties.m_AnisotropyRotation - reference.m_Rotation) < 0.0001f &&
							properties.m_MetallicFactor == 1.0f && std::abs(properties.m_RoughnessFactor - 0.42f) < 0.0001f &&
							properties.m_ClearcoatFactor == 0.0f &&
							(normalBinding.m_TextureIndex != ImportedMaterialTextureBinding::InvalidTextureIndex) ==
								reference.m_NormalMapped;
						if (reference.m_NormalMapped)
							valuesValid &= std::abs(properties.m_NormalScale - 0.4f) < 0.0001f && normalBinding.m_TexCoordIndex == 0;
					}
					context.Check(valuesValid, std::format("{} retains factors, rotation and base-normal binding", reference.m_Name));
					const Vector3 front(80.0f + 2.2f * static_cast<float>(index % 3),
						0.8f, 2.6f * static_cast<float>(index / 3) - 0.8f);
					bool frontFound = false;
					bool basisValid = true;
					for (const auto& instance : model.m_MeshInstances)
					{
						if (model.m_Materials[instance.m_MaterialIndex].m_Name != reference.m_Name) continue;
						const auto normalMatrix = math::CreateNormalMatrix(instance.m_LocalTransform);
						for (const auto& vertex : model.m_Meshes[instance.m_MeshIndex].m_Vertices)
						{
							const auto position = math::TransformPoint(vertex.m_Position, instance.m_LocalTransform);
							if ((position - front).Length() > 0.0001f) continue;
							frontFound = true;
							const auto n = math::TransformDirection(vertex.m_Normal, normalMatrix);
							const auto t = math::TransformDirection(
								{ vertex.m_Tangent.m_X, vertex.m_Tangent.m_Y, vertex.m_Tangent.m_Z }, instance.m_LocalTransform);
							const auto b = n.Cross(t) * vertex.m_Tangent.m_W;
							basisValid &= (n + Vector3::UnitZ).Length() < 0.0001f &&
								(t - (reference.m_Mirrored ? -Vector3::UnitX : Vector3::UnitX)).Length() < 0.0001f &&
								(b - Vector3::UnitY).Length() < 0.0001f &&
								std::abs(vertex.m_TexCoord0.m_X - (reference.m_Mirrored ? 0.75f : 0.25f)) < 0.0001f &&
								std::abs(vertex.m_TexCoord0.m_Y - 0.5f) < 0.0001f;
						}
					}
					context.Check(frontFound && basisValid,
						std::format("{} preserves the authored front tangent, mirror handedness and UV after left-handed import", reference.m_Name));
				}
				context.Check(model.m_TextureSources.size() == 2, "Extended material references retain both normal images");
				CheckImportedTextures(context, model);
			}

			const auto atrium = ModelImporter::Import(ResolveAssetPath(assetRoot,
				"Models/GGLabCoastalAtriumResearchLounge/GGLabCoastalAtrium.gltf"), {});
			context.Check(atrium.Succeeded() && !atrium.m_Model.m_MeshInstances.empty() &&
				atrium.m_Model.m_TextureSources.size() == 15,
				std::format("Coastal atrium geometry and all surface textures import "
					"(instances={}, textures={}): {}", atrium.m_Model.m_MeshInstances.size(),
					atrium.m_Model.m_TextureSources.size(), atrium.m_Error));
			if (!atrium.Succeeded() || atrium.m_Model.m_TextureSources.size() != 15)
			{
				return;
			}
			const auto atriumTextures = CheckImportedTextures(context, atrium.m_Model);
			constexpr std::array<std::pair<std::string_view, size_t>, 11> atriumTriangles = { {
				{ "MAT_Concrete", 3054 },
				{ "MAT_Paving", 156 },
				{ "MAT_Structure", 10812 },
				{ "MAT_CoastalRock", 3902 },
				{ "MAT_OceanPlaceholder", 2 },
				{ "MAT_LoungeCoatedShell", 1460 },
				{ "MAT_LoungeBrushedAluminum", 1504 },
				{ "MAT_LoungeUpholstery", 752 },
				{ "MAT_LoungeJoints", 752 },
				{ "MAT_ServicePaint", 1296 },
				{ "MAT_ServiceSeal", 968 },
			} };
			std::array<size_t, atriumTriangles.size()> importedTriangles{};
			bool geometryValid = true;
			for (const auto& mesh : atrium.m_Model.m_Meshes)
			{
				geometryValid &= mesh.m_HasBounds && !mesh.m_Vertices.empty() && mesh.m_Indices.size() % 3 == 0;
				for (const auto index : mesh.m_Indices)
					geometryValid &= index < mesh.m_Vertices.size();
				for (const auto& vertex : mesh.m_Vertices)
				{
					const Vector3 tangent(vertex.m_Tangent.m_X, vertex.m_Tangent.m_Y, vertex.m_Tangent.m_Z);
					geometryValid &= std::isfinite(vertex.m_Position.m_X) && std::isfinite(vertex.m_Position.m_Y) &&
						std::isfinite(vertex.m_Position.m_Z) && std::isfinite(vertex.m_TexCoord0.m_X) &&
						std::isfinite(vertex.m_TexCoord0.m_Y) && std::abs(vertex.m_Normal.LengthSquared() - 1.0f) < 0.0002f &&
						std::abs(tangent.LengthSquared() - 1.0f) < 0.0002f &&
						std::abs(vertex.m_Normal.Dot(tangent)) < 0.0002f && std::abs(std::abs(vertex.m_Tangent.m_W) - 1.0f) < 0.0002f;
				}
			}
			// Assimp merges compatible meshes; count placed triangles through material bindings.
			for (const auto& instance : atrium.m_Model.m_MeshInstances)
			{
				if (instance.m_MeshIndex >= atrium.m_Model.m_Meshes.size() ||
					instance.m_MaterialIndex >= atrium.m_Model.m_Materials.size())
				{
					geometryValid = false;
					continue;
				}
				const auto& material = atrium.m_Model.m_Materials[instance.m_MaterialIndex];
				geometryValid &= material.m_Properties.m_AlphaMode == AlphaMode::Opaque;
				const auto match = std::ranges::find(atriumTriangles, material.m_Name,
					&std::pair<std::string_view, size_t>::first);
				if (match == atriumTriangles.end()) { geometryValid = false; continue; }
				importedTriangles[static_cast<size_t>(match - atriumTriangles.begin())] +=
					atrium.m_Model.m_Meshes[instance.m_MeshIndex].m_Indices.size() / 3;
			}
			context.Check(geometryValid, "Coastal atrium retains valid geometry, opaque bindings and orthonormal imported tangent frames");
			for (size_t index = 0; index < atriumTriangles.size(); ++index)
				context.Check(importedTriangles[index] == atriumTriangles[index].second,
					std::format("Coastal atrium {} retains {} placed triangles (imported={})",
						atriumTriangles[index].first, atriumTriangles[index].second, importedTriangles[index]));
			const auto rock = std::ranges::find(atrium.m_Model.m_Materials, "MAT_CoastalRock", &ImportedMaterial::m_Name);
			bool rockBindingsValid = rock != atrium.m_Model.m_Materials.end();
			if (rockBindingsValid)
			{
				rockBindingsValid &= rock->m_Properties.m_MetallicFactor == 0.0f && rock->m_Properties.m_NormalScale == 1.0f;
				for (const auto& [slot, filename] : std::array{
					std::pair{ MaterialTextureSlot::BaseColor, "CoastalRock_BaseColor.png" },
					std::pair{ MaterialTextureSlot::Normal, "CoastalRock_Normal.png" },
					std::pair{ MaterialTextureSlot::MetallicRoughness, "CoastalRock_MetallicRoughness.png" } })
				{
					const auto& binding = rock->m_TextureBindings[static_cast<size_t>(slot)];
					rockBindingsValid &= binding.m_TexCoordIndex == 0 && binding.m_TextureIndex < atrium.m_Model.m_TextureSources.size();
					if (binding.m_TextureIndex < atrium.m_Model.m_TextureSources.size())
					{
						const auto& source = atrium.m_Model.m_TextureSources[binding.m_TextureIndex];
						rockBindingsValid &= source.m_CanonicalPath.filename() == filename && source.m_Semantic == GetMaterialTextureSlotSemantic(slot);
					}
				}
			}
			context.Check(rockBindingsValid, "Coastal rock retains dedicated UV0 color, normal and metallic-roughness bindings");
			const auto concrete = std::ranges::find(atrium.m_Model.m_Materials, "MAT_Concrete", &ImportedMaterial::m_Name);
			bool concreteBindingsValid = concrete != atrium.m_Model.m_Materials.end();
			if (concreteBindingsValid)
			{
				concreteBindingsValid &= concrete->m_Properties.m_MetallicFactor == 0.0f &&
					concrete->m_Properties.m_RoughnessFactor == 1.0f && concrete->m_Properties.m_NormalScale == 1.0f;
				for (const auto& [slot, filename] : std::array{
					std::pair{ MaterialTextureSlot::BaseColor, "Concrete_BaseColor.png" },
					std::pair{ MaterialTextureSlot::Normal, "Concrete_Normal.png" },
					std::pair{ MaterialTextureSlot::MetallicRoughness, "Concrete_MetallicRoughness.png" } })
				{
					const auto& binding = concrete->m_TextureBindings[static_cast<size_t>(slot)];
					concreteBindingsValid &= binding.m_TexCoordIndex == 0 && binding.m_TextureIndex < atriumTextures.size();
					if (binding.m_TextureIndex < atriumTextures.size())
					{
						const auto& source = atrium.m_Model.m_TextureSources[binding.m_TextureIndex];
						const auto& texture = atriumTextures[binding.m_TextureIndex];
						concreteBindingsValid &= source.m_CanonicalPath.filename() == filename &&
							source.m_Semantic == GetMaterialTextureSlotSemantic(slot) &&
							texture.m_Extent.m_Width == 1024 && texture.m_Extent.m_Height == 1024 && texture.m_MipLevels == 11;
					}
				}
			}
			context.Check(concreteBindingsValid, "Refined concrete retains UV0 factors and three 1024-square semantic textures with complete mip chains");
			struct SurfaceReference
			{
				std::string_view m_Name;
				std::string_view m_Prefix;
				float m_Metallic;
			};
			constexpr std::array<SurfaceReference, 3> surfaces = { {
				{ "MAT_Paving", "Stone", 0.0f },
				{ "MAT_Structure", "Metal", 1.0f },
				{ "MAT_LoungeUpholstery", "Upholstery", 0.0f },
			} };
			for (const auto& surface : surfaces)
			{
				const auto material = std::ranges::find(atrium.m_Model.m_Materials,
					surface.m_Name, &ImportedMaterial::m_Name);
				bool valid = material != atrium.m_Model.m_Materials.end();
				if (valid)
				{
					const auto& properties = material->m_Properties;
					valid &= properties.m_MetallicFactor == surface.m_Metallic &&
						properties.m_RoughnessFactor == 1.0f && properties.m_NormalScale == 1.0f &&
						properties.m_ClearcoatFactor == 0.0f && properties.m_AnisotropyStrength == 0.0f;
					for (size_t channel = 0; channel < 4; ++channel)
						valid &= properties.m_BaseColor[channel] == 1.0f;
					for (const auto& [slot, suffix] : std::array{
						std::pair{ MaterialTextureSlot::BaseColor, "BaseColor" },
						std::pair{ MaterialTextureSlot::Normal, "Normal" },
						std::pair{ MaterialTextureSlot::MetallicRoughness, "MetallicRoughness" } })
					{
						const auto& binding = material->m_TextureBindings[static_cast<size_t>(slot)];
						valid &= binding.m_TexCoordIndex == 0 && binding.m_TextureIndex < atriumTextures.size() &&
							binding.m_SamplerKey.m_AddressU == RHITextureAddressMode::Wrap &&
							binding.m_SamplerKey.m_AddressV == RHITextureAddressMode::Wrap;
						if (binding.m_TextureIndex < atriumTextures.size())
						{
							const auto& source = atrium.m_Model.m_TextureSources[binding.m_TextureIndex];
							const auto& texture = atriumTextures[binding.m_TextureIndex];
							valid &= source.m_CanonicalPath.filename() == std::format("{}_{}.png", surface.m_Prefix, suffix) &&
								source.m_Semantic == GetMaterialTextureSlotSemantic(slot) &&
								texture.m_Extent.m_Width == 1024 && texture.m_Extent.m_Height == 1024 && texture.m_MipLevels == 11;
						}
					}
					for (const auto slot : { MaterialTextureSlot::Occlusion, MaterialTextureSlot::Emissive,
						MaterialTextureSlot::Clearcoat, MaterialTextureSlot::ClearcoatRoughness,
						MaterialTextureSlot::ClearcoatNormal, MaterialTextureSlot::Anisotropy })
						valid &= material->m_TextureBindings[static_cast<size_t>(slot)].m_TextureIndex ==
							ImportedMaterialTextureBinding::InvalidTextureIndex;
				}
				context.Check(valid, std::format("{} retains opaque isotropic UV0 factors, repeating semantic textures and complete 1024-square mip chains",
					surface.m_Name));
			}
			const auto coatedShell = std::ranges::find(atrium.m_Model.m_Materials,
				"MAT_LoungeCoatedShell", &ImportedMaterial::m_Name);
			context.Check(coatedShell != atrium.m_Model.m_Materials.end() &&
				std::abs(coatedShell->m_Properties.m_ClearcoatFactor - 0.82f) < 0.0001f &&
				std::abs(coatedShell->m_Properties.m_ClearcoatRoughness - 0.11f) < 0.0001f,
				"Research lounge coated shell retains its authored clearcoat layer");
			const auto frame = std::ranges::find(atrium.m_Model.m_Materials,
				"MAT_LoungeBrushedAluminum", &ImportedMaterial::m_Name);
			context.Check(frame != atrium.m_Model.m_Materials.end() &&
				frame->m_Properties.m_MetallicFactor == 1.0f &&
				std::abs(frame->m_Properties.m_RoughnessFactor - 0.28f) < 0.0001f &&
				std::abs(frame->m_Properties.m_AnisotropyStrength - 0.78f) < 0.0001f &&
				frame->m_Properties.m_AnisotropyRotation == 0.0f,
				"Research lounge frame retains its brushed-metal anisotropy");
		}

		void CheckAnisotropyReferenceImports(SelfTestContext& context) noexcept
		{
			const auto assetRoot = GetApplicationSelfTestAssetRoot();
			const auto import = [&](const char* path)
			{
				return ModelImporter::Import(ResolveAssetPath(assetRoot, path), {});
			};
			const auto checkDirectionTextures = [&](const ImportedModel& model, std::string_view name)
			{
				bool found = false;
				bool valid = true;
				for (const auto& source : model.m_TextureSources)
				{
					if (source.m_Semantic != TextureSemantic::Anisotropy) continue;
					found = true;
					const auto texture = TextureLoader::LoadTextureData(
						source.m_CanonicalPath, source.m_ImportSettings);
					valid &= texture.IsValid() && texture.m_ColorSpace == TextureColorSpace::Linear &&
						texture.m_ViewFormat == RHIFormat::R8G8B8A8Unorm;
				}
				context.Check(found && valid,
					std::format("{} direction textures decode as linear data", name));
			};
			const auto strength = import("Models/AnisotropyStrengthTest/AnisotropyStrengthTest.gltf");
			context.Check(strength.Succeeded() && strength.m_Model.m_MeshInstances.size() >= 49,
				std::format("Anisotropy strength grid imports with its geometry: {}", strength.m_Error));
			if (strength.Succeeded())
			{
				const auto hasEndpoint = [&](float roughness, float anisotropy)
				{
					return std::ranges::any_of(strength.m_Model.m_Materials,
						[&](const ImportedMaterial& material) noexcept
						{
							return std::abs(material.m_Properties.m_RoughnessFactor - roughness) < 0.0001f &&
								std::abs(material.m_Properties.m_AnisotropyStrength - anisotropy) < 0.0001f;
						});
				};
				context.Check(hasEndpoint(0.0f, 0.0f) && hasEndpoint(0.0f, 1.0f) &&
					hasEndpoint(1.0f, 0.0f) && hasEndpoint(1.0f, 1.0f),
					"Anisotropy strength grid retains the roughness and strength endpoints");
			}

			const auto rotation = import("Models/AnisotropyRotationTest/AnisotropyRotationTest.gltf");
			context.Check(rotation.Succeeded() && rotation.m_Model.m_MeshInstances.size() >= 6,
				std::format("Anisotropy rotation reference imports with its geometry: {}", rotation.m_Error));
			if (rotation.Succeeded())
			{
				const auto rotated = std::ranges::find(rotation.m_Model.m_Materials,
					"Aniso Tan + Rotation", &ImportedMaterial::m_Name);
				const auto combined = std::ranges::find(rotation.m_Model.m_Materials,
					"Aniso Tan + Rotation + Texture", &ImportedMaterial::m_Name);
				const bool hasDirectionTexture = combined != rotation.m_Model.m_Materials.end() &&
					combined->m_TextureBindings[static_cast<size_t>(MaterialTextureSlot::Anisotropy)].m_TextureIndex !=
						ImportedMaterialTextureBinding::InvalidTextureIndex;
				context.Check(rotated != rotation.m_Model.m_Materials.end() &&
					std::abs(rotated->m_Properties.m_AnisotropyRotation - 0.523599f) < 0.0001f &&
					combined != rotation.m_Model.m_Materials.end() &&
					std::abs(combined->m_Properties.m_AnisotropyRotation - 0.349066f) < 0.0001f &&
					hasDirectionTexture,
					"Anisotropy rotation reference retains factor and texture direction paths");
				checkDirectionTextures(rotation.m_Model, "Anisotropy rotation reference");
			}

			const auto disc = import("Models/AnisotropyDiscTest/AnisotropyDiscTest.gltf");
			context.Check(disc.Succeeded() && disc.m_Model.m_MeshInstances.size() >= 12,
				std::format("Anisotropy disc reference imports with its geometry: {}", disc.m_Error));
			if (disc.Succeeded())
			{
				const bool anisotropyTexture = std::ranges::any_of(disc.m_Model.m_TextureSources,
					[](const ImportedTextureSource& source) noexcept
					{
						std::error_code error;
						return source.m_Semantic == TextureSemantic::Anisotropy &&
							std::filesystem::exists(source.m_CanonicalPath, error) && !error;
					});
				context.Check(anisotropyTexture,
					"Anisotropy disc reference resolves its direction and strength texture");
				checkDirectionTextures(disc.m_Model, "Anisotropy disc reference");
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

		void CheckCoastalSceneCameraPaths(SelfTestContext& context) noexcept
		{
			Camera camera(Camera::CreateInfo{ .m_Width = 1920, .m_Height = 1080 });
			CameraController controller(CameraController::CreateInfo{});
			CameraRig rig;
			rig.AttachMainCamera(camera, controller);
			const std::vector<CameraPath> paths = MakeCoastalSceneCameraPaths();
			const bool registered = rig.SetReferenceViews(
				{ CoastalSceneReferenceViews.begin(), CoastalSceneReferenceViews.end() }) &&
				rig.SetCameraPaths(paths);
			context.Check(registered && paths.size() == 9,
				"Nine coastal temporal evaluation paths register on the main camera");
			if (!registered) return;

			// Each path starts at the reference view it was copied from; changing either
			// side requires a deliberate edit and a path version change.
			struct Expected
			{
				std::string_view m_PathId;
				std::string_view m_ViewId;
				uint32_t m_FrameCount;
				uint32_t m_Cuts;
			};
			constexpr std::array<Expected, 9> expected{ {
				{ "SEQ_StaticRailings", "CAM_ShadowStairs", 96, 1 },
				{ "SEQ_LateralPanRailings", "CAM_ShadowStairs", 180, 1 },
				{ "SEQ_PanStopRailings", "CAM_ShadowStairs", 180, 1 },
				{ "SEQ_DollyDoorway", "CAM_InteriorExterior", 180, 1 },
				{ "SEQ_OrbitLounge", "Retreat_Lounge", 241, 1 },
				{ "SEQ_HorizonPan", "CAM_SkyHorizon", 181, 1 },
				{ "SEQ_CutCourtyardToStairs", "CAM_Courtyard", 120, 2 },
				{ "SEQ_StaticGlassTerrace", "Retreat_GlassTerrace", 96, 1 },
				{ "SEQ_PanGlassTerrace", "Retreat_GlassTerrace", 180, 1 },
			} };
			for (const Expected& entry : expected)
			{
				const CameraPath* path = rig.FindCameraPath(entry.m_PathId);
				const auto view = std::ranges::find(
					CoastalSceneReferenceViews, entry.m_ViewId, &CameraReferenceView::m_Id);
				const std::optional<CameraPathPose> start =
					path ? EvaluateCameraPath(*path, 0) : std::nullopt;
				context.Check(path && view != CoastalSceneReferenceViews.end() && start &&
					path->m_Version == 1 && GetCameraPathFrameCount(*path) == entry.m_FrameCount &&
					(start->m_Position - view->m_Position).LengthSquared() == 0.0f &&
					(start->m_Target - view->m_Target).LengthSquared() == 0.0f &&
					start->m_VerticalFovDegrees == view->m_VerticalFovDegrees &&
					path->m_NearPlane == view->m_NearPlane && path->m_FarPlane == view->m_FarPlane &&
					path->m_ManualEV100 == view->m_ManualEV100,
					std::format("{} starts at {} with {} frames", entry.m_PathId, entry.m_ViewId,
						entry.m_FrameCount));
				if (!path) continue;

				const uint64_t serial = camera.GetTemporalResetSerial();
				bool applied = true;
				for (uint32_t frame = 0; frame < entry.m_FrameCount; ++frame)
				{
					applied = applied && rig.ApplyCameraPathFrame(entry.m_PathId, frame).has_value();
				}
				context.Check(applied && camera.GetTemporalResetSerial() == serial + entry.m_Cuts,
					std::format("{} applies every frame and resets temporal history only at its "
						"{} cut(s)", entry.m_PathId, entry.m_Cuts));
			}
		}

		void CheckCoastalSceneReferenceViews(SelfTestContext& context) noexcept
		{
			Camera camera(Camera::CreateInfo{ .m_Width = 1920, .m_Height = 1080 });
			CameraController controller(CameraController::CreateInfo{});
			CameraRig rig;
			rig.AttachMainCamera(camera, controller);
			const bool registered = rig.SetReferenceViews(
				{ CoastalSceneReferenceViews.begin(), CoastalSceneReferenceViews.end() });
			context.Check(registered && CoastalSceneReferenceViews.size() == 14,
				"Eight retained atrium views, five coastal retreat views and one temporal "
				"evaluation view register in runtime coordinates");
			if (!registered) return;
			for (const auto& reference : CoastalSceneReferenceViews)
			{
				const bool restored = rig.RestoreReferenceView(reference.m_Id);
				const Vector3 targetInView = math::TransformPoint(reference.m_Target, camera.GetViewMatrix());
				context.Check(restored && (camera.GetPosition() - reference.m_Position).LengthSquared() == 0.0f &&
					std::abs(targetInView.m_X) < 0.0001f && std::abs(targetInView.m_Y) < 0.0001f &&
					targetInView.m_Z > 0.0f && camera.GetFov() == reference.m_VerticalFovDegrees &&
					camera.GetNear() == reference.m_NearPlane && camera.GetFar() == reference.m_FarPlane,
					std::format("{} looks at its authored target with the intended perspective projection", reference.m_Id));
				const auto exposure = ResolveViewRenderSettings(ViewRenderProfile{}, camera).m_Exposure;
				const float daylightScale = 1.0f / (1.2f * 32768.0f);
				context.Check(reference.m_ProfileVersion == 2 && camera.GetFar() == 6000.0f &&
					camera.GetManualEV100() == 15.0f &&
					exposure.m_EffectiveEV100 == 15.0f && exposure.m_ExposureScale == daylightScale &&
					exposure.m_PreExposure == daylightScale,
					std::format("{} retains the coastal range, EV100 15 and consistent physical daylight pre-exposure", reference.m_Id));
				const auto view = camera.GetViewMatrix().ToArray();
				const auto projection = camera.GetProjMatrix().ToArray();
				camera.SetYawPitch(0.5f, 0.2f);
				camera.SetFov(75.0f);
				camera.SetNearFar(0.5f, 500.0f);
				camera.SetManualEV100(5.0f);
				camera.SetExposureCompensationEV(2.0f);
				controller.Update(camera, CameraInput{ .m_Front = true }, 0.1f);
				const auto serial = camera.GetTemporalResetSerial();
				const bool restoredAgain = rig.RestoreReferenceView(reference.m_Id);
				controller.Update(camera, CameraInput{}, 0.1f);
				camera.Update();
				context.Check(restoredAgain && camera.GetViewMatrix().ToArray() == view &&
					camera.GetProjMatrix().ToArray() == projection && camera.GetManualEV100() == 15.0f &&
					camera.GetExposureCompensationEV() == 0.0f &&
					camera.GetTemporalResetSerial() == serial + 1 && rig.GetLastRestoredReferenceId() == reference.m_Id,
					std::format("{} restores identical matrices after movement and lens edits; runtime yaw/pitch {:.9g}, {:.9g}; "
						"vertical FOV {:.9g} deg; aspect {:.9g}", reference.m_Id,
						camera.GetYaw(), camera.GetPitch(), camera.GetFov(), camera.GetAspect()));
			}
		}

		void CheckCoastalRetreatContent(SelfTestContext& context) noexcept
		{
			const auto imported = ModelImporter::Import(ResolveAssetPath(GetApplicationSelfTestAssetRoot(),
				"Models/GGLabCoastalRetreat/GGLabCoastalRetreat.gltf"), {});
			context.Check(imported.Succeeded() && imported.m_Model.m_TextureSources.size() == 24,
				std::format("Coastal retreat geometry and twenty-four surface textures import: {}", imported.m_Error));
			if (!imported.Succeeded()) return;
			const auto& model = imported.m_Model;
			CheckImportedTextures(context, model);
			constexpr std::array<std::pair<std::string_view, size_t>, 24> expectedTriangles = { {
				{ "MAT_RetreatStone", 220 },
				{ "MAT_RetreatLime", 4754 },
				{ "MAT_RetreatMetal", 10268 },
				{ "MAT_RetreatTimber", 75472 },
				{ "MAT_CoastalRock", 6048 },
				{ "MAT_LoungeCoatedShell", 1460 },
				{ "MAT_LoungeUpholstery", 752 },
				{ "MAT_LoungeJoints", 752 },
				{ "MAT_LoungeBrushedAluminum", 12560 },
				{ "MAT_RetreatPaint", 2832 },
				{ "MAT_ServiceSeal", 3392 },
				{ "MAT_RetreatCeramic", 1784 },
				{ "MAT_RetreatPaper", 188 },
				{ "MAT_RetreatSoil", 36 },
				{ "MAT_RetreatLeaf", 722280 },
				{ "MAT_RetreatSilverLeaf", 192952 },
				{ "MAT_RetreatDryLeaf", 229448 },
				{ "MAT_RetreatSea", 2 },
				{ "MAT_RetreatDistantRock", 2298 },
				{ "MAT_CoastalCanopyGlass", 1176 },
				{ "MAT_DockFender", 8476 },
				{ "MAT_LandingRope", 23808 },
				{ "MAT_LifebuoyRed", 1024 },
				{ "MAT_LifebuoyWhite", 1024 },
			} };
			std::array<size_t, expectedTriangles.size()> triangles{};
			bool geometryValid = true;
			std::string geometryFailure;
			// Assimp appends one anonymous default material to this glTF. Require
			// exactly one copy of every source material and keep the default unbound.
			bool materialBindingsValid = model.m_Materials.size() == expectedTriangles.size() + 1 &&
				std::ranges::count(model.m_Materials, std::string{}, &ImportedMaterial::m_Name) == 1 &&
				std::ranges::all_of(expectedTriangles, [&](const auto& entry)
					{ return std::ranges::count(model.m_Materials, entry.first, &ImportedMaterial::m_Name) == 1; });
			for (const auto& mesh : model.m_Meshes)
			{
				geometryValid &= mesh.m_HasBounds && !mesh.m_Vertices.empty() && mesh.m_Indices.size() % 3 == 0;
				for (const auto index : mesh.m_Indices)
					geometryValid &= index < mesh.m_Vertices.size();
				for (const auto& vertex : mesh.m_Vertices)
				{
					const Vector3 tangent(vertex.m_Tangent.m_X, vertex.m_Tangent.m_Y, vertex.m_Tangent.m_Z);
					const bool vertexValid = std::isfinite(vertex.m_Position.m_X) && std::isfinite(vertex.m_Position.m_Y) &&
						std::isfinite(vertex.m_Position.m_Z) && std::isfinite(vertex.m_TexCoord0.m_X) &&
						std::isfinite(vertex.m_TexCoord0.m_Y) && std::abs(vertex.m_Normal.LengthSquared() - 1.0f) < 0.0002f &&
						std::abs(tangent.LengthSquared() - 1.0f) < 0.0002f &&
						std::abs(vertex.m_Normal.Dot(tangent)) < 0.0002f && std::abs(std::abs(vertex.m_Tangent.m_W) - 1.0f) < 0.0002f;
					if (!vertexValid && geometryFailure.empty())
						geometryFailure = std::format("; mesh {} at ({}, {}, {}): normal squared length {}, "
							"tangent squared length {}, normal/tangent dot {}, handedness {}", mesh.m_Name,
							vertex.m_Position.m_X, vertex.m_Position.m_Y, vertex.m_Position.m_Z,
							vertex.m_Normal.LengthSquared(), tangent.LengthSquared(), vertex.m_Normal.Dot(tangent),
							vertex.m_Tangent.m_W);
					geometryValid &= vertexValid;
				}
			}
			// Count placed geometry through bindings so shared foliage meshes retain every instance.
			for (const auto& instance : model.m_MeshInstances)
			{
				if (instance.m_MeshIndex >= model.m_Meshes.size() || instance.m_MaterialIndex >= model.m_Materials.size())
				{
					geometryValid = false;
					continue;
				}
				const auto& material = model.m_Materials[instance.m_MaterialIndex];
				const auto& properties = material.m_Properties;
				if (material.m_Name == "MAT_CoastalCanopyGlass")
				{
					// The installed core-glTF approximation must stay transparent until
					// the Runtime supports the source scene's physical transmission.
					materialBindingsValid &= properties.m_AlphaMode == AlphaMode::Blend &&
						std::abs(properties.m_BaseColor[3] - 0.18f) < 0.00001f &&
						std::abs(properties.m_RoughnessFactor - 0.075f) < 0.00001f &&
						properties.m_MetallicFactor == 0.0f && properties.m_Ior == 1.5f;
				}
				else
				{
					materialBindingsValid &= properties.m_AlphaMode == AlphaMode::Opaque &&
						properties.m_BaseColor[3] == 1.0f;
				}
				const auto match = std::ranges::find(expectedTriangles, material.m_Name,
					&std::pair<std::string_view, size_t>::first);
				materialBindingsValid &= match != expectedTriangles.end();
				if (match == expectedTriangles.end()) { geometryValid = false; continue; }
				triangles[static_cast<size_t>(match - expectedTriangles.begin())] +=
					model.m_Meshes[instance.m_MeshIndex].m_Indices.size() / 3;
				for (const auto slot : { MaterialTextureSlot::BaseColor, MaterialTextureSlot::Normal,
					MaterialTextureSlot::MetallicRoughness })
				{
					const auto& binding = material.m_TextureBindings[static_cast<size_t>(slot)];
					if (binding.m_TextureIndex == ImportedMaterialTextureBinding::InvalidTextureIndex) continue;
					geometryValid &= binding.m_TexCoordIndex == 0 && binding.m_TextureIndex < model.m_TextureSources.size();
					if (binding.m_TextureIndex < model.m_TextureSources.size())
						geometryValid &= model.m_TextureSources[binding.m_TextureIndex].m_Semantic == GetMaterialTextureSlotSemantic(slot);
				}
			}
			context.Check(geometryValid,
				"Coastal retreat retains valid geometry, tangent frames and UV0 texture semantics" + geometryFailure);
			std::string materialDetail = std::format(" (allocated materials={})", model.m_Materials.size());
			for (const auto& material : model.m_Materials)
			{
				const auto& properties = material.m_Properties;
				materialDetail += std::format("; {}: alpha mode {}, alpha {}, roughness {}, metallic {}, IOR {}", material.m_Name,
					static_cast<uint32_t>(properties.m_AlphaMode), properties.m_BaseColor[3], properties.m_RoughnessFactor,
					properties.m_MetallicFactor, properties.m_Ior);
			}
			context.Check(materialBindingsValid,
				"Coastal retreat retains twenty-three opaque materials and the explicit glass blend approximation" + materialDetail);
			for (size_t index = 0; index < expectedTriangles.size(); ++index)
				context.Check(triangles[index] == expectedTriangles[index].second,
					std::format("Coastal retreat {} retains {} placed triangles (imported={})",
						expectedTriangles[index].first, expectedTriangles[index].second, triangles[index]));

			// Probe the upper front bevel in model space, independent of Assimp mesh
			// merging. The lowered seat moves these retained bevels down by 0.16 m.
			// Nonplanar source quads once acquired flat normals here despite
			// valid unit normals and tangent frames, producing block-shaped highlights.
			constexpr std::array armCentersX{ 5.04f, 7.66f };
			std::array<std::vector<std::pair<Vector3, Vector3>>, armCentersX.size()> frontCorners;
			for (const auto& instance : model.m_MeshInstances)
			{
				if (instance.m_MeshIndex >= model.m_Meshes.size() || instance.m_MaterialIndex >= model.m_Materials.size())
					continue;
				if (model.m_Materials[instance.m_MaterialIndex].m_Name != "MAT_LoungeCoatedShell")
					continue;
				for (const auto& vertex : model.m_Meshes[instance.m_MeshIndex].m_Vertices)
				{
					const Vector3 position = math::TransformPoint(vertex.m_Position, instance.m_LocalTransform);
					if (position.m_Y < 3.0399f || position.m_Y > 3.2741f ||
						position.m_Z < -1.6151f || position.m_Z > -1.5249f)
						continue;
					// The authored shells have rigid instance transforms.
					const Vector3 normal = math::TransformDirection(vertex.m_Normal, instance.m_LocalTransform).Normalized();
					for (size_t arm = 0; arm < armCentersX.size(); ++arm)
						if (std::abs(position.m_X - armCentersX[arm]) < 0.056f)
							frontCorners[arm].emplace_back(position, normal);
				}
			}
			for (size_t arm = 0; arm < frontCorners.size(); ++arm)
			{
				const auto& corners = frontCorners[arm];
				size_t coincidentPairs = 0;
				float maximumNormalDelta = 0.0f;
				for (size_t first = 0; first < corners.size(); ++first)
					for (size_t second = first + 1; second < corners.size(); ++second)
						if ((corners[first].first - corners[second].first).LengthSquared() < 1e-10f)
						{
							++coincidentPairs;
							maximumNormalDelta = std::max(maximumNormalDelta,
								(corners[first].second - corners[second].second).Length());
						}
				// Allow four-decimal export/encoding noise, while rejecting a real
				// smooth-bevel seam across vertices split by UVs, normals or tangents.
				context.Check(corners.size() >= 12 && coincidentPairs > 0 && maximumNormalDelta < 0.0005f,
					std::format("Coastal retreat {} upper front bevel retains continuous imported normals "
						"(vertices={}, coincident pairs={}, maximum normal delta={:.6f})",
						arm == 0 ? "left" : "right", corners.size(), coincidentPairs, maximumNormalDelta));
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
		context.Check(desktop.IsValid() && desktop.m_Demos.size() == 4 &&
			desktop.m_Labs.size() == 16 && desktopSelection.Succeeded() &&
			std::ranges::any_of(desktop.m_Labs, [](const LabRegistration& lab) noexcept
				{ return lab.m_Descriptor.m_Id == LabId("gglab.lab.temporal_aa"); }),
			"Windows desktop composition includes four Demo entries and sixteen Labs");
		for (const std::string_view retiredLabId : {
			"gglab.lab.task_system",
			"gglab.lab.asset_publication",
			"gglab.lab.asset_residency",
			"gglab.lab.environment_assets" })
		{
			context.Check(ResolveApplicationContentSelection(
				desktop, DesktopLabHostDemoId, retiredLabId).m_Status ==
				ApplicationContentSelectionStatus::StartupLabUnavailable,
				"Retired Lab identities are unavailable");
		}
		const ApplicationContentSelection islandSelection = ResolveApplicationContentSelection(
			desktop, DesktopIslandDemoId, DesktopDefaultLabId);
		context.Check(islandSelection.Succeeded(),
			"Island remains selectable through its stable Demo identity");
		context.Check(!ResolveApplicationContentSelection(
			desktop, "Demo.Playground", DesktopDefaultLabId).Succeeded(),
			"The retired original Playground Demo identity is unavailable");
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
			rendererDemands, shader_programs::TemporalAAReprojectionCompute) != rendererDemands.end() &&
			std::ranges::find(rendererDemands, shader_programs::TemporalAADepthHistoryCompute) !=
				rendererDemands.end(),
			"Renderer artifact demand includes the production Temporal AA compute programs");
		context.Check(std::ranges::find(rendererDemands, shader_programs::AerialPerspectiveBuildCompute) != rendererDemands.end() &&
			std::ranges::find(rendererDemands, shader_programs::AerialPerspectiveCompositeCompute) != rendererDemands.end(),
			"Renderer startup artifacts include both aerial transport programs before any Lab enables atmosphere");
		context.Check(rendererDemands.size() == 50 &&
			std::ranges::find(rendererDemands, shader_programs::IBLImportanceVertex) != rendererDemands.end() &&
			std::ranges::find(rendererDemands, shader_programs::IBLImportancePixel) != rendererDemands.end(),
			"Renderer startup demand includes both IBL importance programs in its 50-program contract");

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
		checkSelectedDemand("gglab.lab.render_graph_compute", 54,
			"Render-graph compute selection contributes four stable shader demands");
		checkSelectedDemand("gglab.lab.coordinate_conformance", 54,
			"Coordinate conformance selection contributes four stable shader demands");
		checkSelectedDemand("gglab.lab.napa_voxel", 52,
			"Napa voxel selection contributes two stable shader demands");
		checkSelectedDemand("gglab.lab.texture_contract", 50,
			"Texture contract uses the production renderer's shader demands");
		checkSelectedDemand("gglab.lab.lighting_contract", 50,
			"Lighting contract is selectable through LabHost with production shader demands");
		CheckLightingContractContent(context);
		checkSelectedDemand("gglab.lab.atmosphere_range", 50,
			"Atmosphere range uses the production renderer's shader demands");
		CheckAtmosphereRangeContent(context);
		CheckIslandContent(context);
		CheckCoastalSceneReferenceViews(context);
		CheckCoastalSceneCameraPaths(context);
		// Keep one COM apartment alive across WIC decoder use, as runtime asset workers do.
		std::thread textureWorker([&]
			{
				const auto workerContext = win32::Win32TaskWorkerLifecycle{}.CreateContext(0);
				CheckMaterialReferenceImports(context);
				CheckAnisotropyReferenceImports(context);
				CheckCoastalAtriumContent(context);
				CheckCoastalRetreatContent(context);
				CheckTextureContractContent(context);
			});
		textureWorker.join();
	}
}
