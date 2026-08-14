#include "Application/SelfTest/AssetUploadSchedulerSelfTests.h"

#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHISampler.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RHI/RHITransferContext.h"
#include "Graphics/RHI/RHITypes.h"
#include "Graphics/TransferManager.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

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

	}

	void RunAssetUploadSchedulerSelfTests(SelfTestContext& context) noexcept
	{
		RunAssetUploadSchedulerWorkerHandoffTest(context);
	}
}
