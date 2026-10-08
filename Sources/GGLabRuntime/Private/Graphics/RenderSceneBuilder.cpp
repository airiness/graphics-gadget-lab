#include "Graphics/RenderSceneBuilder.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/Math/Transform.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/RenderMaterialFrameCache.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabRuntime/Scene/Components.h"

#include <limits>
#include <span>
#include <vector>

namespace gglab
{
	LightGPU RenderSceneBuilder::BuildLightData(uint64_t entityKey,
		const components::TransformComponent& transform, const components::LightComponent& light,
		const RenderDirectionalLight& mainLight) noexcept
	{
		LightGPU gpu{};
		gpu.Position = math::ToVector4(transform.m_Position, 1.0f);
		Vector3 direction = math::TransformDirection(Vector3::Forward, math::CreateFromQuaternion(transform.m_Rotation));
		direction.Normalize();
		gpu.Direction = math::ToVector4(direction, 0.0f);
		gpu.Color = light.m_Color;
		gpu.Intensity = light.m_Intensity;
		gpu.Range = light.m_Range;
		gpu.SpotAngle = light.m_SpotAngle;
		gpu.LightType = static_cast<uint32_t>(light.m_Type);
		if (mainLight.m_EntityKey == entityKey && light.m_Type == LightType::Directional)
		{
			gpu.Direction = math::ToVector4(mainLight.m_Direction, 0.0f);
			if (const auto& sun = mainLight.m_WorldSun)
			{
				// The BRDF already supplies Lambert's 1/pi and the cosine term.
				gpu.Color = Color(sun->m_Chromaticity.m_X, sun->m_Chromaticity.m_Y, sun->m_Chromaticity.m_Z, 1.0f);
				gpu.Intensity = sun->m_PerpendicularIlluminanceLux;
			}
		}
		return gpu;
	}

	namespace
	{
		constexpr uint64_t DefaultLightKey = std::numeric_limits<uint64_t>::max();

	}

	RenderSceneBuilder::ViewUploadData RenderSceneBuilder::BuildViewData(
		std::span<const RenderView> cameraViews, const DirectionalShadowFramePlan& cascades,
		const RenderView* postTemporalView) noexcept
	{
		ViewUploadData result{};
		auto& viewData = result.m_Views;
		viewData.reserve(cameraViews.size() + cascades.m_Cascades.size() + 1);
		const auto appendView = [&viewData](const RenderView& renderView)
		{
			ViewGPU viewGpu{};
			viewGpu.ViewMat = renderView.m_View;
			viewGpu.ProjMat = renderView.m_RasterProj;
			viewGpu.InvViewMat = renderView.m_InvView;
			viewGpu.InvProjMat = renderView.m_InvRasterProj;
			viewGpu.PreviousViewMat = renderView.m_PreviousView;
			viewGpu.PreviousRasterViewProj = renderView.m_PreviousRasterViewProj;
			viewGpu.CameraPos = math::ToVector4(renderView.m_CameraPosition, 1.0f);
			viewGpu.PreviousDepthReconstructionParams =
				renderView.m_PreviousDepthReconstructionParams;
			viewGpu.Near = renderView.m_Near;
			viewGpu.Far = renderView.m_Far;
			viewGpu.FovRadians = renderView.m_FovRadians;
			viewGpu.Aspect = renderView.m_Aspect;
			viewGpu.CurrentJitterUV = renderView.m_JitterUV;
			viewGpu.PreviousJitterUV = renderView.m_PreviousJitterUV;
			viewGpu.ExposureMultiplier = renderView.m_ExposureMultiplier;
			viewGpu.ScenePreExposure = renderView.m_ScenePreExposure;
			viewGpu.PreviousScenePreExposure = renderView.m_PreviousScenePreExposure;
			viewGpu.TemporalFrameIndex = renderView.m_TemporalFrameIndex;
			viewGpu.TextureLodBias = renderView.m_TextureLodBias;
			viewGpu.Width = renderView.m_Width;
			viewGpu.Height = renderView.m_Height;
			viewGpu.DepthConvention = static_cast<uint32_t>(renderView.m_DepthConvention);
			viewGpu.PreviousDepthConvention =
				static_cast<uint32_t>(renderView.m_PreviousDepthConvention);
			viewData.push_back(viewGpu);
		};
		for (const RenderView& view : cameraViews)
		{
			appendView(view);
		}
		result.m_ShadowViewBaseOffset = cascades.m_Cascades.empty()
			? DirectionalShadowFramePlan::UnassignedViewBaseOffset : static_cast<uint32_t>(viewData.size());
		for (const DirectionalShadowCascade& cascade : cascades.m_Cascades)
		{
			appendView(cascade.m_View);
		}
		if (postTemporalView)
		{
			result.m_PostTemporalViewOffset = static_cast<uint32_t>(viewData.size());
			appendView(*postTemporalView);
		}
		return result;
	}

	RenderSceneBuilder::BuildResult RenderSceneBuilder::Build(const BuildInfo& info) noexcept
	{
		BuildResult result{};

		auto& registry = info.m_World.GetRegistry();
		auto& transferManager = info.m_TransferManager;
		auto& assetManager = info.m_AssetManager;

		// Reclaim staging uploads. GPU-local structured-buffer allocations are
		// reclaimed by Renderer from the graphics fence timeline.
		transferManager.Reclaim();

		// Assembly RenderInstaces
		result.m_RenderScene.m_RenderInstances.clear();

		using ObjectTable = PersistentStructuredBufferTable<uint64_t, ObjectGPU>;
		using LightTable = PersistentStructuredBufferTable<uint64_t, LightGPU>;
		GGLAB_ASSERT(info.m_FrameSlotIndex < info.m_ObjectsSB.GetBufferCount());
		GGLAB_ASSERT(info.m_FrameSlotIndex < info.m_MaterialsSB.GetBufferCount());
		GGLAB_ASSERT(info.m_FrameSlotIndex < info.m_LightsSB.GetBufferCount());
		info.m_ObjectTable.BeginUpdate();
		info.m_MaterialTable.BeginUpdate();
		info.m_LightTable.BeginUpdate();

		const auto viewUpload = BuildViewData(
			info.m_RenderViews, info.m_DirectionalShadowFramePlan, info.m_PostTemporalView);
		const auto& viewData = viewUpload.m_Views;
		result.m_ShadowViewBaseOffset = viewUpload.m_ShadowViewBaseOffset;
		result.m_PostTemporalViewOffset = viewUpload.m_PostTemporalViewOffset;

		RenderMaterialFrameCache materialCache(
			info.m_MaterialTable, assetManager, info.m_SamplerRegistry);

		registry.view<components::TransformComponent, components::ModelComponent>().each(
			[&result, &assetManager, &info, &registry, &materialCache](auto entity,
				const components::TransformComponent& transformComp,
				const components::ModelComponent& modelComp)
			{
				const auto* model = assetManager.GetModel(modelComp.m_ModelId);
				if (!model)
				{
					GGLAB_LOG_GRAPHICS_WARN("Entity has no model.");
					return;
				}
				if (model->m_ContentState != AssetContentState::Ready ||
					model->m_ResidencyState != AssetResidencyState::Resident)
				{
					return;
				}
				assetManager.MarkModelUsed(modelComp.m_ModelId);

				const Matrix entityWorld = math::CreateTransformMatrix(
					transformComp.m_Scale, transformComp.m_Rotation, transformComp.m_Position);

				for (uint32_t modelMeshIndex = 0; modelMeshIndex < model->m_MeshInstance.size();
					++modelMeshIndex)
				{
					const ModelMesh& modelMesh = model->m_MeshInstance[modelMeshIndex];
					const Mesh* mesh = assetManager.GetMesh(modelMesh.m_MeshId);
					if (!mesh || mesh->m_IndexCount == 0 || !mesh->m_IsUploaded ||
						mesh->m_ResidencyState != AssetResidencyState::Resident)
					{
						continue;
					}
					assetManager.MarkMeshUsed(modelMesh.m_MeshId);

					const Matrix world = modelMesh.m_LocalTransform * entityWorld;
					const Matrix normalMat = math::CreateNormalMatrix(world);

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

					const auto resolvedMaterial = materialCache.Resolve(materialKey, *material);
					if (resolvedMaterial.m_KeyCollision)
					{
						GGLAB_LOG_GRAPHICS_WARN(
							"Render material key collision for domain={} value=0x{:016X}.",
							static_cast<uint32_t>(materialKey.m_Domain), materialKey.m_Value);
					}
					if (resolvedMaterial.m_Index == RenderMaterialFrameCache::MaterialTable::InvalidSlot)
					{
						continue;
					}

					ObjectGPU objectGpu{};
					objectGpu.ModelMat = world;
					RenderObjectHistoryKey objectHistoryKey{};
					if (info.m_TemporalFrameTransaction)
					{
						objectHistoryKey = {
							.m_EntityIdentity =
								static_cast<uint32_t>(entt::to_integral(entity)),
							.m_ModelId = modelComp.m_ModelId,
							.m_ModelContentGeneration = model->m_ContentGeneration,
							.m_ModelMeshIndex = modelMeshIndex,
							.m_MeshId = modelMesh.m_MeshId,
							.m_MeshContentGeneration = mesh->m_ContentGeneration,
							.m_SessionIdentity =
								info.m_TemporalFrameTransaction->GetSessionIdentity(),
						};
						objectGpu.PreviousModelMat =
							info.m_TemporalFrameTransaction->ResolvePreviousObjectModel(
								objectHistoryKey, world);
					}
					else
					{
						objectGpu.PreviousModelMat = world;
					}
					objectGpu.NormalMat = normalMat;
					objectGpu.MaterialIndex = resolvedMaterial.m_Index;

					const uint64_t objectKey =
						(static_cast<uint64_t>(entt::to_integral(entity)) << 32) | modelMeshIndex;
					const uint32_t objectOffset = info.m_ObjectTable.Upsert(objectKey, objectGpu);
					if (objectOffset == ObjectTable::InvalidSlot)
					{
						continue;
					}
					if (info.m_TemporalFrameTransaction)
					{
						GGLAB_ASSERT_MSG(
							info.m_TemporalFrameTransaction->StageSubmittedObject(
								objectHistoryKey, world),
							"Submitted object history must fit the bounded GPU object capacity.");
					}

					Vector3 worldCenter = transformComp.m_Position;
					math::Sphere worldBounds(worldCenter, 0.0f);
					bool hasWorldBounds = false;
					if (mesh->m_HasBounds)
					{
						worldCenter = math::TransformPoint(mesh->m_Aabb.m_Center, world);
						worldBounds = math::Transform(mesh->m_Sphere, world);
						hasWorldBounds = true;
					}

					RenderInstance renderInstance{};
					renderInstance.m_MeshId = modelMesh.m_MeshId;
					renderInstance.m_MaterialKey = materialKey;
					renderInstance.m_MaterialFlags = resolvedMaterial.m_Flags;
					renderInstance.m_AlphaMode = resolvedMaterial.m_AlphaMode;
					renderInstance.m_ObjectOffset = objectOffset;
					renderInstance.m_MaterialOffset = resolvedMaterial.m_Index;
					renderInstance.m_WorldCenterPos = worldCenter;
					renderInstance.m_WorldBounds = worldBounds;
					renderInstance.m_HasWorldBounds = hasWorldBounds;
					result.m_RenderScene.m_RenderInstances.push_back(renderInstance);
					result.m_RenderScene.m_HasMaterialDiagnostics |= resolvedMaterial.m_HasDiagnosticView;
				}
			});

		result.m_RenderScene.m_Atmosphere = info.m_World.m_Atmosphere;
		if (const auto& active = info.m_EnvironmentLightingSystem.GetActivePhysicalSky())
		{
			result.m_RenderScene.m_Atmosphere = active->m_Settings;
		}
		else if (info.m_EnvironmentLightingSystem.GetSettings().m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky)
		{
			// Keep the texture path until the first complete physical generation is available.
			result.m_RenderScene.m_Atmosphere.reset();
		}
		result.m_RenderScene.m_WorldSun = info.m_MainDirectionalLight.m_WorldSun;
		uint32_t directionalShadowLightSlot = LightTable::InvalidSlot;

		// Light data
		{
			bool foundLight = false;
			auto lightView =
				registry.view<components::TransformComponent, components::LightComponent>();
			for (auto&& [entity, transComp, lightComp] : lightView.each())
			{
				const uint64_t lightKey = static_cast<uint64_t>(entt::to_integral(entity));
				const LightGPU lightGpu = BuildLightData(lightKey, transComp, lightComp, info.m_MainDirectionalLight);
				const uint32_t lightSlot = info.m_LightTable.Upsert(lightKey, lightGpu);
				foundLight = foundLight || lightSlot != LightTable::InvalidSlot;
				if (lightSlot != LightTable::InvalidSlot)
				{
					GGLAB_ASSERT(lightSlot < MaxLightCapacity);
					result.m_RenderScene.m_LightTypesByIndex[lightSlot] =
						static_cast<uint32_t>(lightComp.m_Type);
					if (lightComp.m_Type == LightType::Directional)
					{
						++result.m_RenderScene.m_DirectionalLightCount;
						result.m_RenderScene.m_GlobalLightIndices.push_back(lightSlot);
						if (info.m_MainDirectionalLight.m_EntityKey == lightKey &&
							info.m_MainDirectionalLight.m_WorldSun)
						{
							result.m_RenderScene.m_WorldSunLightIndex = lightSlot;
						}
					}
					else
					{
						++result.m_RenderScene.m_LocalLightCount;
					}
				}
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
				lightGpu.Color = Color::White;
				lightGpu.Intensity = 1.0f;
				lightGpu.Range = 1000.0f;
				lightGpu.SpotAngle = 60.0f;
				lightGpu.LightType = static_cast<uint32_t>(LightType::Directional);
				const uint32_t lightSlot = info.m_LightTable.Upsert(DefaultLightKey, lightGpu);
				GGLAB_ASSERT(lightSlot != LightTable::InvalidSlot);
				if (lightSlot != LightTable::InvalidSlot)
				{
					GGLAB_ASSERT(lightSlot < MaxLightCapacity);
					result.m_RenderScene.m_LightTypesByIndex[lightSlot] =
						static_cast<uint32_t>(LightType::Directional);
					++result.m_RenderScene.m_DirectionalLightCount;
					result.m_RenderScene.m_GlobalLightIndices.push_back(lightSlot);
				}
			}
		}

		info.m_ObjectTable.EndUpdate();
		info.m_MaterialTable.EndUpdate();
		info.m_LightTable.EndUpdate();

		// Update View Structured Buffer
		RHIFencePoint uploadFencePoint{};
		DynamicStructuredBufferAllocator<ViewGPU>::Allocation viewsBufferResult{};
		if (!viewData.empty())
		{
			viewsBufferResult = info.m_ViewsSB.Upload(std::span<const ViewGPU>(viewData));
		}

		const auto objectDirtyRanges =
			info.m_ObjectTable.BuildDirtyRanges(info.m_FrameSlotIndex);
		const auto materialDirtyRanges =
			info.m_MaterialTable.BuildDirtyRanges(info.m_FrameSlotIndex);
		const auto lightDirtyRanges =
			info.m_LightTable.BuildDirtyRangesIncludingFreeSlots(info.m_FrameSlotIndex);
		bool objectsUploadSucceeded = true;
		bool materialsUploadSucceeded = true;
		bool lightsUploadSucceeded = true;

		// Only upload changed contiguous ranges into the physical buffer version
		// associated with the current frame slot.
		if (!objectDirtyRanges.empty() || !materialDirtyRanges.empty() || !lightDirtyRanges.empty())
		{
			auto batch = transferManager.BeginBatch();

			const RHIBufferHandle objectBuffer =
				info.m_ObjectsSB.GetBufferHandle(info.m_FrameSlotIndex);
			for (const auto& range : objectDirtyRanges)
			{
				const std::span<const ObjectGPU> data = info.m_ObjectTable.GetData(range);
				objectsUploadSucceeded &= batch.UploadBuffer(objectBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(ObjectGPU), data.data(),
					data.size_bytes());
			}

			const RHIBufferHandle materialBuffer =
				info.m_MaterialsSB.GetBufferHandle(info.m_FrameSlotIndex);
			for (const auto& range : materialDirtyRanges)
			{
				const std::span<const MaterialGPU> data = info.m_MaterialTable.GetData(range);
				materialsUploadSucceeded &= batch.UploadBuffer(materialBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(MaterialGPU), data.data(),
					data.size_bytes());
			}

			const RHIBufferHandle lightBuffer =
				info.m_LightsSB.GetBufferHandle(info.m_FrameSlotIndex);
			for (const auto& range : lightDirtyRanges)
			{
				const std::span<const LightGPU> data = info.m_LightTable.GetData(range);
				lightsUploadSucceeded &= batch.UploadBuffer(lightBuffer,
					static_cast<uint64_t>(range.m_FirstElement) * sizeof(LightGPU), data.data(),
					data.size_bytes());
			}

			uploadFencePoint = batch.Submit(false).m_Completion;
			if (objectsUploadSucceeded)
			{
				info.m_ObjectTable.Commit(info.m_FrameSlotIndex, objectDirtyRanges);
			}
			if (materialsUploadSucceeded)
			{
				info.m_MaterialTable.Commit(info.m_FrameSlotIndex, materialDirtyRanges);
			}
			if (lightsUploadSucceeded)
			{
				info.m_LightTable.Commit(info.m_FrameSlotIndex, lightDirtyRanges);
			}
		}
		else
		{
			info.m_ObjectTable.Commit(info.m_FrameSlotIndex, {});
			info.m_MaterialTable.Commit(info.m_FrameSlotIndex, {});
			info.m_LightTable.Commit(info.m_FrameSlotIndex, {});
		}

		result.m_UploadFencePoint = uploadFencePoint;
		result.m_GpuAllocations.m_Views = viewsBufferResult;

		const bool viewsUploadSucceeded = viewData.empty() || viewsBufferResult.IsValid();

		if (objectsUploadSucceeded && materialsUploadSucceeded && lightsUploadSucceeded &&
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
			for (uint32_t& globalLightIndex : result.m_RenderScene.m_GlobalLightIndices)
			{
				globalLightIndex += result.m_RenderScene.m_LightBaseIndex;
			}
			SortForwardPlusGlobalLightIndices(result.m_RenderScene.m_GlobalLightIndices);
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
				objectsUploadSucceeded, materialsUploadSucceeded, lightsUploadSucceeded,
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
		sceneCB.WorldSunLightIndex = result.m_RenderScene.m_WorldSunLightIndex;
		if (result.m_RenderScene.m_WorldSun)
		{
			sceneCB.WorldSunAngularRadius = result.m_RenderScene.m_WorldSun->m_AngularRadiusRadians;
		}
		if (result.m_RenderScene.m_Atmosphere && result.m_RenderScene.m_WorldSun)
		{
			const auto atmosphere = ResolveAtmosphere(*result.m_RenderScene.m_Atmosphere,
				*result.m_RenderScene.m_WorldSun, Vector3::Zero);
			sceneCB.AtmosphereWorld = atmosphere.m_World;
			sceneCB.AtmosphereRadii = atmosphere.m_Radii;
		}

		info.m_RenderResourceRegistry.FillIBLBindlessGPU(sceneCB.IBLResource);
		const auto& environmentSettings = info.m_EnvironmentLightingSystem.GetRenderSettings();
		sceneCB.IBLResource.EnvironmentIntensity = environmentSettings.m_Intensity;
		sceneCB.IBLResource.EnvironmentRotationRadians = environmentSettings.m_RotationRadians;

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

		result.m_GpuAllocations.m_ShadowConstants = info.m_SceneCB.Upload(
			BuildDirectionalShadowGPU(info.m_DirectionalShadowFramePlan, viewUpload.m_ShadowViewBaseOffset));
		if (!result.m_GpuAllocations.m_ShadowConstants.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("RenderSceneBuilder: Shadow constant allocation failed.");
			result.m_Status = RenderSceneBuildStatus::GpuUploadFailed;
		}
		else
		{
			result.m_RenderScene.m_ShadowConstantBufferOffset =
				result.m_GpuAllocations.m_ShadowConstants.m_OffsetInBytes;
		}

		return result;
	}
}
