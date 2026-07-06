#include "Core/Precompiled.h"
#include "Graphics/RenderScene.h"
#include "Graphics/TransferManager.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Core/World.h"
#include "Scene/Components.h"
#include "Core/Math/MathFunctions.h"
#include "Graphics/RenderView.h"

namespace gglab
{
	namespace
	{
		constexpr uint64_t DefaultLightKey = std::numeric_limits<uint64_t>::max();

		MaterialGPU BuildMaterialGpu(
			const MaterialProperties& material,
			const AssetManager& assetManager) noexcept
		{
			MaterialGPU gpu{};
			gpu.BaseColorFactor = material.m_BaseColor;
			gpu.MetallicFactor = material.m_MetallicFactor;
			gpu.RoughnessFactor = material.m_RoughnessFactor;
			gpu.NormalScale = material.m_NormalScale;
			gpu.OcclusionStrength = material.m_OcclusionStrength;
			gpu.EmissiveColorFactor = material.m_EmissiveColor;

			gpu.BaseColorBinding = assetManager.ResolveTextureBinding(
				material.m_BaseColorBinding,
				ReservedTextureIDIndex::BaseColorWhite,
				SamplerPreset::LinearWrap);
			gpu.EmissiveBinding = assetManager.ResolveTextureBinding(
				material.m_EmissiveBinding,
				ReservedTextureIDIndex::EmissiveBlack,
				SamplerPreset::LinearWrap);
			gpu.MetallicRoughnessBinding = assetManager.ResolveTextureBinding(
				material.m_MetallicRoughnessBinding,
				ReservedTextureIDIndex::DefaultMetallicRoughness,
				SamplerPreset::LinearWrap);
			gpu.NormalBinding = assetManager.ResolveTextureBinding(
				material.m_NormalBinding,
				ReservedTextureIDIndex::NormalFlat,
				SamplerPreset::LinearWrap);
			gpu.OcclusionBinding = assetManager.ResolveTextureBinding(
				material.m_OcclusionBinding,
				ReservedTextureIDIndex::OcclusionWhite,
				SamplerPreset::LinearWrap);

			gpu.AlphaMode = static_cast<int32_t>(material.m_AlphaMode);
			gpu.AlphaCutoff = material.m_AlphaCutoff;
			gpu.Flags = static_cast<uint32_t>(material.m_Flags);
			gpu.DebugView = static_cast<uint32_t>(material.m_DebugView);
			return gpu;
		}

		struct MaterialUploadRecord
		{
			MaterialGPU m_Gpu{};
			uint32_t m_Index = 0;
			MaterialFlags m_Flags = MaterialFlags::None;
			AlphaMode m_AlphaMode = AlphaMode::Opaque;
		};
	}

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

		using ObjectTable = PersistentStructuredBufferTable<uint64_t, ObjectGPU>;
		using MaterialTable = PersistentStructuredBufferTable<RenderMaterialKey, MaterialGPU>;
		using LightTable = PersistentStructuredBufferTable<uint64_t, LightGPU>;
		GGLAB_ASSERT(info.m_CurrentBackBufferIndex < info.m_ObjectsSB.GetBufferCount());
		GGLAB_ASSERT(info.m_CurrentBackBufferIndex < info.m_MaterialsSB.GetBufferCount());
		GGLAB_ASSERT(info.m_CurrentBackBufferIndex < info.m_LightsSB.GetBufferCount());
		info.m_ObjectTable.BeginUpdate();
		info.m_MaterialTable.BeginUpdate();
		info.m_LightTable.BeginUpdate();

		std::vector<ViewGPU> viewData;
		viewData.reserve(renderViews.size());

		std::unordered_map<RenderMaterialKey, MaterialUploadRecord> materialRecords;

		registry.view<components::TransformComponent, components::ModelComponent>().each(
			[&result, &assetManager, &info, &registry, &materialRecords](auto entity,
				const components::TransformComponent& transformComp,
				const components::ModelComponent& modelComp)
			{
				const auto* model = assetManager.GetModel(modelComp.m_ModelId);
				if (!model)
				{
					GGLAB_LOG_GRAPHICS_WARN("Entity has no model.");
					return;
				}

				const Matrix entityWorld =
					Matrix::CreateScale(transformComp.m_Scale) *
					Matrix::CreateFromQuaternion(transformComp.m_Rotation) *
					Matrix::CreateTranslation(transformComp.m_Position);

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

					const Matrix world = modelMesh.m_LocalTransform * entityWorld;
					Matrix normalMat = world;
					normalMat.Translation(Vector3::Zero);
					normalMat = normalMat.Invert().Transpose();

					const MaterialProperties* material = nullptr;
					RenderMaterialKey materialKey{};
					const auto* materialInstance =
						registry.try_get<components::MaterialInstanceComponent>(entity);
					if (materialInstance && materialInstance->m_Key.IsValid())
					{
						material = &materialInstance->m_Properties;
						materialKey = RenderMaterialKey::FromRuntime(materialInstance->m_Key);
					}
					else
					{
						material = assetManager.GetMaterial(modelMesh.m_MaterialId);
						materialKey = RenderMaterialKey::FromAsset(modelMesh.m_MaterialId);
					}

					if (!material)
					{
						GGLAB_LOG_GRAPHICS_WARN("Mesh has no valid material.");
						continue;
					}

					const MaterialGPU materialGpu = BuildMaterialGpu(*material, assetManager);
					auto iter = materialRecords.find(materialKey);
					if (iter == materialRecords.end())
					{
						const uint32_t materialIndex =
							info.m_MaterialTable.Upsert(materialKey, materialGpu);
						if (materialIndex == MaterialTable::InvalidSlot)
						{
							continue;
						}
						iter = materialRecords.emplace(materialKey, MaterialUploadRecord{
							.m_Gpu = materialGpu,
							.m_Index = materialIndex,
							.m_Flags = material->m_Flags,
							.m_AlphaMode = material->m_AlphaMode,
						}).first;
					}
					else if (std::memcmp(&iter->second.m_Gpu, &materialGpu, sizeof(MaterialGPU)) != 0 ||
						iter->second.m_Flags != material->m_Flags ||
						iter->second.m_AlphaMode != material->m_AlphaMode)
					{
						GGLAB_LOG_GRAPHICS_WARN(
							"Render material key collision for domain={} value=0x{:016X}.",
							static_cast<uint32_t>(materialKey.m_Domain),
							materialKey.m_Value);
					}

					ObjectGPU objectGpu{};
					objectGpu.ModelMat = world;
					objectGpu.NormalMat = normalMat;
					objectGpu.MaterialIndex = iter->second.m_Index;

					const uint64_t objectKey =
						(static_cast<uint64_t>(entt::to_integral(entity)) << 32) |
						modelMeshIndex;
					const uint32_t objectOffset = info.m_ObjectTable.Upsert(objectKey, objectGpu);
					if (objectOffset == ObjectTable::InvalidSlot)
					{
						continue;
					}

					Vector3 worldCenter = transformComp.m_Position;
					if (mesh->m_HasBounds)
					{
						worldCenter = Vector3::Transform(mesh->m_BoundingBox.m_Center, world);
					}

					RenderInstance renderInstance{};
					renderInstance.m_MeshId = modelMesh.m_MeshId;
					renderInstance.m_MaterialKey = materialKey;
					renderInstance.m_MaterialFlags = iter->second.m_Flags;
					renderInstance.m_AlphaMode = iter->second.m_AlphaMode;
					renderInstance.m_ObjectOffset = objectOffset;
					renderInstance.m_WorldCenterPos = worldCenter;
					result.m_RenderScene.m_RenderInstances.push_back(renderInstance);
				}
			});

		uint32_t directionalShadowLightSlot = LightTable::InvalidSlot;

		// Light data
		{
			bool foundLight = false;
			auto lightView = registry.view<components::TransformComponent, components::LightComponent>();
			for (auto&& [entity, transComp, lightComp] : lightView.each())
			{
				LightGPU lightGpu{};
				lightGpu.Position = math::ToVector4(transComp.m_Position, 1.0f);

				Matrix rotation = Matrix::CreateFromQuaternion(transComp.m_Rotation);
				Vector3 forward = Vector3::Transform(-Vector3::UnitZ, rotation);
				forward.Normalize();
				lightGpu.Direction = math::ToVector4(forward, 0.0f);

				lightGpu.Color = lightComp.m_Color;
				lightGpu.Intensity = lightComp.m_Intensity;
				lightGpu.Range = lightComp.m_Range;
				lightGpu.SpotAngle = lightComp.m_SpotAngle;
				lightGpu.LightType = static_cast<uint32_t>(lightComp.m_Type);

				const uint64_t lightKey = static_cast<uint64_t>(entt::to_integral(entity));
				const uint32_t lightSlot = info.m_LightTable.Upsert(lightKey, lightGpu);
				foundLight = foundLight || lightSlot != LightTable::InvalidSlot;
				if (lightSlot != LightTable::InvalidSlot &&
					info.m_DirectionalShadowLightKey == lightKey &&
					lightComp.m_Type == LightType::Directional)
				{
					directionalShadowLightSlot = lightSlot;
				}
			}

			// Preserve the previous fallback lighting for scenes with no explicit light.
			if (!foundLight)
			{
				LightGPU lightGpu{};
				lightGpu.Direction = -Vector4::UnitY;
				lightGpu.Color = color::White;
				lightGpu.Intensity = 1.0f;
				lightGpu.Range = 1000.0f;
				lightGpu.SpotAngle = 60.0f;
				lightGpu.LightType = static_cast<uint32_t>(LightType::Directional);
				const uint32_t lightSlot = info.m_LightTable.Upsert(DefaultLightKey, lightGpu);
				GGLAB_ASSERT(lightSlot != LightTable::InvalidSlot);
			}
		}

		info.m_ObjectTable.EndUpdate();
		info.m_MaterialTable.EndUpdate();
		info.m_LightTable.EndUpdate();

		// View data
		for (const RenderView& renderView : renderViews)
		{
			ViewGPU viewGpu{};
			viewGpu.ViewMat = renderView.m_View;
			viewGpu.ProjMat = renderView.m_Proj;
			viewGpu.InvViewMat = renderView.m_InvView;
			viewGpu.InvProjMat = renderView.m_InvProj;
			viewGpu.CameraPos = math::ToVector4(renderView.m_CameraPosition, 1.0f);
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
		RHIFencePoint uploadFencePoint{};
		DynamicStructuredBufferAllocator<ViewGPU>::Allocation viewsBufferResult{};
		if (!viewData.empty())
		{
			viewsBufferResult = info.m_ViewsSB.Upload(std::span<const ViewGPU>(viewData));
		}

		const auto objectDirtyRanges =
			info.m_ObjectTable.BuildDirtyRanges(info.m_CurrentBackBufferIndex);
		const auto materialDirtyRanges =
			info.m_MaterialTable.BuildDirtyRanges(info.m_CurrentBackBufferIndex);
		const auto lightDirtyRanges =
			info.m_LightTable.BuildDirtyRangesIncludingFreeSlots(info.m_CurrentBackBufferIndex);
		bool objectsUploadSucceeded = true;
		bool materialsUploadSucceeded = true;
		bool lightsUploadSucceeded = true;

		// Only upload changed contiguous ranges into the physical buffer version
		// associated with the current backbuffer.
		if (!objectDirtyRanges.empty() || !materialDirtyRanges.empty() || !lightDirtyRanges.empty())
		{
			auto batch = transferManager.BeginBatch();

			const RHIBufferHandle objectBuffer =
				info.m_ObjectsSB.GetBufferHandle(info.m_CurrentBackBufferIndex);
			for (const auto& range : objectDirtyRanges)
			{
				const std::span<const ObjectGPU> data = info.m_ObjectTable.GetData(range);
				objectsUploadSucceeded &= batch.UploadBuffer(
					objectBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(ObjectGPU),
					data.data(),
					data.size_bytes());
			}

			const RHIBufferHandle materialBuffer =
				info.m_MaterialsSB.GetBufferHandle(info.m_CurrentBackBufferIndex);
			for (const auto& range : materialDirtyRanges)
			{
				const std::span<const MaterialGPU> data = info.m_MaterialTable.GetData(range);
				materialsUploadSucceeded &= batch.UploadBuffer(
					materialBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(MaterialGPU),
					data.data(),
					data.size_bytes());
			}

			const RHIBufferHandle lightBuffer =
				info.m_LightsSB.GetBufferHandle(info.m_CurrentBackBufferIndex);
			for (const auto& range : lightDirtyRanges)
			{
				const std::span<const LightGPU> data = info.m_LightTable.GetData(range);
				lightsUploadSucceeded &= batch.UploadBuffer(
					lightBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(LightGPU),
					data.data(),
					data.size_bytes());
			}

			uploadFencePoint = batch.Submit(false);
			if (objectsUploadSucceeded)
			{
				info.m_ObjectTable.Commit(info.m_CurrentBackBufferIndex, objectDirtyRanges);
			}
			if (materialsUploadSucceeded)
			{
				info.m_MaterialTable.Commit(info.m_CurrentBackBufferIndex, materialDirtyRanges);
			}
			if (lightsUploadSucceeded)
			{
				info.m_LightTable.Commit(info.m_CurrentBackBufferIndex, lightDirtyRanges);
			}
		}
		else
		{
			info.m_ObjectTable.Commit(info.m_CurrentBackBufferIndex, {});
			info.m_MaterialTable.Commit(info.m_CurrentBackBufferIndex, {});
			info.m_LightTable.Commit(info.m_CurrentBackBufferIndex, {});
		}

		result.m_UploadFencePoint = uploadFencePoint;
		result.m_GpuAllocations.m_Views = viewsBufferResult;

		const bool viewsUploadSucceeded =
			viewData.empty() || viewsBufferResult.IsValid();

		if (objectsUploadSucceeded &&
			materialsUploadSucceeded &&
			lightsUploadSucceeded &&
			viewsUploadSucceeded)
		{
			result.m_Status = RenderSceneBuildStatus::Ready;

			result.m_RenderScene.m_ObjectBaseIndex = 0;
			result.m_RenderScene.m_ObjectCount = info.m_ObjectTable.GetLiveCount();
			result.m_RenderScene.m_MaterialBaseIndex = 0;
			result.m_RenderScene.m_MaterialCount = info.m_MaterialTable.GetLiveCount();
			result.m_RenderScene.m_ViewBaseIndex = viewsBufferResult.m_FirstElementIndex;
			result.m_RenderScene.m_ViewCount = viewsBufferResult.m_ElementCount;
			result.m_RenderScene.m_LightBaseIndex = 0;
			result.m_RenderScene.m_LightCount = info.m_LightTable.GetCapacity();
			if (directionalShadowLightSlot != LightTable::InvalidSlot)
			{
				result.m_RenderScene.m_DirectionalShadowLightIndex =
					result.m_RenderScene.m_LightBaseIndex + directionalShadowLightSlot;
			}
		}
		else
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"RenderSceneBuilder: GPU scene upload failed "
				"(objects={}, materials={}, lights={}, views={}). Rendering is disabled for this frame.",
				objectsUploadSucceeded,
				materialsUploadSucceeded,
				lightsUploadSucceeded,
				viewsUploadSucceeded);
		}

		SceneGPU sceneCB{};
		sceneCB.ObjectBaseIndex = result.m_RenderScene.m_ObjectBaseIndex;
		sceneCB.ObjectCount = result.m_RenderScene.m_ObjectCount;
		sceneCB.MaterialBaseIndex = result.m_RenderScene.m_MaterialBaseIndex;
		sceneCB.MaterialCount = result.m_RenderScene.m_MaterialCount;
		sceneCB.ViewBaseIndex = result.m_RenderScene.m_ViewBaseIndex;
		sceneCB.ViewCount = result.m_RenderScene.m_ViewCount;
		sceneCB.LightBaseIndex = result.m_RenderScene.m_LightBaseIndex;
		sceneCB.LightCount = result.m_RenderScene.m_LightCount;
		sceneCB.DirectionalShadowLightIndex = result.m_RenderScene.m_DirectionalShadowLightIndex;

		info.m_RenderResourceRegistry.EnsureIblResources();
		info.m_RenderResourceRegistry.FillIBLBindlessGPU(sceneCB.IBLResource);

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
