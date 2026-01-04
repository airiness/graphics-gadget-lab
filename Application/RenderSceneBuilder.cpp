#include "Precompiled.h"
#include "RenderSceneBuilder.h"
#include "TransferManager.h"
#include "AssetManager.h"
#include "World.h"
#include "Components.h"

namespace gglab
{
	RenderSceneBuilder::BuildResult RenderSceneBuilder::Build(const BuildInfo& info) noexcept
	{
		BuildResult result{};

		auto& registry = info.m_World.GetRegistry();
		auto& transferManager = info.m_TransferManager;
		auto& assetManager = info.m_AssetManager;

		// Reclaim ring buffer
		const DX12FencePoint completedFence = transferManager.Reclaim();
		if (completedFence.IsValid())
		{
			info.m_ObjectsSB.ReclaimCompleted(completedFence);
			info.m_MaterialsSB.ReclaimCompleted(completedFence);
		}

		// Assembly object/material data
		result.m_RenderScene.m_DrawItems.clear();

		// Update Object and Material Structured Buffer
		std::vector<ObjectGPU> objectData;
		std::vector<MaterialGPU> materialData;
		objectData.reserve(MaxObjectCapacity);
		materialData.reserve(MaxMaterialCapacity);

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

						materialGpu.BaseColorTexIndex = assetManager.GetTextureDescriptorIndex(material->m_BaseColorTex);
						materialGpu.MetallicRoughnessTexIndex = assetManager.GetTextureDescriptorIndex(material->m_MetallicRoughnessTex);
						materialGpu.NormalTexIndex = assetManager.GetTextureDescriptorIndex(material->m_NormalTex);
						materialGpu.OcclusionTexIndex = assetManager.GetTextureDescriptorIndex(material->m_OcclusionTex);
						materialGpu.EmissiveTexIndex = assetManager.GetTextureDescriptorIndex(material->m_EmissiveTex);

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

					DrawItem drawItem{};
					drawItem.m_MeshId = modelMesh.m_MeshId;
					drawItem.m_MaterialId = modelMesh.m_MaterialId;
					drawItem.m_ObjectOffset = objectOffset;
					result.m_RenderScene.m_DrawItems.push_back(drawItem);

				}
			});

		DX12FencePoint uploadFencePoint{};
		TransferBatch::StageBufferWriteResult<ObjectGPU> objectsBufferResult{};
		TransferBatch::StageBufferWriteResult<MaterialGPU> materialsBufferResult{};

		// Upload structured buffer to GPU
		if (!objectData.empty() || !materialData.empty())
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

			uploadFencePoint = batch.Submit(false);
		}

		result.m_UploadFencePoint = uploadFencePoint;

		result.m_RenderScene.m_ObjectBaseIndex = objectsBufferResult.m_FirstElementIndex;
		result.m_RenderScene.m_ObjectCount = objectsBufferResult.m_ElementCount;

		result.m_RenderScene.m_MaterialBaseIndex = materialsBufferResult.m_FirstElementIndex;
		result.m_RenderScene.m_MaterialCount = materialsBufferResult.m_ElementCount;

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