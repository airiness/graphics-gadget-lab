#include "Core/Precompiled.h"
#include "Graphics/RenderScene.h"
#include "Graphics/TransferManager.h"
#include "Graphics/AssetManager.h"
#include "Graphics/RenderResourceRegistry.h"
#include "Core/World.h"
#include "Core/Components.h"
#include "Core/Utility/MathUtils.h"
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

		// Reclaim staging uploads. GPU-local structured-buffer allocations are
		// reclaimed by Renderer from the graphics fence timeline.
		transferManager.Reclaim();

		// Assembly RenderInstaces
		result.m_RenderScene.m_RenderInstances.clear();

		GGLAB_ASSERT(info.m_CurrentBackBufferIndex < info.m_ObjectsSB.GetBufferCount());
		GGLAB_ASSERT(info.m_CurrentBackBufferIndex < info.m_MaterialsSB.GetBufferCount());
		const RHIBufferHandle objectBufferIdentity = info.m_ObjectsSB.GetBufferHandle();
		const RHIBufferHandle materialBufferIdentity = info.m_MaterialsSB.GetBufferHandle();
		if (!m_ObjectTable ||
			!m_MaterialTable ||
			m_ObjectBufferIdentity != objectBufferIdentity ||
			m_MaterialBufferIdentity != materialBufferIdentity)
		{
			GGLAB_ASSERT(info.m_ObjectsSB.GetBufferCount() == info.m_MaterialsSB.GetBufferCount());
			m_ObjectTable = std::make_unique<ObjectTable>(
				info.m_ObjectsSB.GetElementCapacity(),
				info.m_ObjectsSB.GetBufferCount());
			m_MaterialTable = std::make_unique<MaterialTable>(
				info.m_MaterialsSB.GetElementCapacity(),
				info.m_MaterialsSB.GetBufferCount());
			m_ObjectBufferIdentity = objectBufferIdentity;
			m_MaterialBufferIdentity = materialBufferIdentity;
		}

		m_ObjectTable->BeginUpdate();
		m_MaterialTable->BeginUpdate();

		std::vector<ViewGPU> viewData;
		viewData.reserve(renderViews.size());

		std::unordered_map<MaterialID, uint32_t> materialIndexMap;

		registry.view<components::TransformComponent, components::ModelComponent>().each(
			[&result, &assetManager, this, &materialIndexMap](auto entity,
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

				for (uint32_t modelMeshIndex = 0;
					modelMeshIndex < model->m_MeshInstance.size();
					++modelMeshIndex)
				{
					const ModelMesh& modelMesh = model->m_MeshInstance[modelMeshIndex];
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

						materialIndex = m_MaterialTable->Upsert(modelMesh.m_MaterialId, materialGpu);
						if (materialIndex == MaterialTable::InvalidSlot)
						{
							continue;
						}
						materialIndexMap.emplace(modelMesh.m_MaterialId, materialIndex);
					}
					else
					{
						materialIndex = iter->second;
					}

					ObjectGPU objectGpu{};
					objectGpu.ModelMat = world;
					objectGpu.NormalMat = normalMat;
					objectGpu.MaterialIndex = materialIndex;

					const uint64_t objectKey =
						(static_cast<uint64_t>(entt::to_integral(entity)) << 32) |
						modelMeshIndex;
					const uint32_t objectOffset = m_ObjectTable->Upsert(objectKey, objectGpu);
					if (objectOffset == ObjectTable::InvalidSlot)
					{
						continue;
					}

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

		m_ObjectTable->EndUpdate();
		m_MaterialTable->EndUpdate();

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
		DynamicStructuredBufferAllocator<ViewGPU>::Allocation viewsBufferResult{};
		if (!viewData.empty())
		{
			viewsBufferResult = info.m_ViewsSB.Upload(std::span<const ViewGPU>(viewData));
		}

		const auto objectDirtyRanges =
			m_ObjectTable->BuildDirtyRanges(info.m_CurrentBackBufferIndex);
		const auto materialDirtyRanges =
			m_MaterialTable->BuildDirtyRanges(info.m_CurrentBackBufferIndex);
		bool objectsUploadSucceeded = true;
		bool materialsUploadSucceeded = true;

		// Only upload changed contiguous ranges into the physical buffer version
		// associated with the current backbuffer.
		if (!objectDirtyRanges.empty() || !materialDirtyRanges.empty())
		{
			auto batch = transferManager.BeginBatch();

			const RHIBufferHandle objectBuffer =
				info.m_ObjectsSB.GetBufferHandle(info.m_CurrentBackBufferIndex);
			for (const auto& range : objectDirtyRanges)
			{
				const std::span<const ObjectGPU> data = m_ObjectTable->GetData(range);
				objectsUploadSucceeded &= batch.StageBufferWrite(
					objectBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(ObjectGPU),
					data.data(),
					data.size_bytes());
			}

			const RHIBufferHandle materialBuffer =
				info.m_MaterialsSB.GetBufferHandle(info.m_CurrentBackBufferIndex);
			for (const auto& range : materialDirtyRanges)
			{
				const std::span<const MaterialGPU> data = m_MaterialTable->GetData(range);
				materialsUploadSucceeded &= batch.StageBufferWrite(
					materialBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(MaterialGPU),
					data.data(),
					data.size_bytes());
			}

			uploadFencePoint = batch.Submit(false);
			if (objectsUploadSucceeded)
			{
				m_ObjectTable->Commit(info.m_CurrentBackBufferIndex, objectDirtyRanges);
			}
			if (materialsUploadSucceeded)
			{
				m_MaterialTable->Commit(info.m_CurrentBackBufferIndex, materialDirtyRanges);
			}
		}

		result.m_UploadFencePoint = uploadFencePoint;
		result.m_GpuAllocations.m_Views = viewsBufferResult;

		const bool viewsUploadSucceeded =
			viewData.empty() || viewsBufferResult.IsValid();

		if (objectsUploadSucceeded &&
			materialsUploadSucceeded &&
			viewsUploadSucceeded)
		{
			result.m_Status = RenderSceneBuildStatus::Ready;

			result.m_RenderScene.m_ObjectBaseIndex = 0;
			result.m_RenderScene.m_ObjectCount = m_ObjectTable->GetLiveCount();
			result.m_RenderScene.m_MaterialBaseIndex = 0;
			result.m_RenderScene.m_MaterialCount = m_MaterialTable->GetLiveCount();
			result.m_RenderScene.m_ViewBaseIndex = viewsBufferResult.m_FirstElementIndex;
			result.m_RenderScene.m_ViewCount = viewsBufferResult.m_ElementCount;
		}
		else
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"RenderSceneBuilder: GPU scene upload failed "
				"(objects={}, materials={}, views={}). Rendering is disabled for this frame.",
				objectsUploadSucceeded,
				materialsUploadSucceeded,
				viewsUploadSucceeded);
		}

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

		SceneGPU sceneCB{};
		sceneCB.ObjectBaseIndex = result.m_RenderScene.m_ObjectBaseIndex;
		sceneCB.ObjectCount = result.m_RenderScene.m_ObjectCount;
		sceneCB.MaterialBaseIndex = result.m_RenderScene.m_MaterialBaseIndex;
		sceneCB.MaterialCount = result.m_RenderScene.m_MaterialCount;
		sceneCB.ViewBaseIndex = result.m_RenderScene.m_ViewBaseIndex;
		sceneCB.ViewCount = result.m_RenderScene.m_ViewCount;

		info.m_RenderResourceRegistry.EnsureIblResources();
		info.m_RenderResourceRegistry.FillIBLBindlessGPU(sceneCB.IBLResource);

		sceneCB.MainLight = result.m_RenderScene.m_MainLight;
		result.m_GpuAllocations.m_SceneConstants = info.m_SceneCB.Upload(sceneCB);
		if (!result.m_GpuAllocations.m_SceneConstants.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("RenderSceneBuilder: Scene constant allocation failed.");
			result.m_Status = RenderSceneBuildStatus::GpuUploadFailed;
		}
		else
		{
			result.m_RenderScene.m_SceneConstantBufferOffset =
				result.m_GpuAllocations.m_SceneConstants.m_OffsetInBytes;
		}

		return result;
	}
}
