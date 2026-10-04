#pragma once

#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadControl.h"
#include "GGLabRuntime/Graphics/Asset/AssetUploadScheduling.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITransferContext.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabTestCore/SelfTest.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Headless asset services for owner-level Runtime tests. The fake RHI creates
// handles and completes fences on demand; it never records GPU work, so tests
// using it establish CPU contracts only.
namespace gglab::asset_test
{
	class TestRHIDevice final : public RHIDevice
	{
	public:
		RHIBackendType GetBackendType() const noexcept override { return RHIBackendType::DX12; }
		std::string_view GetAdapterCompatibilityIdentity() const noexcept override
		{
			return "GGLab.AssetTestDevice";
		}
		RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override { return {}; }
		RHITextureSupportResult QueryTextureSupport(const RHITextureDesc&) const noexcept override
		{
			return { .m_Supported = true };
		}
		RHITextureSupportResult QueryTextureViewSupport(
			const RHITextureDesc&, const RHITextureViewDesc&) const noexcept override
		{
			return { .m_Supported = true };
		}
		RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo&,
			const RHIResourceDebugIdentityDesc&) noexcept override;
		RHIBufferHandle CreateBuffer(
			const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc&) noexcept override;
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
		void DestroyTexture(RHITextureHandle texture) noexcept override { m_LiveTextures.erase(texture); }
		void DestroyBuffer(RHIBufferHandle buffer) noexcept override;
		void DestroyTextureView(RHITextureViewHandle) noexcept override {}
		void DestroyBufferView(RHIBufferViewHandle) noexcept override {}
		void DestroySampler(RHISamplerHandle) noexcept override {}
		void SetTextureDebugBinding(RHITextureHandle, const RHIResourceDebugBindingDesc&) noexcept override {}
		void SetBufferDebugBinding(RHIBufferHandle, const RHIResourceDebugBindingDesc&) noexcept override {}
		std::string_view GetTextureDebugName(RHITextureHandle) const noexcept override { return {}; }
		std::string_view GetBufferDebugName(RHIBufferHandle) const noexcept override { return {}; }
		void* MapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override { return nullptr; }
		void UnmapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override {}
		uint32_t GetBufferViewAlignment(RHIBufferViewType) const noexcept override { return 1; }
		bool IsAlive(RHITextureHandle texture) const noexcept override { return m_LiveTextures.contains(texture); }
		bool IsAlive(RHIBufferHandle buffer) const noexcept override { return m_LiveBuffers.contains(buffer); }
		bool IsAlive(RHISamplerHandle sampler) const noexcept override { return sampler.IsValid(); }
		bool IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept override;
		void RecordTextureUse(RHITextureHandle, const RHIFencePoint&) noexcept override {}
		void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
		RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle view) const noexcept override
		{
			return { .m_HeapType = RHIDescriptorHeapType::CbvSrvUav, .m_Index = view.Index() };
		}
		RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle) const noexcept override { return {}; }
		RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle sampler) const noexcept override
		{
			return { .m_HeapType = RHIDescriptorHeapType::Sampler, .m_Index = sampler.Index() };
		}
		void RetireCompletedWork() noexcept override {}

		// Fences complete immediately unless a test withholds completion.
		void SetFencesComplete(bool complete) noexcept { m_AllFencesCompleted = complete; }
		[[nodiscard]] size_t GetLiveTextureCount() const noexcept { return m_LiveTextures.size(); }

	private:
		std::unordered_map<RHIBufferHandle, uint64_t> m_LiveBuffers;
		std::unordered_set<RHITextureHandle> m_LiveTextures;
		uint32_t m_NextTextureIndex = 0;
		uint32_t m_NextTextureViewIndex = 0;
		uint32_t m_NextSamplerIndex = 0;
		uint32_t m_NextBufferIndex = 0;
		bool m_AllFencesCompleted = true;
	};

	class TestTransferContext final : public RHITransferContext
	{
	public:
		RHICommandContextHandle GetHandle() const noexcept override { return { 1, 1 }; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Copy; }
		void TrackTextureUse(RHITextureHandle) noexcept override {}
		void TrackBufferUse(RHIBufferHandle) noexcept override {}
		void TextureBarrier(std::span<const RHITextureBarrier>) noexcept override {}
		void BufferBarrier(std::span<const RHIBufferBarrier>) noexcept override {}
		void FlushBarriers() noexcept override {}
		void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override {}
		void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override {}
		void Begin() noexcept override { m_IsRecording = true; }
		RHIFencePoint Submit(bool) noexcept override
		{
			m_IsRecording = false;
			return GetLastSubmittedFence(++m_SubmissionCount);
		}
		void Abort() noexcept override { m_IsRecording = false; }
		void ReclaimCompleted() noexcept override {}
		bool UploadBuffer(const void* data, uint64_t sizeInBytes, RHIBufferHandle destination,
			uint64_t) noexcept override
		{
			return m_IsRecording && data && sizeInBytes != 0 && destination.IsValid();
		}
		bool UploadTexture(const RHITextureUploadData& data, RHITextureHandle destination) noexcept override
		{
			return m_IsRecording && data.IsValid() && destination.IsValid();
		}
		RHITextureReadbackRequest ReadbackTexture(RHITextureHandle, const RHITextureDesc&) noexcept override
		{
			return {};
		}

		[[nodiscard]] RHIFencePoint GetLastSubmittedFence() const noexcept
		{
			return GetLastSubmittedFence(m_SubmissionCount);
		}

	private:
		[[nodiscard]] static RHIFencePoint GetLastSubmittedFence(uint64_t value) noexcept
		{
			return { RHIFenceHandle{ 1, 1 }, value };
		}

		uint64_t m_SubmissionCount = 0;
		bool m_IsRecording = false;
	};

	class TestSamplerAccess final : public RenderSamplerAccess
	{
	public:
		SamplerID GetOrCreateSampler(const SamplerKey&) noexcept override { return SamplerID{ 1 }; }
		SamplerID GetPresetSamplerId(SamplerPreset) const noexcept override { return SamplerID{ 1 }; }
		uint32_t GetSamplerIndex(SamplerPreset) const noexcept override { return 0; }
		uint32_t GetSamplerIndex(const SamplerID&) const noexcept override { return 0; }
		uint32_t ResolveSamplerIndex(SamplerID, SamplerPreset) const noexcept override { return 0; }
	};

	// Unique directory under the system temporary path, removed on destruction.
	class TemporaryDirectory
	{
	public:
		explicit TemporaryDirectory(std::string_view name) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TemporaryDirectory);
		~TemporaryDirectory();

		[[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_Path; }
		[[nodiscard]] bool IsValid() const noexcept { return !m_Path.empty(); }

	private:
		std::filesystem::path m_Path;
	};

	// Writes an RGBA8 PNG with uncompressed deflate blocks.
	[[nodiscard]] bool WritePng(const std::filesystem::path& path, uint32_t width, uint32_t height,
		std::span<const uint8_t> rgba) noexcept;
	// Writes a 2x2 solid-color PNG.
	[[nodiscard]] bool WriteSolidPng(const std::filesystem::path& path,
		uint8_t red, uint8_t green, uint8_t blue) noexcept;
	// Writes a square PNG of deterministic noise; large sizes keep decode tasks
	// running long enough to observe them mid-execution.
	[[nodiscard]] bool WriteNoisePng(const std::filesystem::path& path, uint32_t size) noexcept;

	// Writes a glTF scene with one triangle mesh, material and node per base-color
	// image. Images are referenced relative to the glTF file.
	[[nodiscard]] bool WriteTexturedModel(const std::filesystem::path& gltfPath,
		std::span<const std::string> baseColorImages) noexcept;

	// AssetManager composed over the fake RHI, a real TaskSystem and the real upload
	// scheduler. PumpFrame follows the application frame order.
	class AssetManagerHarness
	{
	public:
		struct CreateInfo
		{
			AssetStreamingFrameBudget m_FrameBudget{};
			uint64_t m_RuntimeEntryRetentionFrames = 2;
			// The application always composes a local texture DDC.
			bool m_EnableTextureDerivedDataCache = true;
		};

		AssetManagerHarness(std::string_view name, const CreateInfo& createInfo) noexcept;
		explicit AssetManagerHarness(std::string_view name) noexcept :
			AssetManagerHarness(name, CreateInfo{})
		{
		}
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManagerHarness);
		// Requires every owner scope created from this harness to be released.
		~AssetManagerHarness();

		[[nodiscard]] bool IsValid() const noexcept { return m_AssetManager != nullptr; }
		[[nodiscard]] const std::filesystem::path& GetAssetRoot() const noexcept;
		[[nodiscard]] AssetManager& GetAssets() const noexcept { return *m_AssetManager; }
		[[nodiscard]] AssetUploadScheduling& GetScheduling() const noexcept
		{
			return *m_Scheduler.m_Scheduling;
		}
		[[nodiscard]] AssetUploadControl& GetControl() const noexcept { return *m_Scheduler.m_Control; }
		[[nodiscard]] TestRHIDevice& GetDevice() noexcept { return m_Device; }
		[[nodiscard]] const TaskSystem& GetTaskSystem() const noexcept { return m_TaskSystem; }

		void PumpFrame() noexcept;
		// Pumps frames until the predicate holds, yielding to asynchronous workers.
		template <typename Predicate>
		[[nodiscard]] bool PumpUntil(Predicate&& predicate,
			std::chrono::milliseconds timeout = std::chrono::seconds(30)) noexcept
		{
			const auto deadline = std::chrono::steady_clock::now() + timeout;
			while (!predicate())
			{
				if (std::chrono::steady_clock::now() > deadline)
				{
					return false;
				}
				PumpFrame();
				std::this_thread::sleep_for(std::chrono::microseconds(200));
			}
			return true;
		}
		// Pumps a fixed number of frames, letting deferred work settle.
		void PumpFrames(uint32_t frameCount) noexcept;

	private:
		TemporaryDirectory m_Root;
		TestRHIDevice m_Device;
		TestTransferContext* m_TransferContext = nullptr;
		TransferManager m_TransferManager;
		TaskSystem m_TaskSystem;
		TestSamplerAccess m_Samplers;
		AssetUploadSchedulerInstance m_Scheduler;
		std::unique_ptr<AssetManager> m_AssetManager;
	};

	[[nodiscard]] const AssetSnapshot::Model* FindModel(
		const AssetSnapshot& snapshot, ModelID modelId) noexcept;
	[[nodiscard]] const AssetSnapshot::Mesh* FindMesh(
		const AssetSnapshot& snapshot, MeshID meshId) noexcept;
	[[nodiscard]] const AssetSnapshot::Texture* FindTexture(
		const AssetSnapshot& snapshot, TextureID textureId) noexcept;
	[[nodiscard]] const AssetInterestActivity* FindInterest(
		const AssetOwnershipStatistics& ownership, AssetKind kind, uint64_t stableId) noexcept;
	[[nodiscard]] bool IsTerminal(AssetState state) noexcept;
	[[nodiscard]] bool IsStreamingIdle(const AssetUploadStatistics& statistics) noexcept;
	[[nodiscard]] bool HasPendingPublication(
		const AssetUploadStatistics& statistics, const AssetStreamingIdentity& identity) noexcept;
	[[nodiscard]] bool HasPendingUpload(
		const AssetUploadStatistics& statistics, const AssetStreamingIdentity& identity) noexcept;

	// After every owner scope is released and released models retire, the harness
	// holds no ownership, and the scheduler conserves every enqueued publication
	// and submitted upload.
	void CheckQuiescent(SelfTestContext& context, AssetManagerHarness& harness,
		std::string_view scenario) noexcept;
}
