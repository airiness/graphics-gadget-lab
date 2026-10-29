#include "GGLabRuntime/Graphics/RayScene.h"
#include "Graphics/MaterialGpuEncoder.h"
#include "Graphics/RayScenePlan.h"
#include "GGLabRuntime/Core/Math/Transform.h"
#include "GGLabRuntime/Core/Math/BoundingVolumes.h"
#include "GGLabRuntime/Core/World.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/VertexData.h"
#include "GGLabRuntime/Scene/Components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <utility>

namespace gglab
{
	namespace
	{
		static_assert(sizeof(Vertex) == 56 && offsetof(Vertex, m_Position) == 0 &&
			offsetof(Vertex, m_Normal) == 12 && offsetof(Vertex, m_TexCoord0) == 24 &&
			offsetof(Vertex, m_TexCoord1) == 32 && offsetof(Vertex, m_Tangent) == 40);
		struct EmptyData {};

		struct GeometryCache
		{
			RHIDevice* m_Device = nullptr;
			RHIBufferOwner m_Vertices;
			RHIBufferOwner m_Indices;
			RHIBufferDesc m_VertexDesc;
			RHIBufferDesc m_IndexDesc;
			std::vector<RHIAccelerationStructureHandle> m_BottomLevels;
			std::vector<RHIASBuildSizes> m_Sizes;
			std::vector<RHIASIndexedTriangles> m_Inputs;
			bool m_Built = false;

			~GeometryCache()
			{
				for (const auto handle : m_BottomLevels)
				{
					m_Device->DestroyAccelerationStructure(handle);
				}
			}
		};

		struct TopLevelCache
		{
			RHIDevice* m_Device = nullptr;
			RHIAccelerationStructureHandle m_Handle;
			RHIASBuildSizes m_Sizes;
			std::vector<RHIASInstanceDesc> m_Instances;
			bool m_Built = false;
			~TopLevelCache()
			{
				if (m_Handle.IsValid())
				{
					m_Device->DestroyAccelerationStructure(m_Handle);
				}
			}
		};

		struct SceneSnapshot
		{
			// Destruction releases the TLAS before its BLAS/pools. Submission leases
			// retain this complete owner on every build and query, including failures.
			std::shared_ptr<GeometryCache> m_Geometry;
			std::shared_ptr<TopLevelCache> m_Top;
			RayScenePlan m_Plan;
			std::array<RHIBufferOwner, 3> m_Tables;
			std::array<RHIBufferOwner, 3> m_Uploads;
			std::array<RHIBufferDesc, 3> m_TableDescs;
			bool m_TablesUploaded = false;
		};

		bool CreateTables(SceneSnapshot& snapshot, RHIDevice& device) noexcept
		{
			std::vector<RaySceneGeometryGPU> geometries;
			for (const auto& geometry : snapshot.m_Plan.m_Geometries)
			{
				geometries.push_back(geometry.m_GPU);
			}
			const std::array data{
				std::as_bytes(std::span(geometries)),
				std::as_bytes(std::span(snapshot.m_Plan.m_Instances)),
				std::as_bytes(std::span(snapshot.m_Plan.m_Materials)) };
			const std::array<uint32_t, 3> strides{
				sizeof(RaySceneGeometryGPU), sizeof(RaySceneInstanceGPU), sizeof(MaterialGPU) };
			for (size_t index = 0; index < data.size(); ++index)
			{
				auto& desc = snapshot.m_TableDescs[index];
				desc = { .m_SizeInBytes = data[index].size(), .m_StrideInBytes = strides[index],
					.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest | RHIBufferUsage::CopySource };
				snapshot.m_Tables[index] = RHIBufferOwner(&device, device.CreateBuffer(desc));
				snapshot.m_Uploads[index] = RHIBufferOwner(&device, device.CreateBuffer({
					.m_SizeInBytes = desc.m_SizeInBytes, .m_Usage = RHIBufferUsage::CopySource,
					.m_MemoryUsage = RHIMemoryUsage::CpuToGpu }));
				if (!snapshot.m_Tables[index] || !snapshot.m_Uploads[index])
				{
					return false;
				}
				auto* mapped = device.MapBuffer(snapshot.m_Uploads[index].Get(), {});
				if (!mapped)
				{
					return false;
				}
				std::memcpy(mapped, data[index].data(), data[index].size());
				device.UnmapBuffer(snapshot.m_Uploads[index].Get(), { 0, data[index].size() });
			}
			return true;
		}

		std::shared_ptr<GeometryCache> CreateGeometry(const RayScenePlan& plan, RHIDevice& device,
			std::string& reason) noexcept
		{
			auto cache = std::make_shared<GeometryCache>();
			cache->m_Device = &device;
			// Raw, shader-readable byte-address pools retain the raster Vertex layout.
			cache->m_VertexDesc = { .m_SizeInBytes = plan.m_VertexBytes,
				.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest |
					RHIBufferUsage::CopySource | RHIBufferUsage::AccelerationStructureInput,
				.m_DebugName = "RayScene.Vertices" };
			cache->m_IndexDesc = cache->m_VertexDesc;
			cache->m_IndexDesc.m_SizeInBytes = plan.m_IndexBytes;
			cache->m_IndexDesc.m_DebugName = "RayScene.Indices";
			cache->m_Vertices = RHIBufferOwner(&device, device.CreateBuffer(cache->m_VertexDesc));
			cache->m_Indices = RHIBufferOwner(&device, device.CreateBuffer(cache->m_IndexDesc));
			if (!cache->m_Vertices || !cache->m_Indices)
			{
				reason = "Ray geometry byte pool allocation failed.";
				return {};
			}
			for (const auto& geometry : plan.m_Geometries)
			{
				const auto& table = geometry.m_GPU;
				const RHIASIndexedTriangles triangles{
					.m_Vertices = { cache->m_Vertices.Get(), table.m_VertexBaseInBytes,
						uint64_t(table.m_VertexCount) * sizeof(Vertex) },
					.m_VertexCount = table.m_VertexCount, .m_VertexStrideInBytes = sizeof(Vertex),
					.m_Indices = { cache->m_Indices.Get(), table.m_IndexBaseInBytes,
						uint64_t(table.m_TriangleCount) * 3 * sizeof(uint32_t) },
					.m_IndexCount = table.m_TriangleCount * 3 };
				const auto sizes = device.QueryAccelerationStructureBuildSizes({ .m_Input = triangles });
				if (!sizes)
				{
					const auto valid = ValidateRHIASIndexedTriangles(triangles,
						cache->m_VertexDesc, cache->m_IndexDesc, device.GetRayQueryCapabilities().m_BuildLimits);
					reason = std::format("BLAS prebuild failed: mesh={}, vertices={}, indices={}, vertexBase={}, indexBase={}, validation={}.",
						geometry.m_Key.m_Mesh.Value(), triangles.m_VertexCount, triangles.m_IndexCount,
						table.m_VertexBaseInBytes, table.m_IndexBaseInBytes, uint32_t(valid.m_Error));
					return {};
				}
				const auto handle = device.CreateAccelerationStructure({
					.m_SizeInBytes = sizes->m_StorageSizeInBytes, .m_DebugName = "RayScene.BLAS" });
				if (!handle.IsValid())
				{
					reason = std::format("BLAS allocation failed: mesh={}, storageBytes={}.",
						geometry.m_Key.m_Mesh.Value(), sizes->m_StorageSizeInBytes);
					return {};
				}
				cache->m_BottomLevels.push_back(handle);
				cache->m_Sizes.push_back(*sizes);
				cache->m_Inputs.push_back(triangles);
			}
			return cache;
		}

		std::shared_ptr<TopLevelCache> CreateTopLevel(const RayScenePlan& plan,
			const GeometryCache& geometry, RHIDevice& device) noexcept
		{
			auto top = std::make_shared<TopLevelCache>();
			top->m_Device = &device;
			for (uint32_t index = 0; index < plan.m_Instances.size(); ++index)
			{
				const auto& instance = plan.m_Instances[index];
				if ((instance.m_Flags & 1) != 0)
				{
					top->m_Instances.push_back({
						.m_BottomLevel = geometry.m_BottomLevels[instance.m_GeometryIndex],
						.m_Transform = plan.m_Sources[index].m_Transform,
						.m_InstanceIndex = index, .m_Mask = 1,
						.m_Flags = RHIASInstanceFlags::TriangleCullDisable });
				}
			}
			const auto sizes = device.QueryAccelerationStructureBuildSizes({
				.m_Input = std::span<const RHIASInstanceDesc>(top->m_Instances) });
			if (!sizes)
			{
				return {};
			}
			top->m_Sizes = *sizes;
			top->m_Handle = device.CreateAccelerationStructure({
				.m_Type = RHIAccelerationStructureType::TopLevel,
				.m_SizeInBytes = sizes->m_StorageSizeInBytes, .m_DebugName = "RayScene.TLAS" });
			return top->m_Handle.IsValid() ? top : nullptr;
		}
	}

	struct RayScene::State
	{
		RaySceneCapacity m_Capacity;
		RaySceneDiagnostics m_Diagnostics;
		std::shared_ptr<SceneSnapshot> m_Snapshot;
	};

	RayScene::RayScene(RaySceneCapacity capacity) noexcept : m_State(std::make_unique<State>())
	{
		m_State->m_Capacity = capacity;
	}
	RayScene::~RayScene() = default;
	const RaySceneDiagnostics& RayScene::GetDiagnostics() const noexcept { return m_State->m_Diagnostics; }

	RaySceneStatus RayScene::Prepare(const World& world, AssetManager& assets,
		RHIDevice& device, const RenderSamplerAccess& samplers) noexcept
	{
		auto& diagnostics = m_State->m_Diagnostics;
		auto fail = [&diagnostics](RaySceneStatus status, const char* reason) noexcept
			{
				diagnostics.m_Status = status;
				diagnostics.m_Reason = reason;
				return status;
			};
		const auto capabilities = device.GetRayQueryCapabilities();
		if (!capabilities.m_Enabled)
		{
			return fail(RaySceneStatus::Unavailable, "Ray queries are not enabled on the device.");
		}
		std::vector<RaySceneSource> sources;
		std::optional<math::Aabb> bounds;
		bool completeBounds = true;
		diagnostics.m_HasCompleteBounds = false;
		diagnostics.m_BoundsDiagonal = 0.0f;
		const auto& registry = world.GetRegistry();
		for (auto&& [entity, transform, modelComponent] : registry.view<
			const components::TransformComponent, const components::ModelComponent>().each())
		{
			const auto* model = assets.GetModel(modelComponent.m_ModelId);
			if (!model || model->m_State != AssetState::Ready)
			{
				return fail(RaySceneStatus::NotReady, "A World model is not Ready; no partial snapshot is published.");
			}
			assets.MarkModelUsed(modelComponent.m_ModelId);
			const auto entityWorld = math::CreateTransformMatrix(
				transform.m_Scale, transform.m_Rotation, transform.m_Position);
			for (uint32_t index = 0; index < model->m_MeshInstance.size(); ++index)
			{
				const auto& modelMesh = model->m_MeshInstance[index];
				const auto* mesh = assets.GetMesh(modelMesh.m_MeshId);
				if (!mesh || mesh->m_State != AssetState::Ready || !mesh->m_IsUploaded)
				{
					return fail(RaySceneStatus::NotReady, "A World mesh is not Ready.");
				}
				assets.MarkMeshUsed(modelMesh.m_MeshId);
				const auto* overrideMaterial = registry.try_get<components::MaterialInstanceComponent>(entity);
				const bool overridden = overrideMaterial && overrideMaterial->m_Key.IsValid();
				const MaterialProperties* material = overridden ?
					&overrideMaterial->m_Properties : assets.GetMaterial(modelMesh.m_MaterialId);
				if (!material)
				{
					return fail(RaySceneStatus::InvalidSource, "A World mesh has no material.");
				}
				if (material->m_AlphaMode == AlphaMode::Opaque && !mesh->m_IsRayGeometryValid)
				{
					return fail(RaySceneStatus::InvalidSource, "Ray mesh indices or positions failed upload validation.");
				}
				const auto matrix = modelMesh.m_LocalTransform * entityWorld;
				if (material->m_AlphaMode == AlphaMode::Opaque)
				{
					completeBounds = completeBounds && mesh->m_HasBounds;
					if (mesh->m_HasBounds)
					{
						const auto instanceBounds = math::Transform(mesh->m_Aabb, matrix);
						bounds = bounds ? math::Merge(*bounds, instanceBounds) : instanceBounds;
					}
				}
				const auto vertices = device.QueryBufferDescription(mesh->m_VertexBufferBinding.m_Buffer);
				const auto indices = device.QueryBufferDescription(mesh->m_IndexBufferBinding.m_Buffer);
				if (!vertices || !indices)
				{
					return fail(RaySceneStatus::InvalidSource, "Ray mesh buffer descriptions are unavailable.");
				}
				sources.push_back({
					.m_EntityIdentity = static_cast<uint32_t>(entt::to_integral(entity)),
					.m_Model = model->m_Id, .m_ModelGeneration = model->m_ContentGeneration,
					.m_ModelMeshIndex = index,
					.m_Geometry = { mesh->m_Id, mesh->m_ContentGeneration },
					.m_Vertices = mesh->m_VertexBufferBinding, .m_Indices = mesh->m_IndexBufferBinding,
					.m_VertexBufferDesc = *vertices, .m_IndexBufferDesc = *indices,
					.m_VertexCount = mesh->m_VertexCount, .m_IndexCount = mesh->m_IndexCount,
					.m_MaterialKey = overridden ? RenderMaterialKey::FromRuntime(overrideMaterial->m_Key) :
						RenderMaterialKey::FromAsset(modelMesh.m_MaterialId),
					.m_Material = MaterialGpuEncoder::Encode(*material, assets, samplers),
					.m_AlphaMode = material->m_AlphaMode,
					.m_Transform = { matrix.m_11, matrix.m_21, matrix.m_31, matrix.m_41,
						matrix.m_12, matrix.m_22, matrix.m_32, matrix.m_42,
						matrix.m_13, matrix.m_23, matrix.m_33, matrix.m_43 } });
			}
		}
		RayScenePlan plan;
		diagnostics.m_Status = PlanRayScene(std::move(sources), m_State->m_Capacity,
			capabilities.m_BuildLimits, plan, diagnostics.m_Reason);
		if (diagnostics.m_Status != RaySceneStatus::Prepared)
		{
			return diagnostics.m_Status;
		}
		if (bounds && completeBounds)
		{
			diagnostics.m_BoundsDiagonal = 2.0f * bounds->m_Extents.Length();
			diagnostics.m_HasCompleteBounds = std::isfinite(diagnostics.m_BoundsDiagonal) &&
				diagnostics.m_BoundsDiagonal > 0.0f;
		}
		const auto previous = m_State->m_Snapshot;
		if (previous && HasSameRayInstances(previous->m_Plan, plan))
		{
			return diagnostics.m_Status;
		}
		auto snapshot = std::make_shared<SceneSnapshot>();
		snapshot->m_Plan = std::move(plan);
		const bool sameGeometry = previous && HasSameRayGeometry(previous->m_Plan, snapshot->m_Plan);
		snapshot->m_Geometry = sameGeometry ? previous->m_Geometry :
			CreateGeometry(snapshot->m_Plan, device, diagnostics.m_Reason);
		if (!snapshot->m_Geometry)
		{
			diagnostics.m_Status = RaySceneStatus::AllocationFailed;
			return diagnostics.m_Status;
		}
		const bool sameTransforms = previous && HasSameRayTransforms(previous->m_Plan, snapshot->m_Plan);
		snapshot->m_Top = sameTransforms ? previous->m_Top :
			CreateTopLevel(snapshot->m_Plan, *snapshot->m_Geometry, device);
		if (!snapshot->m_Top || !CreateTables(*snapshot, device))
		{
			return fail(RaySceneStatus::AllocationFailed, "Ray TLAS or snapshot table allocation failed.");
		}
		++diagnostics.m_Revision;
		if (!sameGeometry)
		{
			++diagnostics.m_GeometryRevision;
		}
		diagnostics.m_GeometryCount = static_cast<uint32_t>(snapshot->m_Plan.m_Geometries.size());
		diagnostics.m_InstanceCount = static_cast<uint32_t>(snapshot->m_Plan.m_Instances.size());
		diagnostics.m_OpaqueInstanceCount = snapshot->m_Plan.m_OpaqueCount;
		diagnostics.m_ExcludedBlendCount = snapshot->m_Plan.m_BlendCount;
		diagnostics.m_MaterialCount = static_cast<uint32_t>(snapshot->m_Plan.m_Materials.size());
		diagnostics.m_VertexBytes = snapshot->m_Plan.m_VertexBytes;
		diagnostics.m_IndexBytes = snapshot->m_Plan.m_IndexBytes;
		m_State->m_Snapshot = std::move(snapshot);
		return diagnostics.m_Status;
	}

	RaySceneGraphResources RayScene::AddBuildPasses(RenderGraph& graph) noexcept
	{
		GGLAB_ASSERT(m_State->m_Diagnostics.m_Status == RaySceneStatus::Prepared);
		const auto snapshot = m_State->m_Snapshot;
		const auto geometry = snapshot->m_Geometry;
		const auto top = snapshot->m_Top;
		const bool buildGeometry = !geometry->m_Built;
		const bool buildTop = !top->m_Built;
		auto resources = std::make_shared<RaySceneGraphResources>();
		resources->m_GeometryCount = static_cast<uint32_t>(snapshot->m_Plan.m_Geometries.size());
		resources->m_InstanceCount = static_cast<uint32_t>(snapshot->m_Plan.m_Instances.size());
		resources->m_MaterialCount = static_cast<uint32_t>(snapshot->m_Plan.m_Materials.size());
		resources->m_Lifetime = snapshot;
		graph.AddPass<EmptyData>("RayScene.Import",
			[=](RenderGraph::RGBuilder& builder, EmptyData&)
			{
				resources->m_Vertices = builder.ImportBuffer("RayScene.Vertices", geometry->m_Vertices.Get(),
					geometry->m_VertexDesc, buildGeometry ? RGBufferAccess::None : RGBufferAccess::StructuredRead,
					buildGeometry ? RGContentValidity::Undefined : RGContentValidity::Defined);
				resources->m_Indices = builder.ImportBuffer("RayScene.Indices", geometry->m_Indices.Get(),
					geometry->m_IndexDesc, buildGeometry ? RGBufferAccess::None : RGBufferAccess::StructuredRead,
					buildGeometry ? RGContentValidity::Undefined : RGContentValidity::Defined);
				for (size_t index = 0; index < geometry->m_BottomLevels.size(); ++index)
				{
					resources->m_BottomLevels.push_back(builder.Import<RGAccelerationStructureResource>(
						("RayScene.BLAS." + std::to_string(index)).c_str(), geometry->m_BottomLevels[index],
						{ .m_SizeInBytes = geometry->m_Sizes[index].m_StorageSizeInBytes },
						RGAccelerationStructureAccess::QueryRead,
						buildGeometry ? RGContentValidity::Undefined : RGContentValidity::Defined));
				}
				resources->m_TopLevel = builder.Import<RGAccelerationStructureResource>("RayScene.TLAS", top->m_Handle,
					{ .m_Type = RHIAccelerationStructureType::TopLevel, .m_SizeInBytes = top->m_Sizes.m_StorageSizeInBytes },
					RGAccelerationStructureAccess::QueryRead, buildTop ? RGContentValidity::Undefined : RGContentValidity::Defined);
				const std::array names{ "RayScene.GeometryTable", "RayScene.InstanceTable", "RayScene.MaterialTable" };
				const std::array ids{ &resources->m_Geometries, &resources->m_Instances, &resources->m_Materials };
				for (size_t index = 0; index < ids.size(); ++index)
				{
					*ids[index] = builder.ImportBuffer(names[index], snapshot->m_Tables[index].Get(), snapshot->m_TableDescs[index],
						snapshot->m_TablesUploaded ? RGBufferAccess::StructuredRead : RGBufferAccess::None,
						snapshot->m_TablesUploaded ? RGContentValidity::Defined : RGContentValidity::Undefined);
				}
			});
		if (buildGeometry || !snapshot->m_TablesUploaded)
		{
			struct SourceImport
			{
				RHIBufferHandle m_Buffer;
				RGBufferId m_Id;
				RGBufferAccess m_RasterAccess;
			};
			auto sourceIds = std::make_shared<std::vector<SourceImport>>();
			struct CopyData
			{
				std::vector<RGBufferId> m_SourceVertices;
				std::vector<RGBufferId> m_SourceIndices;
			};
			graph.AddPass<CopyData>("RayScene.Copy", RGPassEncoderType::Copy,
				[=](RenderGraph::RGBuilder& builder, CopyData& data)
				{
					if (buildGeometry)
					{
						auto importSource = [&](const char* name, RHIBufferHandle buffer,
							const RHIBufferDesc& desc, RGBufferAccess rasterAccess)
							{
								const auto found = std::find_if(sourceIds->begin(), sourceIds->end(),
									[buffer](const auto& imported)
									{
										return imported.m_Buffer == buffer;
									});
								if (found != sourceIds->end())
								{
									return found->m_Id;
								}
								const auto id = builder.ImportBuffer(name, buffer, desc, rasterAccess, RGContentValidity::Defined);
								GGLAB_UNUSED(builder.Read(id, RGBufferAccess::CopySource));
								sourceIds->push_back({ buffer, id, rasterAccess });
								return id;
							};
						for (size_t index = 0; index < snapshot->m_Plan.m_Geometries.size(); ++index)
						{
							const auto& source = snapshot->m_Plan.m_Geometries[index];
							data.m_SourceVertices.push_back(importSource(
								("RayScene.SourceVertex." + std::to_string(index)).c_str(), source.m_SourceVertices.m_Buffer,
								source.m_SourceVertexDesc,
								RGBufferAccess::Vertex));
							data.m_SourceIndices.push_back(importSource(
								("RayScene.SourceIndex." + std::to_string(index)).c_str(), source.m_SourceIndices.m_Buffer,
								source.m_SourceIndexDesc,
								RGBufferAccess::Index));
						}
						builder.WriteInPlace(resources->m_Vertices, RGBufferAccess::CopyDest);
						builder.WriteInPlace(resources->m_Indices, RGBufferAccess::CopyDest);
					}
					if (!snapshot->m_TablesUploaded)
					{
						const std::array ids{ &resources->m_Geometries, &resources->m_Instances, &resources->m_Materials };
						for (size_t index = 0; index < ids.size(); ++index)
						{
							const auto upload = builder.ImportBuffer(("RayScene.TableUpload." + std::to_string(index)).c_str(),
								snapshot->m_Uploads[index].Get(), { .m_SizeInBytes = snapshot->m_TableDescs[index].m_SizeInBytes,
									.m_Usage = RHIBufferUsage::CopySource, .m_MemoryUsage = RHIMemoryUsage::CpuToGpu },
								RGBufferAccess::CopySource, RGContentValidity::Defined);
							GGLAB_UNUSED(builder.Read(upload, RGBufferAccess::CopySource));
							builder.WriteInPlace(*ids[index], RGBufferAccess::CopyDest);
						}
					}
				},
				[=](RGExecuteContext& context, CopyData& data)
				{
					auto* commands = context.GetCopyCommandContext();
					if (!commands->RetainForSubmission(snapshot))
					{
						return;
					}
					if (buildGeometry)
					{
						for (size_t index = 0; index < data.m_SourceVertices.size(); ++index)
						{
							const auto& source = snapshot->m_Plan.m_Geometries[index];
							const auto& input = geometry->m_Inputs[index];
							commands->CopyBuffer(geometry->m_Vertices.Get(), input.m_Vertices.m_OffsetInBytes,
								source.m_SourceVertices.m_Buffer, source.m_SourceVertices.m_Offset, input.m_Vertices.m_SizeInBytes);
							commands->CopyBuffer(geometry->m_Indices.Get(), input.m_Indices.m_OffsetInBytes,
								source.m_SourceIndices.m_Buffer, source.m_SourceIndices.m_Offset, input.m_Indices.m_SizeInBytes);
						}
					}
					if (!snapshot->m_TablesUploaded)
					{
						for (size_t index = 0; index < snapshot->m_Tables.size(); ++index)
						{
							commands->CopyBuffer(snapshot->m_Tables[index].Get(), 0, snapshot->m_Uploads[index].Get(),
								0, snapshot->m_TableDescs[index].m_SizeInBytes);
						}
						context.DeferCommit([snapshot](const RGCommitContext&) noexcept
							{
								snapshot->m_TablesUploaded = true;
							});
					}
				});
			// Raster sources are borrowed for the copy and returned to their original states.
			graph.AddPass<EmptyData>("RayScene.RestoreRasterSources",
				[=](RenderGraph::RGBuilder& builder, EmptyData&)
				{
					if (buildGeometry)
					{
						for (const auto& source : *sourceIds)
						{
							builder.Export(source.m_Id, source.m_RasterAccess);
						}
					}
				});
		}
		if (buildGeometry || buildTop)
		{
			uint64_t scratchBytes = top->m_Sizes.m_ScratchSizeInBytes;
			for (const auto& sizes : geometry->m_Sizes)
			{
				scratchBytes = std::max(scratchBytes, sizes.m_ScratchSizeInBytes);
			}
			auto scratch = std::make_shared<RGBufferId>();
			graph.AddPass<EmptyData>("RayScene.Scratch", [=](RenderGraph::RGBuilder& builder, EmptyData&)
				{
					*scratch = builder.CreateBuffer("RayScene.BuildScratch", { .m_SizeInBytes = scratchBytes });
				});
			if (buildGeometry)
			{
				for (size_t index = 0; index < geometry->m_BottomLevels.size(); ++index)
				{
					graph.AddPass<EmptyData>("RayScene.BuildBLAS", RGPassEncoderType::Compute,
						[=](RenderGraph::RGBuilder& builder, EmptyData&)
						{
							GGLAB_UNUSED(builder.Read(resources->m_Vertices, RGBufferAccess::AccelerationStructureInput));
							GGLAB_UNUSED(builder.Read(resources->m_Indices, RGBufferAccess::AccelerationStructureInput));
							builder.WriteInPlace(resources->m_BottomLevels[index], RGAccelerationStructureAccess::BuildWrite);
							builder.WriteInPlace(*scratch, RGBufferAccess::AccelerationStructureScratch);
						},
						[=](RGExecuteContext& context, EmptyData&)
						{
							auto* commands = context.GetDirectComputeCommandContext();
							if (commands->RetainForSubmission(snapshot))
							{
								GGLAB_UNUSED(commands->BuildAccelerationStructure(geometry->m_BottomLevels[index],
									{ .m_Input = geometry->m_Inputs[index] }, { context.GetBufferHandle(*scratch), 0, scratchBytes }));
							}
						});
				}
			}
			if (buildTop)
			{
				graph.AddPass<EmptyData>("RayScene.BuildTLAS", RGPassEncoderType::Compute,
					[=](RenderGraph::RGBuilder& builder, EmptyData&)
					{
						for (const auto id : resources->m_BottomLevels)
						{
							GGLAB_UNUSED(builder.Read(id, RGAccelerationStructureAccess::BuildRead));
						}
						builder.WriteInPlace(resources->m_TopLevel, RGAccelerationStructureAccess::BuildWrite);
						builder.WriteInPlace(*scratch, RGBufferAccess::AccelerationStructureScratch);
					},
					[=, state = m_State.get()](RGExecuteContext& context, EmptyData&)
					{
						auto* commands = context.GetDirectComputeCommandContext();
						if (!commands->RetainForSubmission(snapshot) || !commands->BuildAccelerationStructure(top->m_Handle,
							{ .m_Input = std::span<const RHIASInstanceDesc>(top->m_Instances) },
							{ context.GetBufferHandle(*scratch), 0, scratchBytes }))
						{
							return;
						}
						context.DeferCommit([snapshot, state, buildGeometry](const RGCommitContext&) noexcept
							{
								snapshot->m_Top->m_Built = true;
								++state->m_Diagnostics.m_TopLevelBuildCount;
								if (buildGeometry)
								{
									snapshot->m_Geometry->m_Built = true;
									state->m_Diagnostics.m_BottomLevelBuildCount += snapshot->m_Plan.m_Geometries.size();
									state->m_Diagnostics.m_GeometryCopyBytes += snapshot->m_Plan.m_VertexBytes + snapshot->m_Plan.m_IndexBytes;
								}
							});
					});
			}
		}
		// Settle to the next import's read states even without a query consumer.
		graph.AddPass<EmptyData>("RayScene.Publish",
			[=](RenderGraph::RGBuilder& builder, EmptyData&)
			{
				for (const auto id : resources->m_BottomLevels)
				{
					GGLAB_UNUSED(builder.Read(id, RGAccelerationStructureAccess::QueryRead));
				}
				GGLAB_UNUSED(builder.Read(resources->m_TopLevel, RGAccelerationStructureAccess::QueryRead));
				for (const auto id : { resources->m_Vertices, resources->m_Indices, resources->m_Geometries,
					resources->m_Instances, resources->m_Materials })
				{
					GGLAB_UNUSED(builder.Read(id, RGBufferAccess::StructuredRead));
				}
				builder.SideEffect();
			},
			[=](RGExecuteContext& context, EmptyData&)
			{
				GGLAB_UNUSED(context.GetGraphicsCommandContext()->RetainForSubmission(snapshot));
			});
		return *resources;
	}
}
