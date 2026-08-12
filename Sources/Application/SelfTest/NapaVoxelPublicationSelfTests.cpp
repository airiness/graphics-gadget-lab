#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "Application/Lab/LabRuntime.h"
#include "Application/Lab/NapaVoxel/NapaVoxelCommands.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRenderState.h"

#include "Core/Input/InputManager.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Core/Task/TaskSystem.h"
#include "Core/Time.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/RenderPipeline/RenderPipelineBase.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHITransferContext.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/Shader/ShaderManager.h"
#include "Graphics/TransferManager.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Edit/VoxelDamage.h"
#include "NapaVoxelCore/Edit/VoxelMutation.h"
#include "NapaVoxelCore/Meshing/CpuMeshBatch.h"
#include "NapaVoxelCore/Meshing/DataOnlyPublication.h"
#include "NapaVoxelCore/Testing/DataOnlyPublicationTestAccess.h"
#include "NapaVoxelCore/World/VoxelRestore.h"

#include <array>
#include <limits>
#include <memory>
#include <thread>
#include <unordered_map>

namespace gglab
{
	namespace
	{
		class NapaVoxelPublicationTestDevice final : public RHIDevice
		{
		public:
			RHIBackendType GetBackendType() const noexcept override
			{
				return RHIBackendType::DX12;
			}
			std::string_view GetAdapterCompatibilityIdentity() const noexcept override
			{
				return "NapaVoxel.PublicationTestDevice";
			}
			RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override
			{
				return {};
			}
			RHITextureSupportResult QueryTextureSupport(
				const RHITextureDesc&) const noexcept override
			{
				return { .m_Supported = true };
			}
			RHITextureSupportResult QueryTextureViewSupport(
				const RHITextureDesc&, const RHITextureViewDesc&) const noexcept override
			{
				return { .m_Supported = true };
			}
			RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo&,
				const RHIResourceDebugIdentityDesc&) noexcept override
			{
				return { m_NextTextureIndex++, 1 };
			}
			RHIBufferHandle CreateBuffer(
				const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc&) noexcept override
			{
				if (m_FailNextBufferCreation)
				{
					m_FailNextBufferCreation = false;
					return {};
				}
				const RHIBufferHandle handle{ m_NextBufferIndex++, 1 };
				m_LiveBuffers.emplace(handle, desc.m_SizeInBytes);
				m_LiveBufferBytes += desc.m_SizeInBytes;
				++m_CreatedBufferCount;
				return handle;
			}
			RHITextureViewHandle CreateTextureView(
				RHITextureHandle, const RHITextureViewDesc&) noexcept override
			{
				return { m_NextTextureViewIndex++, 1 };
			}
			RHIBufferViewHandle CreateBufferView(
				RHIBufferHandle, const RHIBufferViewDesc&) noexcept override
			{
				return {};
			}
			RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override
			{
				return { m_NextSamplerIndex++, 1 };
			}
			void DestroyTexture(RHITextureHandle) noexcept override {}
			void DestroyBuffer(RHIBufferHandle buffer) noexcept override
			{
				const auto found = m_LiveBuffers.find(buffer);
				if (found != m_LiveBuffers.end())
				{
					m_LiveBufferBytes -= found->second;
					m_LiveBuffers.erase(found);
					++m_DestroyedBufferCount;
				}
			}
			void DestroyTextureView(RHITextureViewHandle) noexcept override {}
			void DestroyBufferView(RHIBufferViewHandle) noexcept override {}
			void DestroySampler(RHISamplerHandle) noexcept override {}
			void SetTextureDebugBinding(
				RHITextureHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			void SetBufferDebugBinding(
				RHIBufferHandle, const RHIResourceDebugBindingDesc&) noexcept override
			{
			}
			std::string_view GetTextureDebugName(RHITextureHandle) const noexcept override
			{
				return {};
			}
			std::string_view GetBufferDebugName(RHIBufferHandle) const noexcept override
			{
				return {};
			}
			void* MapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override
			{
				return nullptr;
			}
			void UnmapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override {}
			uint32_t GetBufferViewAlignment(RHIBufferViewType) const noexcept override { return 1; }
			bool IsAlive(RHITextureHandle texture) const noexcept override
			{
				return texture.IsValid();
			}
			bool IsAlive(RHIBufferHandle buffer) const noexcept override
			{
				return m_LiveBuffers.contains(buffer);
			}
			bool IsAlive(RHISamplerHandle sampler) const noexcept override
			{
				return sampler.IsValid();
			}
			bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept override
			{
				if (!fencePoint.IsValid())
				{
					return false;
				}
				if (m_AllFencesCompleted)
				{
					return true;
				}
				for (const RHIFencePoint& completed : m_CompletedFencePoints)
				{
					if (completed.m_Fence == fencePoint.m_Fence &&
						completed.m_Value >= fencePoint.m_Value)
					{
						return true;
					}
				}
				return false;
			}
			void RecordTextureUse(RHITextureHandle, const RHIFencePoint&) noexcept override {}
			void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
			RHIDescriptorHandle GetTextureViewDescriptor(
				RHITextureViewHandle view) const noexcept override
			{
				return {
					.m_HeapType = RHIDescriptorHeapType::CbvSrvUav,
					.m_Index = view.Index(),
				};
			}
			RHIDescriptorHandle GetBufferViewDescriptor(
				RHIBufferViewHandle) const noexcept override
			{
				return {};
			}
			RHIDescriptorHandle GetSamplerDescriptor(
				RHISamplerHandle sampler) const noexcept override
			{
				return {
					.m_HeapType = RHIDescriptorHeapType::Sampler,
					.m_Index = sampler.Index(),
				};
			}
			void RetireCompletedWork() noexcept override {}

			void CompleteFence() noexcept { m_AllFencesCompleted = true; }
			void CompleteFence(RHIFencePoint fencePoint)
			{
				for (RHIFencePoint& completed : m_CompletedFencePoints)
				{
					if (completed.m_Fence == fencePoint.m_Fence)
					{
						completed.m_Value = std::max(completed.m_Value, fencePoint.m_Value);
						return;
					}
				}
				m_CompletedFencePoints.push_back(fencePoint);
			}
			void FailNextBufferCreation() noexcept { m_FailNextBufferCreation = true; }
			uint32_t GetCreatedBufferCount() const noexcept { return m_CreatedBufferCount; }
			uint32_t GetDestroyedBufferCount() const noexcept { return m_DestroyedBufferCount; }
			uint32_t GetLiveBufferCount() const noexcept
			{
				return static_cast<uint32_t>(m_LiveBuffers.size());
			}
			uint64_t GetLiveBufferBytes() const noexcept { return m_LiveBufferBytes; }

		private:
			std::unordered_map<RHIBufferHandle, uint64_t> m_LiveBuffers;
			std::vector<RHIFencePoint> m_CompletedFencePoints;
			uint64_t m_LiveBufferBytes = 0;
			uint32_t m_NextTextureIndex = 0;
			uint32_t m_NextTextureViewIndex = 0;
			uint32_t m_NextSamplerIndex = 0;
			uint32_t m_NextBufferIndex = 0;
			uint32_t m_CreatedBufferCount = 0;
			uint32_t m_DestroyedBufferCount = 0;
			bool m_AllFencesCompleted = false;
			bool m_FailNextBufferCreation = false;
		};

		class NapaVoxelPublicationTestTransferContext final : public RHITransferContext
		{
		public:
			RHICommandContextHandle GetHandle() const noexcept override { return { 1, 1 }; }
			RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Copy; }
			void TrackTextureUse(RHITextureHandle) noexcept override {}
			void TrackBufferUse(RHIBufferHandle) noexcept override {}
			void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
			void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
			void FlushBarriers() noexcept override {}
			void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t,
				uint64_t) noexcept override
			{
			}
			void Begin() noexcept override { m_IsRecording = true; }
			RHIFencePoint Submit(bool) noexcept override
			{
				m_IsRecording = false;
				++m_SubmissionCount;
				return { RHIFenceHandle{ 1, 1 }, m_SubmissionCount };
			}
			void Abort() noexcept override { m_IsRecording = false; }
			void ReclaimCompleted() noexcept override { ++m_ReclaimCount; }
			bool UploadBuffer(const void* data, uint64_t sizeInBytes, RHIBufferHandle destination,
				uint64_t) noexcept override
			{
				if (!m_IsRecording || !data || sizeInBytes == 0 || !destination.IsValid())
				{
					return false;
				}
				++m_UploadCount;
				return !m_FailUploads;
			}
			bool UploadTexture(
				const RHITextureUploadData& data, RHITextureHandle destination) noexcept override
			{
				return m_IsRecording && data.IsValid() && destination.IsValid();
			}
			RHITextureReadbackRequest ReadbackTexture(
				RHITextureHandle, const RHITextureDesc&) noexcept override
			{
				return {};
			}

			uint32_t GetSubmissionCount() const noexcept { return m_SubmissionCount; }
			uint32_t GetUploadCount() const noexcept { return m_UploadCount; }
			void SetFailUploads(bool fail) noexcept { m_FailUploads = fail; }

		private:
			uint32_t m_SubmissionCount = 0;
			uint32_t m_UploadCount = 0;
			uint32_t m_ReclaimCount = 0;
			bool m_IsRecording = false;
			bool m_FailUploads = false;
		};

		[[nodiscard]] uint64_t GetGpuMeshBytes(
			const std::shared_ptr<const NapaVoxelGpuMeshSet>& meshes) noexcept
		{
			uint64_t bytes = 0;
			if (!meshes)
			{
				return bytes;
			}
			for (const std::shared_ptr<const NapaVoxelGpuChunkMesh>& chunk : meshes->GetChunks())
			{
				if (chunk)
				{
					bytes += chunk->m_VertexBufferDesc.m_SizeInBytes;
					bytes += chunk->m_IndexBufferDesc.m_SizeInBytes;
				}
			}
			return bytes;
		}

		[[nodiscard]] bool IsSchedulerQuiescent(
			const AssetUploadStatistics& statistics) noexcept
		{
			const auto queueIsQuiescent = [](const AssetStreamingQueueStatistics& queue) noexcept
				{
					return queue.m_PendingCount == 0 && queue.m_PendingSourceBytes == 0 &&
						queue.m_PendingStagingBytes == 0 && queue.m_PendingOperationCount == 0;
				};
			return statistics.m_ReadyPayloadBytes == 0 && statistics.m_InFlightBytes == 0 &&
				statistics.m_PendingCount == 0 &&
				queueIsQuiescent(statistics.m_CpuPayloadQueue) &&
				queueIsQuiescent(statistics.m_ResourcePublicationQueue) &&
				queueIsQuiescent(statistics.m_UploadRecordingQueue) &&
				queueIsQuiescent(statistics.m_GpuFinalizeQueue);
		}

		[[nodiscard]] napa::voxel::VoxelWorldConfig MakePublicationConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 8, 8, 8 },
					},
			};
		}

		[[nodiscard]] bool BuildPublicationOwner(bool renderable, uint64_t stableId,
			uint64_t ownerGeneration,
			std::shared_ptr<NapaVoxelInitialPublicationOwner>& publication) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakePublicationConfig();
			const PrimitiveDesc sphere{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Soil,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_Radius = 2.0,
						},
					},
			};
			const std::span<const PrimitiveDesc> primitives = renderable
				? std::span<const PrimitiveDesc>(&sphere, 1)
				: std::span<const PrimitiveDesc>{};
			std::unique_ptr<VoxelWorld> world;
			PrimitiveWorldGenerationResult generation{};
			if (GeneratePrimitiveVoxelWorld(config, primitives, world, generation).Failed() || !world)
			{
				return false;
			}

			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch batch{};
			VisibleMeshSet visible{};
			std::unique_ptr<PendingCpuMeshBatch> pending;
			if (BuildCpuMeshBatch(*world, world->GetWorldVoxelRevision(), chunks, batch).Failed() ||
				ValidateCpuMeshBatch(batch, visible, pending).Failed() || !pending)
			{
				return false;
			}

			return PrepareNapaVoxelInitialPublication(
				pending, stableId, ownerGeneration, publication);
		}

		[[nodiscard]] bool BuildPublicationPending(bool renderable,
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pending) noexcept
		{
			using namespace napa::voxel;
			const VoxelWorldConfig config = MakePublicationConfig();
			const PrimitiveDesc sphere{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Soil,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_Radius = 2.0,
					},
				},
			};
			const std::span<const PrimitiveDesc> primitives = renderable
				? std::span<const PrimitiveDesc>(&sphere, 1)
				: std::span<const PrimitiveDesc>{};
			std::unique_ptr<VoxelWorld> world;
			PrimitiveWorldGenerationResult generation{};
			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch batch{};
			VisibleMeshSet visible{};
			return GeneratePrimitiveVoxelWorld(config, primitives, world, generation).Succeeded() &&
				world && BuildCpuMeshBatch(*world, 1, chunks, batch).Succeeded() &&
				ValidateCpuMeshBatch(batch, visible, pending).Succeeded() && pending;
		}

		class NapaVoxelLabSwitchTestPipeline final : public RenderPipelineBase
		{
		public:
			std::string_view GetName() const noexcept override
			{
				return "NapaVoxel.LabSwitchTest";
			}
			void BuildRenderGraph(RenderGraph&, const RenderFrameContext&,
				const RenderServices&) noexcept override
			{
			}
		};

		struct NapaVoxelLabSwitchTestState
		{
			NapaVoxelPublicationTestDevice* m_Device = nullptr;
			AssetUploadScheduler* m_Scheduler = nullptr;
			uint64_t m_NextOwnerGeneration = 1;
			std::vector<uint64_t> m_StartedGenerations;
			uint32_t m_CancelledSessionCount = 0;
			uint32_t m_DestroyedSessionCount = 0;
			uint32_t m_UnexpectedCommitCount = 0;
			bool m_CancelledOnlyAfterSubmission = true;
			bool m_CancelledWithoutVisibleState = true;
		};

		NapaVoxelLabSwitchTestState* s_NapaVoxelLabSwitchTestState = nullptr;

		[[nodiscard]] LabDescriptor MakeLabSwitchTestDescriptor(
			std::string_view id, std::string_view displayName) noexcept
		{
			return {
				.m_Id = LabId(id),
				.m_DisplayName = std::string(displayName),
				.m_Category = "Self Test",
				.m_Description = "Exercises LabRuntime publication cancellation.",
			};
		}

		class NapaVoxelLabSwitchControlSession final : public LabSessionBase
		{
		public:
			explicit NapaVoxelLabSwitchControlSession(
				const LabSessionCreateInfo& createInfo) noexcept :
				LabSessionBase(GetDescriptor(), createInfo,
					std::make_unique<NapaVoxelLabSwitchTestPipeline>())
			{
			}

			void Update(float) noexcept override {}

			static LabId GetId() noexcept
			{
				return LabId("gglab.lab.self_test.control");
			}
			static LabDescriptor GetDescriptor() noexcept
			{
				return MakeLabSwitchTestDescriptor(
					"gglab.lab.self_test.control", "Lab Switch Control");
			}
			static std::unique_ptr<LabSessionBase> Create(
				const LabSessionCreateInfo& createInfo) noexcept
			{
				return std::make_unique<NapaVoxelLabSwitchControlSession>(createInfo);
			}
		};

		class NapaVoxelLabSwitchPendingSession final : public LabSessionBase
		{
		public:
			explicit NapaVoxelLabSwitchPendingSession(
				const LabSessionCreateInfo& createInfo) noexcept :
				LabSessionBase(GetDescriptor(), createInfo,
					std::make_unique<NapaVoxelLabSwitchTestPipeline>()),
				m_State(s_NapaVoxelLabSwitchTestState)
			{
			}

			~NapaVoxelLabSwitchPendingSession() override
			{
				if (m_State)
				{
					++m_State->m_DestroyedSessionCount;
				}
			}

			void BeginPrepare() noexcept override
			{
				if (!m_State || !m_State->m_Device || !m_State->m_Scheduler)
				{
					m_Progress = {
						.m_Status = LoadingStatus::Failed,
						.m_Fraction = 0.0f,
						.m_Stage = "Missing Lab switch test services",
					};
					return;
				}

				m_OwnerGeneration = m_State->m_NextOwnerGeneration++;
				m_State->m_StartedGenerations.push_back(m_OwnerGeneration);
				m_Publication = std::make_unique<NapaVoxelPublicationSession>(
					m_State->m_Device, m_State->m_Scheduler, &m_CommandQueue);
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				if (!BuildPublicationPending(true, pending) ||
					!m_Publication->BeginPrepare(
						pending, 30'000 + m_OwnerGeneration, m_OwnerGeneration))
				{
					m_Progress = {
						.m_Status = LoadingStatus::Failed,
						.m_Fraction = 0.0f,
						.m_Stage = "Failed to begin the pending voxel publication",
					};
					return;
				}

				m_Progress = LoadingProgress{
					.m_Status = LoadingStatus::Preparing,
					.m_Fraction = 0.5f,
					.m_Stage = "Awaiting voxel Copy Fence",
				};
			}

			void TickPrepare() noexcept override
			{
				if (!m_Publication)
				{
					return;
				}
				m_Publication->TickPrepare();
				if (m_Publication->IsReady())
				{
					m_Progress = LoadingProgress::Ready();
				}
				else if (m_Publication->GetPublicationStatus() ==
					NapaVoxelInitialPublicationStatus::Failed)
				{
					m_Progress = {
						.m_Status = LoadingStatus::Failed,
						.m_Fraction = 0.0f,
						.m_Stage = "Voxel publication failed",
					};
				}
			}

			LoadingProgress GetPreparationProgress() const noexcept override
			{
				return m_Progress;
			}

			void CommitPrepare() noexcept override
			{
				if (m_State)
				{
					++m_State->m_UnexpectedCommitCount;
				}
			}

			void CancelPrepare() noexcept override
			{
				if (!m_Publication)
				{
					return;
				}
				if (m_State)
				{
					m_State->m_CancelledOnlyAfterSubmission &=
						m_Publication->GetPublicationStatus() ==
						NapaVoxelInitialPublicationStatus::AwaitingFence;
				}
				m_Publication->CancelPrepare();
				if (m_State)
				{
					m_State->m_CancelledWithoutVisibleState &=
						!m_Publication->IsReady() && !m_Publication->HasVisibleMeshes() &&
						!m_Publication->GetFrameView();
					++m_State->m_CancelledSessionCount;
				}
				m_Publication.reset();
			}

			void Update(float) noexcept override {}

			static LabId GetId() noexcept
			{
				return LabId("gglab.lab.self_test.pending_voxel");
			}
			static LabDescriptor GetDescriptor() noexcept
			{
				return MakeLabSwitchTestDescriptor(
					"gglab.lab.self_test.pending_voxel", "Pending Voxel Publication");
			}
			static std::unique_ptr<LabSessionBase> Create(
				const LabSessionCreateInfo& createInfo) noexcept
			{
				return std::make_unique<NapaVoxelLabSwitchPendingSession>(createInfo);
			}

		private:
			NapaVoxelLabSwitchTestState* m_State = nullptr;
			NapaVoxelCommandQueue m_CommandQueue;
			std::unique_ptr<NapaVoxelPublicationSession> m_Publication;
			LoadingProgress m_Progress{};
			uint64_t m_OwnerGeneration = 0;
		};

		[[nodiscard]] bool BuildInteractivePublicationInput(
			std::unique_ptr<napa::voxel::VoxelWorld>& world,
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pending) noexcept
		{
			using namespace napa::voxel;
			const VoxelWorldConfig config = MakePublicationConfig();
			const PrimitiveDesc sphere{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_Radius = 2.0,
					},
				},
			};
			PrimitiveWorldGenerationResult generation{};
			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch batch{};
			VisibleMeshSet visible{};
			return GeneratePrimitiveVoxelWorld(config,
				std::span<const PrimitiveDesc>(&sphere, 1), world, generation).Succeeded() &&
				world && BuildCpuMeshBatch(*world, 1, chunks, batch).Succeeded() &&
				ValidateCpuMeshBatch(batch, visible, pending).Succeeded() && pending;
		}

		[[nodiscard]] bool BuildReplacementPublicationInput(
			NapaVoxelPublicationTestDevice& device, double strength,
			NapaVoxelRenderState& renderState,
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch>& pending,
			std::vector<napa::voxel::ChunkCoord>& replacementChunks) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config{
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 24, 8, 8 },
					},
			};
			const std::array primitives{
				PrimitiveDesc{
					.m_StableId = { 1 },
					.m_Material = VoxelMaterial::Soil,
					.m_Shape = PrimitiveShape::Sphere,
					.m_Parameters = {
						.m_Sphere = {
							.m_Center = { 4.0, 4.0, 4.0 },
							.m_Radius = 2.0,
							},
						},
					},
				PrimitiveDesc{
					.m_StableId = { 2 },
					.m_Material = VoxelMaterial::Soil,
					.m_Shape = PrimitiveShape::Sphere,
					.m_Parameters = {
						.m_Sphere = {
							.m_Center = { 20.0, 4.0, 4.0 },
							.m_Radius = 2.0,
							},
						},
					},
			};
			std::unique_ptr<VoxelWorld> world;
			PrimitiveWorldGenerationResult generation{};
			if (GeneratePrimitiveVoxelWorld(config, primitives, world, generation).Failed() || !world)
			{
				return false;
			}

			constexpr std::array chunks{
				ChunkCoord{ 0, 0, 0 }, ChunkCoord{ 1, 0, 0 }, ChunkCoord{ 2, 0, 0 },
			};
			CpuMeshBatch initialBatch{};
			VisibleMeshSet emptyVisible{};
			std::unique_ptr<PendingCpuMeshBatch> initialPending;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> initialPublication;
			if (BuildCpuMeshBatch(*world, world->GetWorldVoxelRevision(), chunks,
				initialBatch).Failed() ||
				ValidateCpuMeshBatch(initialBatch, emptyVisible, initialPending).Failed() ||
				!PrepareNapaVoxelInitialPublication(initialPending, 7001, 1,
					initialPublication) ||
				!initialPublication->PrepareGpuResources(&device) ||
				!initialPublication->MarkQueued() ||
				!initialPublication->BeginRecording(initialPublication->GetUploadIdentity()) ||
				!initialPublication->SetUploadHandle({ 701 }) ||
				!initialPublication->CompleteUpload({
					.m_Handle = initialPublication->GetUploadHandle(),
					.m_Identity = initialPublication->GetUploadIdentity(),
					.m_Status = AssetUploadStatus::Succeeded,
					.m_FencePoint = { RHIFenceHandle{ 7, 1 }, 1 },
					}))
			{
				return false;
			}

			std::unique_ptr<NapaVoxelPreparedInitialCommit> initialCommit;
			if (!renderState.PrepareInitialCommit(initialPublication, initialCommit))
			{
				return false;
			}
			renderState.CommitInitial(initialCommit);

			const SphereEditRequest edit{
				.m_Brush = {
					.m_CenterWorld = { 4.0, 4.0, 4.0 },
					.m_Radius = 3.0,
					.m_Strength = strength,
					},
			};
			VoxelMutationResult mutation{};
			CpuMeshBatch replacementBatch{};
			if (ApplySphereEdit(*world, edit, mutation).Failed() || !mutation.Changed() ||
				BuildCpuMeshBatch(*world, mutation, replacementBatch).Failed() ||
				ValidateCpuMeshBatch(replacementBatch, renderState.GetVisibleCoreMeshes(),
					pending).Failed() || !pending)
			{
				return false;
			}

			const CpuMeshReplacementView replacements = pending->GetReplacementChunks();
			replacementChunks.clear();
			replacementChunks.reserve(replacements.size());
			for (size_t index = 0; index < replacements.size(); ++index)
			{
				replacementChunks.push_back(replacements[index].m_Chunk);
			}
			return !replacementChunks.empty();
		}

		[[nodiscard]] bool BuildDamageOnlyPublicationInput(
			NapaVoxelPublicationTestDevice& device, NapaVoxelRenderState& renderState,
			std::unique_ptr<napa::voxel::VoxelWorld>& world,
			napa::voxel::VoxelMutationResult& mutation) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakePublicationConfig();
			const PrimitiveDesc sphere{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_Radius = 2.0,
						},
					},
			};
			PrimitiveWorldGenerationResult generation{};
			if (GeneratePrimitiveVoxelWorld(config,
				std::span<const PrimitiveDesc>(&sphere, 1), world, generation).Failed() ||
				!world)
			{
				return false;
			}

			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch initialBatch{};
			VisibleMeshSet emptyVisible{};
			std::unique_ptr<PendingCpuMeshBatch> initialPending;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> initialPublication;
			if (BuildCpuMeshBatch(*world, world->GetWorldVoxelRevision(), chunks,
				initialBatch).Failed() ||
				ValidateCpuMeshBatch(initialBatch, emptyVisible, initialPending).Failed() ||
				!PrepareNapaVoxelInitialPublication(initialPending, 9001, 1,
					initialPublication) ||
				!initialPublication->PrepareGpuResources(&device) ||
				!initialPublication->MarkQueued() ||
				!initialPublication->BeginRecording(initialPublication->GetUploadIdentity()) ||
				!initialPublication->SetUploadHandle({ 901 }) ||
				!initialPublication->CompleteUpload({
					.m_Handle = initialPublication->GetUploadHandle(),
					.m_Identity = initialPublication->GetUploadIdentity(),
					.m_Status = AssetUploadStatus::Succeeded,
					.m_FencePoint = { RHIFenceHandle{ 9, 1 }, 1 },
					}))
			{
				return false;
			}

			std::unique_ptr<NapaVoxelPreparedInitialCommit> initialCommit;
			if (!renderState.PrepareInitialCommit(initialPublication, initialCommit))
			{
				return false;
			}
			renderState.CommitInitial(initialCommit);

			const SphereEditRequest edit{
				.m_Brush = {
					.m_CenterWorld = { 4.0, 4.0, 4.0 },
					.m_Radius = 2.0,
					.m_Strength = 0.5,
					},
			};
			return ApplySphereEdit(*world, edit, mutation).Succeeded() &&
				mutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly;
		}

		[[nodiscard]] bool BuildCoreDataOnlyPublicationInput(
			std::unique_ptr<napa::voxel::VoxelWorld>& world,
			napa::voxel::VisibleMeshSet& visible,
			napa::voxel::VoxelMutationResult& mutation) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakePublicationConfig();
			const PrimitiveDesc sphere{
				.m_StableId = { 1 },
				.m_Material = VoxelMaterial::Stone,
				.m_Shape = PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = { 4.0, 4.0, 4.0 },
						.m_Radius = 2.0,
						},
					},
			};
			PrimitiveWorldGenerationResult generation{};
			constexpr std::array chunks{ ChunkCoord{} };
			CpuMeshBatch initialBatch{};
			std::unique_ptr<PendingCpuMeshBatch> pending;
			std::unique_ptr<PreparedCpuMeshPublication> publication;
			if (GeneratePrimitiveVoxelWorld(config,
				std::span<const PrimitiveDesc>(&sphere, 1), world, generation).Failed() ||
				!world || BuildCpuMeshBatch(*world, 1, chunks, initialBatch).Failed() ||
				ValidateCpuMeshBatch(initialBatch, visible, pending).Failed() ||
				PrepareCpuMeshBatchPublication(pending, visible, publication).Failed())
			{
				return false;
			}
			CommitCpuMeshBatchPublication(publication, visible);

			const SphereEditRequest edit{
				.m_Brush = {
					.m_CenterWorld = { 4.0, 4.0, 4.0 },
					.m_Radius = 2.0,
					.m_Strength = 0.5,
					},
			};
			return ApplySphereEdit(*world, edit, mutation).Succeeded() &&
				mutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly;
		}

		[[nodiscard]] AssetUploadCompletionInfo MakeCompletion(
			const NapaVoxelInitialPublicationOwner& publication, AssetUploadStatus status,
			RHIFencePoint fence = { RHIFenceHandle{ 1, 1 }, 7 }) noexcept
		{
			return {
				.m_Handle = publication.GetUploadHandle(),
				.m_Identity = publication.GetUploadIdentity(),
				.m_Status = status,
				.m_FencePoint = fence,
			};
		}

		[[nodiscard]] bool AdvanceToAwaitingFence(
			NapaVoxelInitialPublicationOwner& publication, AssetUploadHandle handle) noexcept
		{
			return publication.PrepareGpuResources(nullptr) && publication.MarkQueued() &&
				publication.BeginRecording(publication.GetUploadIdentity()) &&
				publication.SetUploadHandle(handle);
		}

		void RunInitialPublicationStateTests(SelfTestContext& context) noexcept
		{
			std::shared_ptr<NapaVoxelInitialPublicationOwner> publication;
			const bool built = BuildPublicationOwner(false, 1001, 1, publication);
			context.Check(built && publication && publication->GetTargetWorldRevision() == 1 &&
				publication->GetUploadIdentity().m_Kind == AssetStreamingWorkKind::RuntimeMesh &&
				ToAssetKind(publication->GetUploadIdentity().m_Kind) == AssetKind::Unknown,
				"Initial voxel publications use an isolated runtime-mesh Scheduler identity");
			if (!built)
			{
				return;
			}

			NapaVoxelRenderState renderState{};
			const bool awaiting = AdvanceToAwaitingFence(*publication, { 31 });
			context.Check(awaiting && !renderState.HasVisibleMeshes() &&
				renderState.GetVisibleWorldRevision() == 0,
				"Initial CPU and GPU state remains uninitialized before Copy Fence completion");
			const bool completed = publication->CompleteUpload(
				MakeCompletion(*publication, AssetUploadStatus::Succeeded));
			context.Check(completed && publication->IsReadyForCommit() &&
				!renderState.HasVisibleMeshes(),
				"Scheduler completion only marks the durable publication ready for commit");
			std::unique_ptr<NapaVoxelPreparedInitialCommit> preparedCommit;
			const bool prepared =
				renderState.PrepareInitialCommit(publication, preparedCommit);
			if (prepared)
			{
				renderState.CommitInitial(preparedCommit);
			}
			context.Check(prepared && renderState.HasVisibleMeshes() &&
				renderState.GetVisibleWorldRevision() == 1 &&
				publication->GetStatus() == NapaVoxelInitialPublicationStatus::Committed,
				"Owner-thread commit publishes Core and GPU initial state at one revision");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> stalePublication;
			const bool staleReady = BuildPublicationOwner(false, 1006, 1, stalePublication) &&
				stalePublication && AdvanceToAwaitingFence(*stalePublication, { 34 }) &&
				stalePublication->CompleteUpload(
					MakeCompletion(*stalePublication, AssetUploadStatus::Succeeded));
			context.Check(staleReady &&
				!renderState.PrepareInitialCommit(stalePublication, preparedCommit) &&
				renderState.GetVisibleWorldRevision() == 1,
				"Host revalidation rejects a same-revision initial publication before commit");
		}

		void RunInitialPublicationFailureTests(SelfTestContext& context) noexcept
		{
			std::unique_ptr<NapaVoxelPreparedInitialCommit> preparedCommit;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> createFailure;
			NapaVoxelRenderState createFailureState{};
			const bool builtRenderable = BuildPublicationOwner(true, 1002, 1, createFailure);
			context.Check(builtRenderable && createFailure &&
				!createFailure->PrepareGpuResources(nullptr) &&
				createFailure->GetStatus() == NapaVoxelInitialPublicationStatus::Failed &&
				!createFailureState.HasVisibleMeshes(),
				"GPU resource creation failure preserves an uninitialized visible state");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> uploadFailure;
			NapaVoxelRenderState uploadFailureState{};
			const bool builtUploadFailure = BuildPublicationOwner(false, 1003, 1, uploadFailure);
			const bool awaiting = builtUploadFailure &&
				AdvanceToAwaitingFence(*uploadFailure, { 32 });
			const bool failed = awaiting && uploadFailure->CompleteUpload(
				MakeCompletion(*uploadFailure, AssetUploadStatus::Failed));
			context.Check(failed &&
				uploadFailure->GetStatus() == NapaVoxelInitialPublicationStatus::Failed &&
				!uploadFailureState.PrepareInitialCommit(uploadFailure, preparedCommit) &&
				!uploadFailureState.HasVisibleMeshes(),
				"Upload recording or completion failure cannot publish partial CPU/GPU state");
		}

		void RunInitialPublicationCancellationTests(SelfTestContext& context) noexcept
		{
			std::shared_ptr<NapaVoxelInitialPublicationOwner> queued;
			const bool builtQueued = BuildPublicationOwner(false, 1004, 1, queued);
			const bool queuedForRecording = builtQueued && queued &&
				queued->PrepareGpuResources(nullptr) &&
				queued->MarkQueued();
			const AssetStreamingIdentity queuedIdentity = queued
				? queued->GetUploadIdentity()
				: AssetStreamingIdentity{};
			if (queued)
			{
				queued->Cancel();
			}
			context.Check(queuedForRecording &&
				queued->GetStatus() == NapaVoxelInitialPublicationStatus::Cancelled &&
				queued->GetOwnerGeneration() == 2 &&
				!queued->BeginRecording(queuedIdentity),
				"Recording-before cancellation invalidates queued initial upload work");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> inFlight;
			const bool builtInFlight = BuildPublicationOwner(false, 1005, 7, inFlight);
			const bool awaiting = builtInFlight && inFlight &&
				AdvanceToAwaitingFence(*inFlight, { 33 });
			std::weak_ptr<NapaVoxelInitialPublicationOwner> weakOwner = inFlight;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> durableCompletionOwner = inFlight;
			if (!durableCompletionOwner)
			{
				context.Check(false,
					"Recording-after cancellation keeps a durable owner through Copy Fence retirement");
				context.Check(false,
					"Cancelled publication resources retire after the durable completion owner releases");
				return;
			}
			inFlight->Cancel();
			inFlight.reset();
			const bool completionAccepted = durableCompletionOwner->CompleteUpload(
				MakeCompletion(*durableCompletionOwner, AssetUploadStatus::Succeeded));
			context.Check(awaiting && completionAccepted && !weakOwner.expired() &&
				durableCompletionOwner->GetStatus() ==
				NapaVoxelInitialPublicationStatus::Cancelled &&
				durableCompletionOwner->HasCompletion(),
				"Recording-after cancellation keeps a durable owner through Copy Fence retirement");
			durableCompletionOwner.reset();
			context.Check(weakOwner.expired(),
				"Cancelled publication resources retire after the durable completion owner releases");
		}

		void RunInitialPublicationIdentityTests(SelfTestContext& context) noexcept
		{
			std::shared_ptr<NapaVoxelInitialPublicationOwner> first;
			std::shared_ptr<NapaVoxelInitialPublicationOwner> second;
			const bool built = BuildPublicationOwner(false, 2001, 1, first) &&
				BuildPublicationOwner(false, 2002, 1, second) &&
				first && second &&
				AdvanceToAwaitingFence(*first, { 41 }) &&
				AdvanceToAwaitingFence(*second, { 42 });
			const RHIFencePoint sharedBatchFence{ RHIFenceHandle{ 2, 1 }, 17 };
			const bool completed = built && first->CompleteUpload(
				MakeCompletion(*first, AssetUploadStatus::Succeeded, sharedBatchFence)) &&
				second->CompleteUpload(
					MakeCompletion(*second, AssetUploadStatus::Succeeded, sharedBatchFence));
			context.Check(completed && first->GetUploadIdentity() != second->GetUploadIdentity() &&
				first->GetUploadHandle() != second->GetUploadHandle() &&
				first->GetCompletionFence() == second->GetCompletionFence() &&
				first->IsReadyForCommit() && second->IsReadyForCommit(),
				"Scheduler co-batching preserves each voxel publication handle and identity");

			std::shared_ptr<NapaVoxelInitialPublicationOwner> exhausted;
			const bool builtExhausted = BuildPublicationOwner(false, 2003,
				std::numeric_limits<uint64_t>::max(), exhausted);
			const AssetStreamingIdentity oldIdentity = exhausted
				? exhausted->GetUploadIdentity()
				: AssetStreamingIdentity{};
			if (exhausted)
			{
				exhausted->Cancel();
			}
			context.Check(builtExhausted && exhausted->IsOwnerGenerationExhausted() &&
				exhausted->GetOwnerGeneration() == std::numeric_limits<uint64_t>::max() &&
				!exhausted->BeginRecording(oldIdentity),
				"Owner generation exhaustion invalidates the old token without wrapping");
		}

		void RunPublicationSessionCancellationIntegrationTest(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferContextView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});

			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			NapaVoxelCommandQueue commandQueue;
			NapaVoxelPublicationSession session(&device, &scheduler, &commandQueue);
			const bool began = BuildPublicationPending(true, pending) &&
				session.BeginPrepare(pending, 3001, 1);
			scheduler.DrainReadyWork();
			const AssetUploadStatistics submitted = scheduler.GetStatistics();
			context.Check(began && !pending &&
				session.GetPublicationStatus() ==
				NapaVoxelInitialPublicationStatus::AwaitingFence &&
				transferContextView->GetSubmissionCount() == 1 &&
				transferContextView->GetUploadCount() == 2 &&
				submitted.m_PendingCount == 1 && submitted.m_SubmittedCount == 1 &&
				device.GetCreatedBufferCount() == 2 &&
				device.GetDestroyedBufferCount() == 0,
				"Session preparation submits one durable Scheduler TransferBatch");

			session.CancelPrepare();
			context.Check(!session.IsReady() && !session.HasVisibleMeshes() &&
				!session.GetFrameView() && device.GetDestroyedBufferCount() == 0,
				"Lab switch cancellation releases Session state without retiring in-flight buffers");

			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			const AssetUploadStatistics completed = scheduler.GetStatistics();
			context.Check(completed.m_PendingCount == 0 && completed.m_SucceededCount == 1 &&
				device.GetDestroyedBufferCount() == device.GetCreatedBufferCount() &&
				!session.IsReady() && !session.HasVisibleMeshes() && !session.GetFrameView(),
				"Post-cancel Copy Fence completion retires buffers without publication or a dangling frame view");
			scheduler.Finalize();
		}

		void RunAssetUploadSchedulerWorkerHandoffTest(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			const std::thread::id ownerThreadId = std::this_thread::get_id();
			std::thread::id cpuCallbackThreadId;
			std::thread::id uploadCallbackThreadId;
			bool workerSawOwner = true;
			bool cpuCallbackRan = false;
			bool uploadCallbackRan = false;

			std::thread worker([&]()
				{
					workerSawOwner = scheduler.IsOwnerThread();
					scheduler.EnqueueCpuPayload({
						.m_Name = "Worker immutable payload handoff",
						.m_Identity = {
							.m_Kind = AssetStreamingWorkKind::Texture,
							.m_StableId = 9001,
							.m_Generation = 1,
							},
						.m_Estimate = {.m_SourceBytes = 16 },
						},
						[&]()
						{
							cpuCallbackRan = true;
							cpuCallbackThreadId = std::this_thread::get_id();
							scheduler.EnqueueUploadRecording({
								.m_Name = "Owner upload promotion",
								.m_Identity = {
									.m_Kind = AssetStreamingWorkKind::Texture,
									.m_StableId = 9001,
									.m_Generation = 1,
									},
								},
								[&]()
								{
									uploadCallbackRan = true;
									uploadCallbackThreadId = std::this_thread::get_id();
								});
						});
				});
			worker.join();

			context.Check(!workerSawOwner && !cpuCallbackRan && !uploadCallbackRan,
				"Worker enqueue hands off immutable payload without executing owner work inline");
			scheduler.DrainReadyWork();
			const AssetUploadStatistics statistics = scheduler.GetStatistics();
			context.Check(cpuCallbackRan && uploadCallbackRan &&
				cpuCallbackThreadId == ownerThreadId && uploadCallbackThreadId == ownerThreadId &&
				statistics.m_CpuPayloadQueue.m_EnqueuedCount == 1 &&
				statistics.m_CpuPayloadQueue.m_ProcessedCount == 1 &&
				statistics.m_UploadRecordingQueue.m_EnqueuedCount == 1 &&
				statistics.m_UploadRecordingQueue.m_ProcessedCount == 1,
				"Scheduler drains worker handoff and every publication/upload callback on its owner thread");
			scheduler.Finalize();
		}

		void RunInteractivePublicationIntegrationTest(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			NapaVoxelCommandQueue commandQueue;
			NapaVoxelPublicationSession session(&device, &scheduler, &commandQueue);
			std::unique_ptr<VoxelWorld> world;
			std::unique_ptr<PendingCpuMeshBatch> initialPending;
			const bool began = BuildInteractivePublicationInput(world, initialPending) &&
				session.BeginPrepare(initialPending, 4001, 1);
			scheduler.DrainReadyWork();
			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			session.TickPrepare();
			session.OnFrameSubmitted({ RHIFenceHandle{ 14, 1 }, 1 });
			const auto initialGpu = session.GetFrameView();

			const SphereEditRequest edit{
				.m_Brush = {
					.m_CenterWorld = { 3.0, 4.0, 4.0 },
					.m_Radius = 0.5,
					.m_Strength = 1.0,
				},
			};
			VoxelMutationResult damageMutation{};
			auto damageSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			const bool damaged = began && session.IsReady() && world &&
				ApplySphereEdit(*world, edit, damageMutation).Succeeded() &&
				damageMutation.GetChangeKind() == VoxelMutationChangeKind::DamageOnly &&
				BuildVoxelDamageMarkerSnapshot(*world,
					damageMutation.m_TargetWorldVoxelRevision, *damageSnapshot).Succeeded() &&
				session.PublishDataOnly(*world, damageMutation, 1, 1, damageSnapshot);
			const auto damagedGpu = session.GetFrameView();
			bool preservedBuffers = initialGpu && damagedGpu &&
				initialGpu->GetChunks().size() == damagedGpu->GetChunks().size();
			if (preservedBuffers)
			{
				for (size_t index = 0; index < initialGpu->GetChunks().size(); ++index)
				{
					preservedBuffers &= initialGpu->GetChunks()[index] ==
						damagedGpu->GetChunks()[index];
				}
			}
			context.Check(damaged && !damageSnapshot && preservedBuffers &&
				session.GetVisibleWorldRevision() == damageMutation.m_TargetWorldVoxelRevision &&
				session.GetLastPublicationSerial() == 1,
				"Interactive damage-only flow advances CPU/GPU/debug revision without replacing geometry");

			VoxelMutationResult surfaceMutation{};
			CpuMeshBatch replacementBatch{};
			std::unique_ptr<PendingCpuMeshBatch> replacementPending;
			auto surfaceSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			const bool surfaceApplied = damaged &&
				ApplySphereEdit(*world, edit, surfaceMutation).Succeeded() &&
				surfaceMutation.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged;
			const ValidationResult buildResult = surfaceApplied
				? BuildCpuMeshBatch(*world, surfaceMutation, replacementBatch)
				: ValidationResult{ ValidationError::InvalidVoxelMutation };
			const bool batchBuilt = buildResult.Succeeded();
			const bool batchValidated = batchBuilt &&
				ValidateCpuMeshBatch(replacementBatch, session.GetVisibleCoreMeshes(),
					replacementPending).Succeeded() && replacementPending;
			const bool snapshotBuilt = batchValidated && BuildVoxelDamageMarkerSnapshot(*world,
				surfaceMutation.m_TargetWorldVoxelRevision, *surfaceSnapshot).Succeeded();
			const bool replacementBegan = snapshotBuilt && session.BeginMeshPrepare(
				replacementPending, 2, 1, surfaceSnapshot);
			context.Check(surfaceApplied,
				"Interactive second Stone hit produces a surface mutation");
			context.Check(batchBuilt,
				"Interactive surface mutation builds its exact replacement batch");
			context.Check(batchValidated,
				"Interactive surface mutation validates its exact replacement batch");
			context.Check(snapshotBuilt,
				"Interactive surface mutation prepares its revision-bound damage snapshot");
			context.Check(replacementBegan,
				"Interactive surface mutation begins one replacement publication");
			scheduler.DrainReadyWork();
			GGLAB_UNUSED(scheduler.Tick());
			const bool published = replacementBegan && session.TickMeshPrepare();
			context.Check(published && !replacementPending && !surfaceSnapshot &&
				!session.HasActiveMeshPrepare() && !session.HasFailed() &&
				session.GetVisibleWorldRevision() == surfaceMutation.m_TargetWorldVoxelRevision &&
				session.GetLastPublicationSerial() == 2 &&
				session.GetFrameView() != damagedGpu,
				"Interactive surface flow publishes one proof-bound replacement after Copy Fence completion");
			scheduler.Finalize();
		}

		void RunReplacementBatchContractTests(SelfTestContext& context) noexcept
		{
			NapaVoxelPublicationTestDevice device;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			std::vector<napa::voxel::ChunkCoord> expectedChunks;
			const bool fixtureBuilt = BuildReplacementPublicationInput(
				device, 10.0, renderState, pending, expectedChunks);
			const std::shared_ptr<const NapaVoxelGpuMeshSet> baseMeshes =
				renderState.GetVisibleGpuMeshes();
			std::shared_ptr<GGLabMeshPublicationBatch> publication;
			const bool prepared = fixtureBuilt && PrepareGGLabMeshPublicationBatch(
				pending, baseMeshes, {
					.m_OperationSerial = 11,
					.m_PublicationSerial = 1,
					.m_OwnerGeneration = 3,
				}, 8001, publication) && publication &&
				publication->PrepareGpuResources(&device);

			bool exactReplacementSet = prepared && !pending &&
				publication->GetReplacements().size() == expectedChunks.size();
			bool hasDelete = false;
			if (exactReplacementSet)
			{
				for (size_t index = 0; index < expectedChunks.size(); ++index)
				{
					exactReplacementSet &=
						publication->GetReplacements()[index].m_Chunk == expectedChunks[index];
					hasDelete |= publication->GetReplacements()[index].IsDelete();
				}
			}
			context.Check(exactReplacementSet && hasDelete &&
				publication->GetIdentity() == GGLabMeshPublicationIdentity{ 11, 1, 3 } &&
				publication->GetUploadIdentity().m_Kind ==
				AssetStreamingWorkKind::RuntimeMesh,
				"Replacement conversion consumes the exact Core replacement view and preserves identity");

			const std::shared_ptr<const NapaVoxelGpuMeshSet> prospective = prepared
				? publication->GetProspectiveGpuMeshes()
				: nullptr;
			bool reusedUntouchedChunk = false;
			if (baseMeshes && prospective)
			{
				for (const auto& base : baseMeshes->GetChunks())
				{
					for (const auto& candidate : prospective->GetChunks())
					{
						if (base && candidate && base->m_Chunk == napa::voxel::ChunkCoord{ 2, 0, 0 } &&
							candidate->m_Chunk == base->m_Chunk && candidate == base)
						{
							reusedUntouchedChunk = true;
						}
					}
				}
			}
			context.Check(prospective && prospective->GetVisibleWorldRevision() == 2 &&
				reusedUntouchedChunk && renderState.GetVisibleWorldRevision() == 1 &&
				renderState.GetVisibleGpuMeshes() == baseMeshes,
				"Prospective replacement state reuses untouched buffers without changing Visible state");

			NapaVoxelRenderState exhaustedState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> exhaustedPending;
			std::vector<napa::voxel::ChunkCoord> exhaustedChunks;
			std::shared_ptr<GGLabMeshPublicationBatch> exhausted;
			const bool exhaustedPrepared = BuildReplacementPublicationInput(
				device, 0.1, exhaustedState, exhaustedPending, exhaustedChunks) &&
				PrepareGGLabMeshPublicationBatch(exhaustedPending,
					exhaustedState.GetVisibleGpuMeshes(), {
						.m_OperationSerial = 12,
						.m_PublicationSerial = 2,
						.m_OwnerGeneration = std::numeric_limits<uint64_t>::max(),
					}, 8002, exhausted);
			const AssetStreamingIdentity exhaustedIdentity = exhaustedPrepared
				? exhausted->GetUploadIdentity()
				: AssetStreamingIdentity{};
			if (exhausted)
			{
				exhausted->Cancel();
			}
			context.Check(exhaustedPrepared && exhausted->IsOwnerGenerationExhausted() &&
				exhausted->GetIdentity().m_OwnerGeneration ==
				std::numeric_limits<uint64_t>::max() &&
				!exhausted->BeginRecording(exhaustedIdentity),
				"Replacement owner-generation cancellation cannot wrap or revive stale work");
		}

		void RunReplacementUploadCompletionTests(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			NapaVoxelCommandQueue commandQueue;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			std::vector<napa::voxel::ChunkCoord> replacementChunks;
			const bool fixtureBuilt = BuildReplacementPublicationInput(
				device, 0.1, renderState, pending, replacementChunks);
			const auto visibleBefore = renderState.GetVisibleGpuMeshes();
			NapaVoxelMeshReplacementUploadSession session(
				&device, &scheduler, &commandQueue);
			const bool began = fixtureBuilt && session.BeginPrepare(
				pending, visibleBefore, 21, 1);
			const auto publication = session.GetPublication();
			scheduler.DrainReadyWork();
			const AssetUploadStatistics submitted = scheduler.GetStatistics();
			context.Check(began && !pending && publication &&
				publication->GetStatus() == GGLabMeshPublicationStatus::AwaitingFence &&
				submitted.m_UploadRecordingQueue.m_EnqueuedCount == 1 &&
				submitted.m_SubmittedCount == 1 && submitted.m_BatchSubmissionCount == 1 &&
				transferView->GetSubmissionCount() == 1 &&
				transferView->GetUploadCount() ==
				publication->GetUploadEstimate().m_OperationCount,
				"One replacement publication records one Scheduler work item, handle, and fence batch");

			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			context.Check(publication->IsReadyForCommit() && !session.IsReady() &&
				renderState.GetVisibleWorldRevision() == 1 &&
				renderState.GetVisibleGpuMeshes() == visibleBefore,
				"Copy completion marks only the durable batch ready before the next owner Update");
			session.TickPrepare();
			context.Check(session.IsReady() && !session.HasFailed() &&
				!commandQueue.IsTerminal() && renderState.GetVisibleWorldRevision() == 1 &&
				renderState.GetVisibleGpuMeshes() == visibleBefore,
				"Owner Update observes upload readiness without publishing replacement state early");
			session.CancelPrepare();
			scheduler.Finalize();
		}

		void RunEmptyReplacementUploadTest(SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			NapaVoxelCommandQueue commandQueue;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
			std::vector<napa::voxel::ChunkCoord> replacementChunks;
			const bool fixtureBuilt = BuildReplacementPublicationInput(
				device, 10.0, renderState, pending, replacementChunks);
			NapaVoxelMeshReplacementUploadSession session(
				&device, &scheduler, &commandQueue);
			const bool began = fixtureBuilt && session.BeginPrepare(
				pending, renderState.GetVisibleGpuMeshes(), 22, 1);
			const auto publication = session.GetPublication();
			bool allDeletes = began && publication && !publication->GetReplacements().empty();
			if (allDeletes)
			{
				for (const NapaVoxelGpuChunkReplacement& replacement :
					publication->GetReplacements())
				{
					allDeletes &= replacement.IsDelete();
				}
			}
			scheduler.DrainReadyWork();
			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			session.TickPrepare();
			context.Check(allDeletes && publication->GetUploadEstimate().m_OperationCount == 0 &&
				transferView->GetUploadCount() == 0 && transferView->GetSubmissionCount() == 1 &&
				scheduler.GetStatistics().m_SubmittedCount == 1 && session.IsReady() &&
				renderState.GetVisibleWorldRevision() == 1,
				"An Empty/Delete replacement still receives one durable Scheduler handle and fence without buffer uploads");
			session.CancelPrepare();
			scheduler.Finalize();
		}

		void RunReplacementUploadFailureTests(SelfTestContext& context) noexcept
		{
			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue, {
					.m_LastPublicationSerial = std::numeric_limits<uint64_t>::max(),
					});
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, renderState.GetVisibleGpuMeshes(), 31, 1);
				const AssetUploadStatistics statistics = scheduler.GetStatistics();
				context.Check(!began && pending && !session.GetPublication() && session.HasFailed() &&
					session.GetLastPublicationSerial() == std::numeric_limits<uint64_t>::max() &&
					commandQueue.GetTerminalError() ==
					NapaVoxelCommandQueueError::PublicationSerialExhausted &&
					statistics.m_UploadRecordingQueue.m_EnqueuedCount == 0 &&
					statistics.m_SubmittedCount == 0 && transferView->GetUploadCount() == 0 &&
					transferView->GetSubmissionCount() == 0,
					"Publication serial exhaustion freezes FIFO without consuming pending data or scheduling upload work");
				scheduler.Finalize();
			}

			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				const auto visibleBefore = renderState.GetVisibleGpuMeshes();
				device.FailNextBufferCreation();
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, visibleBefore, 32, 1);
				context.Check(!began && !pending && session.HasFailed() &&
					commandQueue.GetTerminalError() ==
					NapaVoxelCommandQueueError::HostPreparationFailed &&
					renderState.GetVisibleWorldRevision() == 1 &&
					renderState.GetVisibleGpuMeshes() == visibleBefore &&
					scheduler.GetStatistics().m_UploadRecordingQueue.m_EnqueuedCount == 0,
					"GPU resource creation failure freezes FIFO and preserves the complete old Visible state");
				scheduler.Finalize();
			}

			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
				transferView->SetFailUploads(true);
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				const auto visibleBefore = renderState.GetVisibleGpuMeshes();
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, visibleBefore, 33, 1);
				scheduler.DrainReadyWork();
				device.CompleteFence();
				GGLAB_UNUSED(scheduler.Tick());
				session.TickPrepare();
				context.Check(began && session.HasFailed() && !session.IsReady() &&
					commandQueue.GetTerminalError() ==
					NapaVoxelCommandQueueError::HostPreparationFailed &&
					renderState.GetVisibleWorldRevision() == 1 &&
					renderState.GetVisibleGpuMeshes() == visibleBefore,
					"Upload failure is observed on owner Update and freezes FIFO without partial publication");
				session.CancelPrepare();
				scheduler.Finalize();
			}
		}

		void RunReplacementUploadCancellationTests(SelfTestContext& context) noexcept
		{
			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, renderState.GetVisibleGpuMeshes(), 41, 1);
				std::weak_ptr<const NapaVoxelGpuMeshSet> prospective = began
					? session.GetPublication()->GetProspectiveGpuMeshes()
					: std::shared_ptr<const NapaVoxelGpuMeshSet>{};
				const uint32_t destroyedBefore = device.GetDestroyedBufferCount();
				session.CancelPrepare();
				context.Check(began && prospective.expired() &&
					device.GetDestroyedBufferCount() > destroyedBefore &&
					transferView->GetUploadCount() == 0 &&
					transferView->GetSubmissionCount() == 0 &&
					scheduler.GetStatistics().m_UploadRecordingQueue.m_CancelledCount == 1 &&
					!commandQueue.IsTerminal(),
					"Recording-before cancellation removes ready work and retires unsubmitted replacement buffers");
				scheduler.Finalize();
			}

			{
				auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
				TransferManager transferManager(std::move(transferContext));
				NapaVoxelPublicationTestDevice device;
				AssetUploadScheduler scheduler({
					.m_Device = &device,
					.m_TransferManager = &transferManager,
					});
				NapaVoxelCommandQueue commandQueue;
				NapaVoxelRenderState renderState{};
				std::unique_ptr<napa::voxel::PendingCpuMeshBatch> pending;
				std::vector<napa::voxel::ChunkCoord> replacementChunks;
				const bool fixtureBuilt = BuildReplacementPublicationInput(
					device, 0.1, renderState, pending, replacementChunks);
				const auto visibleBefore = renderState.GetVisibleGpuMeshes();
				NapaVoxelMeshReplacementUploadSession session(&device, &scheduler, &commandQueue);
				const bool began = fixtureBuilt && session.BeginPrepare(
					pending, visibleBefore, 42, 7);
				std::weak_ptr<const NapaVoxelGpuMeshSet> prospective = began
					? session.GetPublication()->GetProspectiveGpuMeshes()
					: std::shared_ptr<const NapaVoxelGpuMeshSet>{};
				scheduler.DrainReadyWork();
				const uint32_t destroyedBeforeCancel = device.GetDestroyedBufferCount();
				session.CancelPrepare();
				const bool retainedThroughFence = !prospective.expired() &&
					device.GetDestroyedBufferCount() == destroyedBeforeCancel;
				device.CompleteFence();
				GGLAB_UNUSED(scheduler.Tick());
				context.Check(began && retainedThroughFence && prospective.expired() &&
					device.GetDestroyedBufferCount() > destroyedBeforeCancel &&
					renderState.GetVisibleWorldRevision() == 1 &&
					renderState.GetVisibleGpuMeshes() == visibleBefore &&
					!session.IsReady() && !commandQueue.IsTerminal(),
					"Recording-after cancellation retains buffers through Copy Fence and never publishes a frame view");
				scheduler.Finalize();
			}
		}

		void RunDataOnlyPublicationContractTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			std::unique_ptr<VoxelWorld> world;
			VisibleMeshSet visible{};
			VoxelMutationResult mutation{};
			if (!BuildCoreDataOnlyPublicationInput(world, visible, mutation))
			{
				context.Check(false, "Data-only publication fixture builds a Damage-only mutation");
				return;
			}

			const auto chunksBefore = visible.GetChunks();
			const ChunkMeshRecord* chunkStorageBefore = chunksBefore.data();
			const WorldMeshValidationResult validationBefore = visible.GetWorldMeshValidation();
			const BoundaryContourValidationResult boundaryBefore =
				visible.GetBoundaryValidation();

			std::unique_ptr<PendingDataOnlyPublication> pending;
			const ValidationResult prepared = PrepareDataOnlyPublication(
				*world, mutation, visible, pending);
			PendingDataOnlyPublication* preservedPending = pending.get();
			VoxelMutationResult invalidSurface = mutation;
			const std::uint8_t originalDensity =
				invalidSurface.m_SampleChanges.front().m_After.m_Density;
			invalidSurface.m_SampleChanges.front().m_After.m_Density =
				originalDensity == std::numeric_limits<std::uint8_t>::max()
				? static_cast<std::uint8_t>(originalDensity - 1)
				: static_cast<std::uint8_t>(originalDensity + 1);
			const ValidationResult surfaceRejected = PrepareDataOnlyPublication(
				*world, invalidSurface, visible, pending);
			VoxelMutationResult invalidDirty = mutation;
			invalidDirty.m_DataDirtyChunks.clear();
			const ValidationResult dirtyRejected = PrepareDataOnlyPublication(
				*world, invalidDirty, visible, pending);
			VoxelMutationResult invalidBase = mutation;
			invalidBase.m_BaseWorldVoxelRevision =
				invalidBase.m_TargetWorldVoxelRevision;
			const ValidationResult baseRejected = PrepareDataOnlyPublication(
				*world, invalidBase, visible, pending);
			VoxelMutationResult invalidTarget = mutation;
			++invalidTarget.m_TargetWorldVoxelRevision;
			const ValidationResult targetRejected = PrepareDataOnlyPublication(
				*world, invalidTarget, visible, pending);
			VoxelMutationResult invalidAfter = mutation;
			invalidAfter.m_SampleChanges.front().m_After.m_Damage =
				static_cast<std::uint8_t>(
					invalidAfter.m_SampleChanges.front().m_After.m_Damage - 1);
			const ValidationResult afterRejected = PrepareDataOnlyPublication(
				*world, invalidAfter, visible, pending);
			VoxelMutationResult invalidMeshDirty = mutation;
			invalidMeshDirty.m_MeshDirtyChunks.push_back({});
			const ValidationResult meshDirtyRejected = PrepareDataOnlyPublication(
				*world, invalidMeshDirty, visible, pending);
			VoxelMutationResult emptyMutation{
				.m_BaseWorldVoxelRevision = mutation.m_BaseWorldVoxelRevision,
				.m_TargetWorldVoxelRevision = mutation.m_TargetWorldVoxelRevision,
			};
			const ValidationResult emptyRejected = PrepareDataOnlyPublication(
				*world, emptyMutation, visible, pending);
			context.Check(prepared.Succeeded() && pending && preservedPending == pending.get() &&
				surfaceRejected.m_Error == ValidationError::InvalidDataOnlyPublication &&
				dirtyRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				baseRejected.m_Error == ValidationError::StaleDataOnlyPublication &&
				targetRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				afterRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				meshDirtyRejected.m_Error == ValidationError::MismatchedDataOnlyPublication &&
				emptyRejected.m_Error == ValidationError::InvalidDataOnlyPublication,
				"Data-only preparation rejects Surface, Dirty, Base, Target, and "
				"authoritative-sample mismatches without replacing a valid token");

			CommitDataOnlyPublication(pending, visible);
			context.Check(!pending && visible.GetVisibleWorldRevision() ==
				mutation.m_TargetWorldVoxelRevision &&
				visible.GetChunks().data() == chunkStorageBefore &&
				visible.GetChunks().size() == chunksBefore.size() &&
				visible.GetWorldMeshValidation() == validationBefore &&
				visible.GetBoundaryValidation() == boundaryBefore,
				"Data-only commit advances only the visible revision and preserves immutable mesh evidence");

			std::unique_ptr<VoxelWorld> maximumWorld;
			VisibleMeshSet maximumVisible{};
			VoxelMutationResult maximumMutation{};
			const bool maximumFixture = BuildCoreDataOnlyPublicationInput(
				maximumWorld, maximumVisible, maximumMutation);
			maximumMutation.m_TargetWorldVoxelRevision =
				std::numeric_limits<std::uint64_t>::max();
			std::unique_ptr<PendingDataOnlyPublication> maximumPending;
			const ValidationResult maximumPrepared = maximumFixture
				? testing::DataOnlyPublicationTestAccess::PrepareWithAuthoritativeRevision(
					*maximumWorld, maximumMutation, maximumVisible,
					std::numeric_limits<std::uint64_t>::max(), maximumPending)
				: ValidationResult{ ValidationError::InvalidDataOnlyPublication };
			if (maximumPrepared.Succeeded())
			{
				CommitDataOnlyPublication(maximumPending, maximumVisible);
			}
			context.Check(maximumPrepared.Succeeded() && !maximumPending &&
				maximumVisible.GetVisibleWorldRevision() ==
				std::numeric_limits<std::uint64_t>::max(),
				"Data-only publication accepts a maximum Target revision without requiring another increment");

			std::unique_ptr<VoxelWorld> allocationWorld;
			VisibleMeshSet allocationVisible{};
			VoxelMutationResult allocationMutation{};
			const bool allocationFixture = BuildCoreDataOnlyPublicationInput(
				allocationWorld, allocationVisible, allocationMutation);
			std::unique_ptr<PendingDataOnlyPublication> allocationPending;
			const ValidationResult allocationFailure = allocationFixture
				? testing::DataOnlyPublicationTestAccess::PrepareWithAllocationFailure(
					*allocationWorld, allocationMutation, allocationVisible,
					allocationPending)
				: ValidationResult{ ValidationError::InvalidDataOnlyPublication };
			context.Check(allocationFailure.m_Error ==
				ValidationError::DataOnlyPublicationAllocationFailure && !allocationPending &&
				allocationVisible.GetVisibleWorldRevision() ==
				allocationMutation.m_BaseWorldVoxelRevision,
				"Data-only token allocation failure preserves the complete visible state and output");

			std::unique_ptr<VoxelWorld> forgedWorld;
			VisibleMeshSet forgedVisible{};
			VoxelMutationResult incompleteMutation{};
			const bool forgedFixture = BuildCoreDataOnlyPublicationInput(
				forgedWorld, forgedVisible, incompleteMutation);
			bool surfaceChanged = false;
			const ValidationResult surfaceWrite = forgedFixture
				? forgedWorld->WriteCurrentSample({ 0, 0, 0 }, {
						.m_Density = IsoValue,
						.m_Material = VoxelMaterial::Stone,
						.m_Damage = 0,
					}, surfaceChanged)
					: ValidationResult{ ValidationError::InvalidDataOnlyPublication };
			if (surfaceWrite.Succeeded())
			{
				incompleteMutation.m_TargetWorldVoxelRevision =
					forgedWorld->GetWorldVoxelRevision();
			}
			std::unique_ptr<PendingDataOnlyPublication> forgedPending;
			const ValidationResult forgedResult = surfaceWrite.Succeeded()
				? PrepareDataOnlyPublication(
					*forgedWorld, incompleteMutation, forgedVisible, forgedPending)
				: surfaceWrite;
			context.Check(surfaceChanged && forgedResult.m_Error ==
				ValidationError::MismatchedDataOnlyPublication && !forgedPending &&
				forgedWorld->GetSurfaceStateRevision() !=
				forgedVisible.GetSurfaceStateRevision(),
				"Data-only preparation rejects an incomplete Mutation that hides a newer Surface state");
		}

		void RunAtomicDataOnlyPublicationTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			NapaVoxelPublicationTestDevice device;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<VoxelWorld> world;
			VoxelMutationResult mutation{};
			if (!BuildDamageOnlyPublicationInput(device, renderState, world, mutation))
			{
				context.Check(false, "Atomic data-only publication fixture initializes Visible state");
				return;
			}

			const auto oldFrameView = renderState.CaptureFrameView();
			std::vector<std::shared_ptr<const NapaVoxelGpuChunkMesh>> oldChunks =
				oldFrameView->GetChunks();
			auto damageSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			const bool snapshotBuilt = BuildVoxelDamageMarkerSnapshot(
				*world, mutation.m_TargetWorldVoxelRevision, *damageSnapshot).Succeeded();
			renderState.OnFrameSubmitted({ RHIFenceHandle{ 10, 1 }, 5 });
			std::unique_ptr<NapaVoxelPreparedDataOnlyCommit> preparedCommit;
			const uint32_t createdBeforePrepare = device.GetCreatedBufferCount();
			const bool prepared = snapshotBuilt && renderState.PrepareDataOnlyCommit(
				*world, mutation, { 1, 1, 1 }, damageSnapshot, preparedCommit);
			const uint32_t createdBeforeCommit = device.GetCreatedBufferCount();
			if (prepared)
			{
				renderState.CommitDataOnly(preparedCommit);
			}

			bool reusedEveryBuffer = renderState.GetVisibleGpuMeshes() &&
				renderState.GetVisibleGpuMeshes()->GetChunks().size() == oldChunks.size();
			if (reusedEveryBuffer)
			{
				for (size_t index = 0; index < oldChunks.size(); ++index)
				{
					reusedEveryBuffer &=
						renderState.GetVisibleGpuMeshes()->GetChunks()[index] == oldChunks[index];
				}
			}
			const VoxelDamageMarkerSnapshot* visibleDamage =
				renderState.GetVisibleDamageSnapshot();
			context.Check(prepared && !preparedCommit && !damageSnapshot &&
				renderState.GetVisibleWorldRevision() == mutation.m_TargetWorldVoxelRevision &&
				renderState.GetVisibleGpuMeshes()->GetVisibleWorldRevision() ==
				mutation.m_TargetWorldVoxelRevision && visibleDamage &&
				visibleDamage->m_SourceWorldVoxelRevision == mutation.m_TargetWorldVoxelRevision &&
				reusedEveryBuffer && createdBeforePrepare == createdBeforeCommit &&
				createdBeforeCommit == device.GetCreatedBufferCount() &&
				oldFrameView->GetVisibleWorldRevision() == mutation.m_BaseWorldVoxelRevision &&
				renderState.GetRetiredGpuMeshSetCount() == 1,
				"One no-fail owner commit publishes Damage-only Core, GPU, and debug revisions "
				"while preserving buffers and the old frame view");

			auto staleSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			GGLAB_UNUSED(BuildVoxelDamageMarkerSnapshot(
				*world, mutation.m_TargetWorldVoxelRevision, *staleSnapshot));
			std::unique_ptr<NapaVoxelPreparedDataOnlyCommit> staleCommit;
			const bool staleRejected = !renderState.PrepareDataOnlyCommit(
				*world, mutation, { 2, 2, 1 }, staleSnapshot, staleCommit);
			const bool ownerRejected = !renderState.PrepareDataOnlyCommit(
				*world, mutation, { 2, 2, 2 }, staleSnapshot, staleCommit);
			context.Check(staleRejected && ownerRejected && staleSnapshot && !staleCommit &&
				renderState.GetLastCommittedIdentity() ==
				GGLabMeshPublicationIdentity{ 1, 1, 1 },
				"Same-revision and mismatched-owner data-only work cannot overwrite newer Visible state");

			renderState.RetireCompletedGpuMeshes(&device);
			const bool retainedBeforeFence = renderState.GetRetiredGpuMeshSetCount() == 1;
			device.CompleteFence();
			renderState.RetireCompletedGpuMeshes(&device);
			context.Check(retainedBeforeFence && renderState.GetRetiredGpuMeshSetCount() == 0 &&
				oldFrameView->GetVisibleWorldRevision() == mutation.m_BaseWorldVoxelRevision,
				"Old GPU draw sets retire only after their Graphics Fence while captured frame views remain immutable");
		}

		void RunAtomicMeshPublicationTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			NapaVoxelPublicationTestDevice device;
			NapaVoxelRenderState renderState{};
			std::unique_ptr<PendingCpuMeshBatch> pending;
			std::vector<ChunkCoord> replacementChunks;
			if (!BuildReplacementPublicationInput(
				device, 0.1, renderState, pending, replacementChunks))
			{
				context.Check(false, "Atomic mesh publication fixture initializes Visible state");
				return;
			}

			const GGLabMeshPublicationIdentity identity{ 3, 4, 1 };
			std::shared_ptr<GGLabMeshPublicationBatch> publication;
			const bool uploaded = PrepareGGLabMeshPublicationBatch(
				pending, renderState.GetVisibleGpuMeshes(), identity, 9101, publication) &&
				publication->PrepareGpuResources(&device) && publication->MarkQueued() &&
				publication->BeginRecording(publication->GetUploadIdentity()) &&
				publication->SetUploadHandle({ 911 }) && publication->CompleteUpload({
					.m_Handle = publication->GetUploadHandle(),
					.m_Identity = publication->GetUploadIdentity(),
					.m_Status = AssetUploadStatus::Succeeded,
					.m_FencePoint = { RHIFenceHandle{ 9, 1 }, 2 },
					});
			const auto oldFrameView = renderState.CaptureFrameView();
			const auto prospective = publication
				? publication->GetProspectiveGpuMeshes()
				: nullptr;
			auto damageSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			damageSnapshot->m_SourceWorldVoxelRevision = publication
				? publication->GetTargetWorldRevision()
				: 0;
			renderState.OnFrameSubmitted({ RHIFenceHandle{ 10, 1 }, 6 });
			std::unique_ptr<NapaVoxelPreparedMeshCommit> preparedCommit;
			const uint32_t createdBeforeCommit = device.GetCreatedBufferCount();
			const bool prepared = uploaded && renderState.PrepareMeshCommit(
				publication, identity, damageSnapshot, preparedCommit);
			if (prepared)
			{
				renderState.CommitMesh(preparedCommit);
			}
			const VoxelDamageMarkerSnapshot* visibleDamage =
				renderState.GetVisibleDamageSnapshot();
			context.Check(prepared && !preparedCommit && !damageSnapshot &&
				publication->GetStatus() == GGLabMeshPublicationStatus::Committed &&
				renderState.GetVisibleGpuMeshes() == prospective &&
				renderState.GetVisibleWorldRevision() == publication->GetTargetWorldRevision() &&
				prospective && prospective->GetVisibleWorldRevision() ==
				renderState.GetVisibleWorldRevision() && visibleDamage &&
				visibleDamage->m_SourceWorldVoxelRevision ==
				renderState.GetVisibleWorldRevision() &&
				renderState.GetLastCommittedIdentity() == identity &&
				device.GetCreatedBufferCount() == createdBeforeCommit &&
				oldFrameView->GetVisibleWorldRevision() == 1 &&
				renderState.GetRetiredGpuMeshSetCount() == 1,
				"One proof-carrying mesh token atomically publishes its exact Core, GPU, and "
				"debug batch without mixed chunks");

			auto repeatedSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			repeatedSnapshot->m_SourceWorldVoxelRevision = publication->GetTargetWorldRevision();
			std::unique_ptr<NapaVoxelPreparedMeshCommit> repeatedCommit;
			context.Check(!renderState.PrepareMeshCommit(
				publication, identity, repeatedSnapshot, repeatedCommit) &&
				repeatedSnapshot && !repeatedCommit &&
				renderState.GetVisibleGpuMeshes() == prospective,
				"A committed same-revision mesh publication cannot be prepared or applied twice");
		}

		void RunLabRuntimeGenerationCancellationStressTests(
			SelfTestContext& context) noexcept
		{
			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			TaskSystem taskSystem({ .m_WorkerCount = 1 });
			// LabSessionBase needs an Asset owner scope, but this fixture has no file assets.
			// Close background submission before AssetManager queues its optional test textures.
			taskSystem.Shutdown();
			SamplerRegistry samplerRegistry({ .m_Device = &device });
			AssetManager assetManager({
				.m_Device = &device,
				.m_TaskSystem = &taskSystem,
				.m_TransferManager = &transferManager,
				.m_AssetUploadScheduler = &scheduler,
				.m_SamplerRegistry = &samplerRegistry,
				});
			Renderer renderer;
			ShaderManager shaderManager(device.GetBackendType());
			InputManager inputManager;
			Time time;
			NapaVoxelLabSwitchTestState state{
				.m_Device = &device,
				.m_Scheduler = &scheduler,
			};
			s_NapaVoxelLabSwitchTestState = &state;

			const LabSessionCreateInfo createInfo{
				.m_Services = {
					.m_Renderer = &renderer,
					.m_AssetManager = &assetManager,
					.m_ShaderManager = &shaderManager,
					.m_TaskSystem = &taskSystem,
					.m_InputManager = &inputManager,
					.m_Time = &time,
					// The lifecycle-only sessions never issue DebugDraw or environment requests.
					.m_DebugDraw = reinterpret_cast<DebugDrawContext*>(&renderer),
					.m_EnvironmentAssetController =
						reinterpret_cast<EnvironmentAssetController*>(&renderer),
				},
				.m_WindowWidth = 64,
				.m_WindowHeight = 64,
				.m_RunConfig = {.m_WarmupFrames = 0 },
			};

			bool runtimeValid = false;
			constexpr uint32_t cancellationCount = 12;
			{
				LabRuntime runtime(createInfo);
				runtimeValid = runtime.RegisterLab(
					NapaVoxelLabSwitchControlSession::GetDescriptor(),
					&NapaVoxelLabSwitchControlSession::Create);
				runtimeValid = runtimeValid && runtime.RegisterLab(
					NapaVoxelLabSwitchPendingSession::GetDescriptor(),
					&NapaVoxelLabSwitchPendingSession::Create);
				runtimeValid = runtimeValid &&
					runtime.Initialize(NapaVoxelLabSwitchControlSession::GetId());
				runtime.TickTransitions();
				runtime.OnEnter();
				runtimeValid &= runtime.IsReady() && runtime.GetActiveSession() &&
					runtime.GetActiveSession()->GetDescriptor().m_Id ==
					NapaVoxelLabSwitchControlSession::GetId();

				for (uint32_t index = 0; index < cancellationCount && runtimeValid; ++index)
				{
					runtime.RequestSwitchLab(NapaVoxelLabSwitchPendingSession::GetId());
					runtime.ProcessPendingCommands();
					scheduler.DrainReadyWork();
					const AssetUploadStatistics submitted = scheduler.GetStatistics();
					runtimeValid &= runtime.HasPendingSession() &&
						submitted.m_PendingCount == index + 1 &&
						transferView->GetSubmissionCount() == index + 2;

					// This is the production Lab switch path: LabRuntime consumes the command,
					// cancels and destroys the pending session, and activates a fresh control Lab.
					runtime.RequestSwitchLab(NapaVoxelLabSwitchControlSession::GetId());
					runtime.ProcessPendingCommands();
					runtimeValid &= runtime.IsReady() && !runtime.HasPendingSession() &&
						runtime.GetActiveSession() &&
						runtime.GetActiveSession()->GetDescriptor().m_Id ==
						NapaVoxelLabSwitchControlSession::GetId();
				}

				const bool generationsAreExact =
					state.m_StartedGenerations.size() == cancellationCount &&
					std::ranges::equal(state.m_StartedGenerations,
						std::views::iota(uint64_t{ 1 }, uint64_t{ cancellationCount + 1 }));
				context.Check(runtimeValid && generationsAreExact &&
					state.m_CancelledSessionCount == cancellationCount &&
					state.m_DestroyedSessionCount == cancellationCount &&
					state.m_UnexpectedCommitCount == 0 &&
					state.m_CancelledOnlyAfterSubmission &&
					state.m_CancelledWithoutVisibleState,
					"LabRuntime switch stress cancels every submitted Session generation without publication");

				device.CompleteFence({
					RHIFenceHandle{ 1, 1 }, transferView->GetSubmissionCount(),
					});
				GGLAB_UNUSED(scheduler.Tick());
				context.Check(runtimeValid && IsSchedulerQuiescent(scheduler.GetStatistics()) &&
					device.GetLiveBufferCount() == 0 &&
					device.GetCreatedBufferCount() == device.GetDestroyedBufferCount() &&
					runtime.IsReady() && runtime.GetActiveSession() &&
					runtime.GetActiveSession()->GetDescriptor().m_Id ==
					NapaVoxelLabSwitchControlSession::GetId(),
					"Stale generation completions retire after the Copy Fence and cannot replace the active Lab");

				runtime.OnExit();
				runtime.Shutdown();
			}
			s_NapaVoxelLabSwitchTestState = nullptr;

			assetManager.BeginShutdown();
			taskSystem.Shutdown();
			taskSystem.PumpCompletions();
			assetManager.DrainLoadCompletions();
			scheduler.DrainReadyWork();
			device.CompleteFence();
			GGLAB_UNUSED(scheduler.Tick());
			scheduler.Finalize();
			assetManager.PrepareForShutdown({});
		}

		void RunPublicationLifecycleStressTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			auto transferContext = std::make_unique<NapaVoxelPublicationTestTransferContext>();
			NapaVoxelPublicationTestTransferContext* transferView = transferContext.get();
			TransferManager transferManager(std::move(transferContext));
			NapaVoxelPublicationTestDevice device;
			AssetUploadScheduler scheduler({
				.m_Device = &device,
				.m_TransferManager = &transferManager,
				});
			NapaVoxelCommandQueue commandQueue;
			NapaVoxelPublicationSession session(&device, &scheduler, &commandQueue);
			std::unique_ptr<VoxelWorld> world;
			std::unique_ptr<PendingCpuMeshBatch> initialPending;
			bool lifecycleValid = BuildInteractivePublicationInput(world, initialPending) &&
				session.BeginPrepare(initialPending, 10'001, 1);
			if (lifecycleValid)
			{
				scheduler.DrainReadyWork();
				device.CompleteFence({
					RHIFenceHandle{ 1, 1 }, transferView->GetSubmissionCount(),
					});
				GGLAB_UNUSED(scheduler.Tick());
				session.TickPrepare();
				lifecycleValid = session.IsReady() && session.GetFrameView() &&
					session.GetVisibleWorldRevision() == world->GetWorldVoxelRevision();
			}
			context.Check(lifecycleValid,
				"Lifecycle stress fixture publishes one initial CPU/GPU frame");

			constexpr RHIFenceHandle graphicsFence{ 14, 1 };
			uint64_t graphicsFenceValue = 100;
			uint64_t operationSerial = 0;
			std::shared_ptr<const NapaVoxelGpuMeshSet> currentFrame = session.GetFrameView();
			if (lifecycleValid)
			{
				session.OnFrameSubmitted({ graphicsFence, graphicsFenceValue });
				session.OnFrameSubmitted({ graphicsFence, graphicsFenceValue - 1 });
			}

			const SphereEditRequest edit{
				.m_Brush = {
					.m_CenterWorld = { 3.0, 4.0, 4.0 },
					.m_Radius = 0.5,
					.m_Strength = 1.0,
					},
				.m_MaterialRules = {
					.m_DamagePerHit = 255,
					.m_StoneBreakThreshold = 255,
					},
			};
			bool heldOldFrameUntilCopy = true;
			bool retiredOnlyAtGraphicsFence = true;
			bool quiescentBetweenPublications = true;
			constexpr uint32_t publicationCount = 8;
			for (uint32_t publicationIndex = 0;
				publicationIndex < publicationCount && lifecycleValid; ++publicationIndex)
			{
				VoxelMutationResult mutation{};
				const ValidationResult mutationResult = publicationIndex % 2 == 0
					? ApplySphereEdit(*world, edit, mutation)
					: RestoreAll(*world, mutation);
				CpuMeshBatch batch{};
				std::unique_ptr<PendingCpuMeshBatch> pending;
				auto damageSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
				lifecycleValid = mutationResult.Succeeded() &&
					mutation.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged &&
					BuildCpuMeshBatch(*world, mutation, batch).Succeeded() &&
					ValidateCpuMeshBatch(
						batch, session.GetVisibleCoreMeshes(), pending).Succeeded() && pending &&
					BuildVoxelDamageMarkerSnapshot(*world,
						mutation.m_TargetWorldVoxelRevision, *damageSnapshot).Succeeded();
				if (!lifecycleValid)
				{
					break;
				}

				std::shared_ptr<const NapaVoxelGpuMeshSet> oldFrame = currentFrame;
				const uint64_t oldRevision = session.GetVisibleWorldRevision();
				lifecycleValid = session.BeginMeshPrepare(
					pending, ++operationSerial, 1, damageSnapshot);
				const AssetUploadStatistics queued = scheduler.GetStatistics();
				const bool queuedBytes =
					queued.m_UploadRecordingQueue.m_PendingSourceBytes != 0 &&
					queued.m_UploadRecordingQueue.m_PendingStagingBytes != 0;
				scheduler.DrainReadyWork();
				const bool rejectedEarlyCommit = !session.TickMeshPrepare() &&
					session.GetVisibleWorldRevision() == oldRevision &&
					session.GetFrameView() == oldFrame;
				heldOldFrameUntilCopy &= queuedBytes && rejectedEarlyCommit;

				device.CompleteFence({
					RHIFenceHandle{ 1, 1 }, transferView->GetSubmissionCount(),
					});
				GGLAB_UNUSED(scheduler.Tick());
				const bool published = session.TickMeshPrepare();
				std::shared_ptr<const NapaVoxelGpuMeshSet> nextFrame = session.GetFrameView();
				lifecycleValid &= published && !pending && !damageSnapshot && nextFrame &&
					nextFrame != oldFrame &&
					session.GetVisibleWorldRevision() == mutation.m_TargetWorldVoxelRevision &&
					nextFrame->GetVisibleWorldRevision() == mutation.m_TargetWorldVoxelRevision &&
					session.GetRetiredGpuMeshSetCount() == 1;

				device.CompleteFence({ graphicsFence, graphicsFenceValue - 1 });
				session.RetireCompletedGpuMeshes();
				const bool retainedAfterOlderSubmission =
					session.GetRetiredGpuMeshSetCount() == 1;
				device.CompleteFence({ graphicsFence, graphicsFenceValue });
				session.RetireCompletedGpuMeshes();
				retiredOnlyAtGraphicsFence &= retainedAfterOlderSubmission &&
					session.GetRetiredGpuMeshSetCount() == 0 && oldFrame &&
					oldFrame->GetVisibleWorldRevision() == oldRevision;

				currentFrame = std::move(nextFrame);
				oldFrame.reset();
				quiescentBetweenPublications &=
					IsSchedulerQuiescent(scheduler.GetStatistics()) &&
					device.GetLiveBufferBytes() == GetGpuMeshBytes(currentFrame);
				++graphicsFenceValue;
				session.OnFrameSubmitted({ graphicsFence, graphicsFenceValue });
				session.OnFrameSubmitted({ graphicsFence, graphicsFenceValue - 1 });
			}

			context.Check(lifecycleValid && heldOldFrameUntilCopy,
				"Repeated replacements keep one complete old frame visible until each Copy Fence");
			context.Check(lifecycleValid && retiredOnlyAtGraphicsFence,
				"OnFrameSubmitted ignores older values and retires each draw set at its exact Graphics Fence");
			context.Check(lifecycleValid && quiescentBetweenPublications &&
				session.GetLastPublicationSerial() == publicationCount,
				"Repeated edit/restore publications return Scheduler and retirement bytes to a "
				"visible-only baseline");

			VoxelMutationResult exitMutation{};
			CpuMeshBatch exitBatch{};
			std::unique_ptr<PendingCpuMeshBatch> exitPending;
			auto exitDamageSnapshot = std::make_unique<VoxelDamageMarkerSnapshot>();
			const bool exitPrepared = lifecycleValid &&
				ApplySphereEdit(*world, edit, exitMutation).Succeeded() &&
				exitMutation.GetChangeKind() == VoxelMutationChangeKind::SurfaceChanged &&
				BuildCpuMeshBatch(*world, exitMutation, exitBatch).Succeeded() &&
				ValidateCpuMeshBatch(exitBatch,
					session.GetVisibleCoreMeshes(), exitPending).Succeeded() && exitPending &&
				BuildVoxelDamageMarkerSnapshot(*world,
					exitMutation.m_TargetWorldVoxelRevision, *exitDamageSnapshot).Succeeded() &&
				session.BeginMeshPrepare(
					exitPending, ++operationSerial, 1, exitDamageSnapshot);
			if (exitPrepared)
			{
				scheduler.DrainReadyWork();
			}
			const uint64_t visibleBytes = GetGpuMeshBytes(currentFrame);
			const bool hadInFlightBytes = exitPrepared &&
				scheduler.GetStatistics().m_InFlightBytes != 0 &&
				device.GetLiveBufferBytes() > visibleBytes;
			session.CancelPrepare();
			const bool noDanglingSessionView = !session.IsReady() &&
				!session.HasVisibleMeshes() && !session.GetFrameView() && currentFrame &&
				currentFrame->GetVisibleWorldRevision() == world->GetWorldVoxelRevision() - 1;
			device.CompleteFence({
				RHIFenceHandle{ 1, 1 }, transferView->GetSubmissionCount(),
				});
			GGLAB_UNUSED(scheduler.Tick());
			const bool copyRetiredWithoutPublication = noDanglingSessionView &&
				IsSchedulerQuiescent(scheduler.GetStatistics()) &&
				device.GetLiveBufferBytes() == visibleBytes;
			device.CompleteFence({ graphicsFence, graphicsFenceValue });
			currentFrame.reset();
			const bool allBytesRetired = device.GetLiveBufferBytes() == 0 &&
				device.GetLiveBufferCount() == 0 &&
				device.GetCreatedBufferCount() == device.GetDestroyedBufferCount();
			context.Check(hadInFlightBytes && copyRetiredWithoutPublication && allBytesRetired,
				"Lab exit during replacement upload retires Copy/Graphics resources with zero "
				"pending bytes and no frame view");
			scheduler.Finalize();
		}
	}

	void RunNapaVoxelPublicationSelfTests(SelfTestContext& context) noexcept
	{
		RunInitialPublicationStateTests(context);
		RunInitialPublicationFailureTests(context);
		RunInitialPublicationCancellationTests(context);
		RunInitialPublicationIdentityTests(context);
		RunAssetUploadSchedulerWorkerHandoffTest(context);
		RunPublicationSessionCancellationIntegrationTest(context);
		RunInteractivePublicationIntegrationTest(context);
		RunReplacementBatchContractTests(context);
		RunReplacementUploadCompletionTests(context);
		RunEmptyReplacementUploadTest(context);
		RunReplacementUploadFailureTests(context);
		RunReplacementUploadCancellationTests(context);
		RunDataOnlyPublicationContractTests(context);
		RunAtomicDataOnlyPublicationTests(context);
		RunAtomicMeshPublicationTests(context);
		RunLabRuntimeGenerationCancellationStressTests(context);
		RunPublicationLifecycleStressTests(context);
	}
}
