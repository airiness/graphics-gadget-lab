#include "Application/SelfTest/ApplicationContentRegistrationSelfTests.h"
#include "Application/SelfTest/SelfTestRunner.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "Application/Demo/CoastalAtriumReferenceViews.h"
#include "GGLabTestCore/SelfTest.h"
#include "GGLabRuntime/Core/Math/Transform.h"
#include "GGLabRuntime/Graphics/Asset/ModelImporter.h"
#include "GGLabRuntime/Graphics/Asset/AssetPaths.h"
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
#include <initializer_list>
#include <limits>
#include <numbers>
#include <string_view>
#include <vector>

namespace gglab
{
	namespace
	{
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
			context.Check(model.m_TextureSources.empty() &&
				std::ranges::all_of(model.m_Materials, [](const ImportedMaterial& material) noexcept
					{
						return material.m_Properties.m_AlphaMode == AlphaMode::Opaque &&
							material.m_Properties.m_MetallicFactor == 0.0f;
					}), "Atrium greybox uses opaque untextured dielectric materials");

			// Probe imported world triangles, independent of Assimp mesh merging or names.
			std::vector<std::array<Vector3, 3>> triangles;
			bool bounded = true;
			for (const auto& instance : model.m_MeshInstances)
			{
				const auto& mesh = model.m_Meshes[instance.m_MeshIndex];
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
			desktop.m_Labs.size() == 18 && desktopSelection.Succeeded() &&
			std::ranges::any_of(desktop.m_Labs, [](const LabRegistration& lab) noexcept
				{ return lab.m_Descriptor.m_Id == LabId("gglab.lab.temporal_aa"); }) &&
			std::ranges::any_of(desktop.m_Labs, [](const LabRegistration& lab) noexcept
				{ return lab.m_Descriptor.m_Id == LabId("gglab.lab.shader_graph_preview"); }),
			"Windows desktop composition includes five Demo entries and eighteen Labs");
		const ApplicationContentSelection islandSelection = ResolveApplicationContentSelection(
			desktop, DesktopIslandDemoId, DesktopDefaultLabId);
		context.Check(islandSelection.Succeeded() &&
			std::ranges::any_of(desktop.m_Demos, [](const ApplicationDemoRegistration& demo) noexcept
				{ return demo.m_Id == DesktopPlaygroundDemoId; }),
			"Island is selectable alongside the original Playground content");
		context.Check(ResolveApplicationContentSelection(
			desktop, DesktopCoastalAtriumDemoId, DesktopDefaultLabId).Succeeded(),
			"Coastal atrium is selectable alongside the import prototype");
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
		checkSelectedDemand("gglab.lab.shader_graph_preview", 35,
			"Shader Graph Preview selection contributes both pinned Pixel Program demands");
		CheckIslandContent(context);
		CheckCoastalAtriumContent(context);
		CheckCoastalAtriumReferenceViews(context);
	}
}
