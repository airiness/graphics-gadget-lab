#include "Core/Precompiled.h"
#include "Graphics/RenderScene.h"
#include "Graphics/TransferManager.h"
#include "Graphics/AssetManager.h"
#include "Core/World.h"
#include "Core/Components.h"
#include "Graphics/RenderView.h"

namespace gglab
{
	RenderSceneBuilder::BuildResult RenderSceneBuilder::Build(const BuildInfo& info) noexcept
	{
		BuildResult result{};

		auto& registry = info.m_World.GetRegistry();
		auto& transferManager = info.m_TransferManager;
		auto& assetManager = info.m_AssetManager;

		const auto& renderViews = info.m_RenderViews;

		// Reclaim ring buffer
		const DX12FencePoint completedFence = transferManager.Reclaim();
		if (completedFence.IsValid())
		{
			info.m_ObjectsSB.ReclaimCompleted(completedFence);
			info.m_MaterialsSB.ReclaimCompleted(completedFence);
			info.m_ViewsSB.ReclaimCompleted(completedFence);
		}

		// Assembly RenderInstaces
		result.m_RenderScene.m_RenderInstances.clear();

		// Update Object and Material Structured Buffer
		std::vector<ObjectGPU> objectData;
		std::vector<MaterialGPU> materialData;
		std::vector<ViewGPU> viewData;
		objectData.reserve(MaxObjectCapacity);
		materialData.reserve(MaxMaterialCapacity);
		viewData.reserve(renderViews.size());

		std::unordered_map<MaterialID, uint32_t> materialIndexMap;

		registry.view<components::TransformComponent, components::ModelComponent>().each(
			[&result, &assetManager, &objectData, &materialData, &materialIndexMap](auto entity,
				const components::TransformComponent& transformComp,
				const components::ModelComponent& modelComp)
			{
				const auto* model = assetManager.GetModel(modelComp.m_ModelId);
				if (!model)
				{
					GGLAB_LOG_GRAPHICS_WARN("Entity has no model.");
					return;
				}

				Matrix world =
					Matrix::CreateScale(transformComp.m_Scale) *
					Matrix::CreateFromQuaternion(transformComp.m_Rotation) *
					Matrix::CreateTranslation(transformComp.m_Position);

				Matrix normalMat = world;
				normalMat.Translation(Vector3::Zero);
				normalMat = normalMat.Invert().Transpose();

				for (const ModelMesh& modelMesh : model->m_MeshInstance)
				{
					const Mesh* mesh = assetManager.GetMesh(modelMesh.m_MeshId);
					if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded)
					{
						continue;
					}

					uint32_t materialIndex = 0;
					auto iter = materialIndexMap.find(modelMesh.m_MaterialId);
					if (iter == materialIndexMap.end())
					{
						const Material* material = assetManager.GetMaterial(modelMesh.m_MaterialId);
						if (!material)
						{
							GGLAB_LOG_GRAPHICS_WARN("Mesh has invalid MaterialID.");
							continue;
						}

						materialIndex = static_cast<uint32_t>(materialData.size());
						materialIndexMap.emplace(modelMesh.m_MaterialId, materialIndex);

						MaterialGPU materialGpu{};
						materialGpu.BaseColorFactor = material->m_BaseColor;
						materialGpu.MetallicFactor = material->m_MetallicFactor;
						materialGpu.RoughnessFactor = material->m_RoughnessFactor;
						materialGpu.NormalScale = material->m_NormalScale;
						materialGpu.OcclusionStrength = material->m_OcclusionStrength;
						materialGpu.EmissiveColorFactor = material->m_EmissiveColor;

						materialGpu.BaseColorBinding = assetManager.ResolveTextureBinding(material->m_BaseColorBinding,
							ReservedTextureIDIndex::BaseColorWhite,
							SamplerPreset::LinearWrap);

						materialGpu.EmissiveBinding = assetManager.ResolveTextureBinding(material->m_EmissiveBinding,
							ReservedTextureIDIndex::EmissiveBlack,
							SamplerPreset::LinearWrap);

						materialGpu.MetallicRoughnessBinding = assetManager.ResolveTextureBinding(material->m_MetallicRoughnessBinding,
							ReservedTextureIDIndex::DefaultMetallicRoughness,
							SamplerPreset::LinearWrap);

						materialGpu.NormalBinding = assetManager.ResolveTextureBinding(material->m_NormalBinding,
							ReservedTextureIDIndex::NormalFlat,
							SamplerPreset::LinearWrap);

						materialGpu.OcclusionBinding = assetManager.ResolveTextureBinding(material->m_OcclusionBinding,
							ReservedTextureIDIndex::OcclusionWhite,
							SamplerPreset::LinearWrap);

						materialGpu.AlphaMode = static_cast<int32_t>(material->m_AlphaMode);
						materialGpu.AlphaCutoff = material->m_AlphaCutoff;
						materialGpu.Flags = static_cast<uint32_t>(material->m_Flags);

						materialData.push_back(materialGpu);
					}
					else
					{
						materialIndex = iter->second;
					}

					uint32_t objectOffset = static_cast<uint32_t>(objectData.size());

					ObjectGPU objectGpu{};
					objectGpu.ModelMat = world;
					objectGpu.NormalMat = normalMat;
					objectGpu.MaterialIndex = materialIndex;
					objectData.push_back(objectGpu);

					Vector3 worldCenter = transformComp.m_Position;
					if (mesh->m_HasBounds)
					{
						const Vector3 localCenter = mesh->m_BoundingBox.Center;
						worldCenter = Vector3::Transform(localCenter, world);
					}

					RenderInstance renderInstance{};
					renderInstance.m_MeshId = modelMesh.m_MeshId;
					renderInstance.m_MaterialId = modelMesh.m_MaterialId;
					renderInstance.m_ObjectOffset = objectOffset;
					renderInstance.m_WorldCenterPos = worldCenter;
					result.m_RenderScene.m_RenderInstances.push_back(renderInstance);
				}
			});

		// View data
		for (const RenderView& renderView : renderViews)
		{
			ViewGPU viewGpu{};
			viewGpu.ViewMat = renderView.m_View;
			viewGpu.ProjMat = renderView.m_Proj;
			viewGpu.InvViewMat = renderView.m_InvView;
			viewGpu.InvProjMat = renderView.m_InvProj;
			viewGpu.CameraPos = utils::ToVector4(renderView.m_CameraPosition, 1.0f);
			viewGpu.Near = renderView.m_Near;
			viewGpu.Far = renderView.m_Far;
			viewGpu.FovRadians = renderView.m_FovRadians;
			viewGpu.Aspect = renderView.m_Aspect;
			viewGpu.Exposure = renderView.m_Exposure;
			viewGpu.Width = renderView.m_Width;
			viewGpu.Height = renderView.m_Height;
			viewData.push_back(viewGpu);
		}

		// Update View Structured Buffer
		DX12FencePoint uploadFencePoint{};
		TransferBatch::StageBufferWriteResult<ObjectGPU> objectsBufferResult{};
		TransferBatch::StageBufferWriteResult<MaterialGPU> materialsBufferResult{};
		TransferBatch::StageBufferWriteResult<ViewGPU> viewsBufferResult{};

		// Upload structured buffer to GPU
		if (!objectData.empty() ||
			!materialData.empty() ||
			!viewData.empty())
		{
			auto batch = transferManager.BeginBatch();

			if (!objectData.empty())
			{
				std::span<const ObjectGPU> objectSpan(objectData.data(), objectData.size());
				objectsBufferResult = batch.StageStructuredBufferWrite<ObjectGPU>(info.m_ObjectsSB, objectSpan);
			}

			if (!materialData.empty())
			{
				std::span<const MaterialGPU> materialSpan(materialData.data(), materialData.size());
				materialsBufferResult = batch.StageStructuredBufferWrite<MaterialGPU>(info.m_MaterialsSB, materialSpan);
			}

			if (!viewData.empty())
			{
				std::span<const ViewGPU> viewSpan(viewData.data(), viewData.size());
				viewsBufferResult = batch.StageStructuredBufferWrite<ViewGPU>(info.m_ViewsSB, viewSpan);
			}

			uploadFencePoint = batch.Submit(false);
		}

		result.m_UploadFencePoint = uploadFencePoint;

		result.m_RenderScene.m_ObjectBaseIndex = objectsBufferResult.m_FirstElementIndex;
		result.m_RenderScene.m_ObjectCount = objectsBufferResult.m_ElementCount;

		result.m_RenderScene.m_MaterialBaseIndex = materialsBufferResult.m_FirstElementIndex;
		result.m_RenderScene.m_MaterialCount = materialsBufferResult.m_ElementCount;

		result.m_RenderScene.m_ViewBaseIndex = viewsBufferResult.m_FirstElementIndex;
		result.m_RenderScene.m_ViewCount = viewsBufferResult.m_ElementCount;

		// MainLight data
		{
			LightGPU lightGpu{};
			auto lightView = registry.view<components::TransformComponent, components::LightComponent>();

			bool found = false;
			for (auto [entity, transComp, lightComp] : lightView.each())
			{
				lightGpu.Position = utils::ToVector4(transComp.m_Position, 1.0f);

				Matrix rotation = Matrix::CreateFromQuaternion(transComp.m_Rotation);
				Vector3 forward = Vector3::Transform(-Vector3::UnitZ, rotation);
				forward.Normalize();
				lightGpu.Direction = utils::ToVector4(forward, 1.0f);

				lightGpu.Color = lightComp.m_Color;
				lightGpu.Intensity = lightComp.m_Intensity;
				lightGpu.Range = lightComp.m_Range;
				lightGpu.SpotAngle = lightComp.m_SpotAngle;
				lightGpu.LightType = static_cast<uint32_t>(lightComp.m_Type);

				found = true;
				break;
			}

			// Dummy main light
			if (!found)
			{
				lightGpu.Direction = -Vector4::UnitY;
				lightGpu.Color = color::White;
				lightGpu.Intensity = 1.0f;
				lightGpu.LightType = static_cast<uint32_t>(LightType::Directional);
			}

			result.m_RenderScene.m_MainLight = lightGpu;
		}

		return result;
	}
}