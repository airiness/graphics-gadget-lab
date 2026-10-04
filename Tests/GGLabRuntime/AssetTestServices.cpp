#include "AssetTestServices.h"

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Diagnostics/AssetSnapshotRead.h"
#include "GGLabFoundation/Platform/Win/Win32TaskWorkerLifecycle.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <fstream>
#include <format>
#include <limits>
#include <system_error>

#include <Windows.h>
#include <combaseapi.h>

namespace gglab::asset_test
{
	namespace
	{
		// DirectXTex caches one WIC factory per process. Keep the multithreaded
		// apartment alive across harness TaskSystems so that factory stays valid
		// after the workers that created it uninitialize COM.
		void RetainMultithreadedApartment() noexcept
		{
			static const bool retained = []() noexcept
				{
					CO_MTA_USAGE_COOKIE cookie{};
					return SUCCEEDED(CoIncrementMTAUsage(&cookie));
				}();
			GGLAB_ASSERT_MSG(retained, "Asset test services require a process-lifetime MTA.");
		}

		[[nodiscard]] std::unique_ptr<RHITransferContext> MakeTransferContext(
			TestTransferContext*& observer) noexcept
		{
			auto context = std::make_unique<TestTransferContext>();
			observer = context.get();
			return context;
		}

		void AppendBigEndian32(std::vector<uint8_t>& bytes, uint32_t value)
		{
			bytes.push_back(static_cast<uint8_t>(value >> 24));
			bytes.push_back(static_cast<uint8_t>(value >> 16));
			bytes.push_back(static_cast<uint8_t>(value >> 8));
			bytes.push_back(static_cast<uint8_t>(value));
		}

		[[nodiscard]] uint32_t Crc32(std::span<const uint8_t> bytes) noexcept
		{
			uint32_t crc = 0xffffffffu;
			for (const uint8_t byte : bytes)
			{
				crc ^= byte;
				for (int bit = 0; bit < 8; ++bit)
				{
					crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
				}
			}
			return ~crc;
		}

		void AppendPngChunk(std::vector<uint8_t>& png, const char (&type)[5],
			std::span<const uint8_t> data)
		{
			AppendBigEndian32(png, static_cast<uint32_t>(data.size()));
			const size_t typeOffset = png.size();
			png.insert(png.end(), type, type + 4);
			png.insert(png.end(), data.begin(), data.end());
			AppendBigEndian32(png, Crc32(std::span(png).subspan(typeOffset)));
		}
	}

	RHITextureHandle TestRHIDevice::CreateTexture(
		const RHIOwnedTextureCreateInfo&, const RHIResourceDebugIdentityDesc&) noexcept
	{
		const RHITextureHandle handle{ m_NextTextureIndex++, 1 };
		m_LiveTextures.insert(handle);
		return handle;
	}

	RHIBufferHandle TestRHIDevice::CreateBuffer(
		const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc&) noexcept
	{
		const RHIBufferHandle handle{ m_NextBufferIndex++, 1 };
		m_LiveBuffers.emplace(handle, desc.m_SizeInBytes);
		return handle;
	}

	void TestRHIDevice::DestroyBuffer(RHIBufferHandle buffer) noexcept
	{
		m_LiveBuffers.erase(buffer);
	}

	bool TestRHIDevice::IsFencePointCompleted(const RHIFencePoint& fencePoint) const noexcept
	{
		return fencePoint.IsValid() && m_AllFencesCompleted;
	}

	TemporaryDirectory::TemporaryDirectory(std::string_view name) noexcept
	{
		static std::atomic<uint32_t> sequence = 0;
		std::error_code errorCode;
		const std::filesystem::path path = std::filesystem::temp_directory_path(errorCode) /
			std::format("gglab-{}-{}-{}", name,
				std::chrono::steady_clock::now().time_since_epoch().count(), sequence++);
		if (!errorCode && std::filesystem::create_directories(path, errorCode) && !errorCode)
		{
			m_Path = path;
		}
	}

	TemporaryDirectory::~TemporaryDirectory()
	{
		if (!m_Path.empty())
		{
			std::error_code errorCode;
			std::filesystem::remove_all(m_Path, errorCode);
		}
	}

	bool WritePng(const std::filesystem::path& path, uint32_t width, uint32_t height,
		std::span<const uint8_t> rgba) noexcept
	{
		const size_t rowBytes = static_cast<size_t>(width) * 4;
		if (width == 0 || height == 0 || rgba.size() != rowBytes * height)
		{
			return false;
		}

		// Filter type 0 per scanline, stored in a zlib stream of uncompressed blocks.
		std::vector<uint8_t> scanlines;
		scanlines.reserve((rowBytes + 1) * height);
		for (uint32_t row = 0; row < height; ++row)
		{
			scanlines.push_back(0);
			const auto source = rgba.subspan(row * rowBytes, rowBytes);
			scanlines.insert(scanlines.end(), source.begin(), source.end());
		}
		std::vector<uint8_t> zlib{ 0x78, 0x01 };
		size_t offset = 0;
		do
		{
			const size_t blockSize = std::min<size_t>(scanlines.size() - offset, 0xffff);
			const bool final = offset + blockSize == scanlines.size();
			zlib.push_back(final ? 1 : 0);
			zlib.push_back(static_cast<uint8_t>(blockSize));
			zlib.push_back(static_cast<uint8_t>(blockSize >> 8));
			zlib.push_back(static_cast<uint8_t>(~blockSize));
			zlib.push_back(static_cast<uint8_t>(~blockSize >> 8));
			zlib.insert(zlib.end(), scanlines.begin() + offset, scanlines.begin() + offset + blockSize);
			offset += blockSize;
		} while (offset < scanlines.size());
		uint32_t adlerA = 1;
		uint32_t adlerB = 0;
		for (const uint8_t byte : scanlines)
		{
			adlerA = (adlerA + byte) % 65521u;
			adlerB = (adlerB + adlerA) % 65521u;
		}
		AppendBigEndian32(zlib, (adlerB << 16) | adlerA);

		std::vector<uint8_t> header;
		AppendBigEndian32(header, width);
		AppendBigEndian32(header, height);
		header.insert(header.end(), { 8, 6, 0, 0, 0 });

		std::vector<uint8_t> png{ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
		AppendPngChunk(png, "IHDR", header);
		AppendPngChunk(png, "IDAT", zlib);
		AppendPngChunk(png, "IEND", {});

		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
		return static_cast<bool>(file);
	}

	bool WriteSolidPng(const std::filesystem::path& path,
		uint8_t red, uint8_t green, uint8_t blue) noexcept
	{
		std::array<uint8_t, 16> pixels{};
		for (size_t pixel = 0; pixel < 4; ++pixel)
		{
			pixels[pixel * 4 + 0] = red;
			pixels[pixel * 4 + 1] = green;
			pixels[pixel * 4 + 2] = blue;
			pixels[pixel * 4 + 3] = 255;
		}
		return WritePng(path, 2, 2, pixels);
	}

	bool WriteNoisePng(const std::filesystem::path& path, uint32_t size) noexcept
	{
		std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
		uint32_t state = 0x9e3779b9u;
		for (size_t index = 0; index < pixels.size(); ++index)
		{
			state = state * 1664525u + 1013904223u;
			pixels[index] = (index % 4 == 3) ? uint8_t{ 255 } : static_cast<uint8_t>(state >> 24);
		}
		return WritePng(path, size, size, pixels);
	}

	bool WriteTexturedModel(const std::filesystem::path& gltfPath,
		std::span<const std::string> baseColorImages) noexcept
	{
		if (baseColorImages.empty())
		{
			return false;
		}
		// Position, normal and UV streams of one triangle shared by every mesh.
		constexpr std::array<float, 24> attributes{
			0, 0, 0, 1, 0, 0, 0, 1, 0,
			0, 0, 1, 0, 0, 1, 0, 0, 1,
			0, 0, 1, 0, 0, 1,
		};
		const std::filesystem::path bufferPath =
			std::filesystem::path(gltfPath).replace_extension(".bin");
		{
			std::ofstream buffer(bufferPath, std::ios::binary | std::ios::trunc);
			buffer.write(reinterpret_cast<const char*>(attributes.data()), sizeof(attributes));
			if (!buffer)
			{
				return false;
			}
		}

		std::string images;
		std::string textures;
		std::string materials;
		std::string meshes;
		std::string nodes;
		std::string sceneNodes;
		for (size_t index = 0; index < baseColorImages.size(); ++index)
		{
			const char* separator = index == 0 ? "" : ",";
			images += std::format(R"({}{{"uri":"{}"}})", separator, baseColorImages[index]);
			textures += std::format(R"({}{{"source":{}}})", separator, index);
			materials += std::format(
				R"({}{{"name":"Material{}","pbrMetallicRoughness":{{"baseColorTexture":{{"index":{}}}}}}})",
				separator, index, index);
			meshes += std::format(
				R"({}{{"primitives":[{{"attributes":{{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2}},"material":{}}}]}})",
				separator, index);
			nodes += std::format(R"({}{{"mesh":{}}})", separator, index);
			sceneNodes += std::format("{}{}", separator, index);
		}

		std::ofstream gltf(gltfPath, std::ios::trunc);
		gltf << R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[)" << sceneNodes << R"(]}],)"
			<< R"("nodes":[)" << nodes << R"(],)"
			<< R"("buffers":[{"uri":")" << bufferPath.filename().string() << R"(","byteLength":96}],)"
			<< R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},)"
			<< R"({"buffer":0,"byteOffset":36,"byteLength":36},)"
			<< R"({"buffer":0,"byteOffset":72,"byteLength":24}],)"
			<< R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
			<< R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},)"
			<< R"({"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}],)"
			<< R"("images":[)" << images << R"(],"textures":[)" << textures << R"(],)"
			<< R"("materials":[)" << materials << R"(],"meshes":[)" << meshes << R"(]})";
		return static_cast<bool>(gltf);
	}

	AssetManagerHarness::AssetManagerHarness(
		std::string_view name, const CreateInfo& createInfo) noexcept :
		m_Root(name),
		m_TransferManager((RetainMultithreadedApartment(), MakeTransferContext(m_TransferContext))),
		// Workers initialize COM like the application host so WIC can decode textures.
		m_TaskSystem(TaskSystem::CreateInfo{
			.m_WorkerCount = 2,
			.m_WorkerLifecycle = std::make_shared<win32::Win32TaskWorkerLifecycle>(),
			}),
		m_Scheduler(CreateAssetUploadScheduler({
			.m_Device = &m_Device,
			.m_TransferManager = &m_TransferManager,
			.m_FrameBudget = createInfo.m_FrameBudget,
			}))
	{
		// AssetManager loads its reserved debug textures from the asset root.
		std::error_code errorCode;
		const std::filesystem::path reservedDirectory = m_Root.GetPath() / "Textures";
		if (!m_Root.IsValid() || !std::filesystem::create_directories(reservedDirectory, errorCode) ||
			!WriteSolidPng(reservedDirectory / "UVTest1K.png", 128, 128, 128) ||
			!WriteSolidPng(reservedDirectory / "UVTest4K.png", 128, 128, 128))
		{
			return;
		}
		AssetManager::CreateInfo assetCreateInfo{};
		assetCreateInfo.m_Device = &m_Device;
		assetCreateInfo.m_TaskSystem = &m_TaskSystem;
		assetCreateInfo.m_TransferManager = &m_TransferManager;
		assetCreateInfo.m_AssetUploadScheduler = m_Scheduler.m_Scheduling.get();
		assetCreateInfo.m_SamplerRegistry = &m_Samplers;
		assetCreateInfo.m_AssetRoot = m_Root.GetPath();
		if (createInfo.m_EnableTextureDerivedDataCache)
		{
			assetCreateInfo.m_TextureDerivedDataCacheDirectory = m_Root.GetPath() / "DerivedData";
		}
		m_AssetManager = std::make_unique<AssetManager>(assetCreateInfo);
		// Released runtime entries retire within a few frames instead of the
		// application's multi-second retention window.
		AssetResidencyConfig residency = m_AssetManager->GetResidencyConfig();
		residency.m_RuntimeEntryRetentionFrames = createInfo.m_RuntimeEntryRetentionFrames;
		m_AssetManager->SetResidencyConfig(residency);
	}

	AssetManagerHarness::~AssetManagerHarness()
	{
		// Mirrors the application shutdown order for asset services.
		if (m_AssetManager)
		{
			m_AssetManager->BeginShutdown();
			m_TaskSystem.Shutdown();
			m_TaskSystem.PumpCompletions();
			m_AssetManager->DrainLoadCompletions();
			m_Scheduler.m_Scheduling->DrainReadyWork();
			m_Scheduler.m_Scheduling->Finalize();
			m_AssetManager->PrepareForShutdown(m_TransferContext->GetLastSubmittedFence());
			m_AssetManager.reset();
		}
		else
		{
			m_TaskSystem.Shutdown();
			m_Scheduler.m_Scheduling->Finalize();
		}
	}

	const std::filesystem::path& AssetManagerHarness::GetAssetRoot() const noexcept
	{
		return m_Root.GetPath();
	}

	void AssetManagerHarness::PumpFrame() noexcept
	{
		m_TaskSystem.PumpCompletions();
		m_AssetManager->DrainLoadCompletions();
		m_AssetManager->Tick();
		GGLAB_UNUSED(m_Scheduler.m_Scheduling->Tick());
	}

	void AssetManagerHarness::PumpFrames(uint32_t frameCount) noexcept
	{
		for (uint32_t frame = 0; frame < frameCount; ++frame)
		{
			PumpFrame();
			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}
	}

	const AssetSnapshot::Model* FindModel(const AssetSnapshot& snapshot, ModelID modelId) noexcept
	{
		const auto model = std::ranges::find(snapshot.m_Models, modelId, &AssetSnapshot::Model::m_Id);
		return model != snapshot.m_Models.end() ? &*model : nullptr;
	}

	const AssetSnapshot::Mesh* FindMesh(const AssetSnapshot& snapshot, MeshID meshId) noexcept
	{
		const auto mesh = std::ranges::find(snapshot.m_Meshes, meshId, &AssetSnapshot::Mesh::m_Id);
		return mesh != snapshot.m_Meshes.end() ? &*mesh : nullptr;
	}

	const AssetSnapshot::Texture* FindTexture(const AssetSnapshot& snapshot, TextureID textureId) noexcept
	{
		const auto texture =
			std::ranges::find(snapshot.m_Textures, textureId, &AssetSnapshot::Texture::m_Id);
		return texture != snapshot.m_Textures.end() ? &*texture : nullptr;
	}

	const AssetInterestActivity* FindInterest(
		const AssetOwnershipStatistics& ownership, AssetKind kind, uint64_t stableId) noexcept
	{
		const auto interest = std::ranges::find_if(ownership.m_ActiveInterests,
			[kind, stableId](const AssetInterestActivity& activity) noexcept
			{ return activity.m_Kind == kind && activity.m_StableId == stableId; });
		return interest != ownership.m_ActiveInterests.end() ? &*interest : nullptr;
	}

	bool IsTerminal(AssetState state) noexcept
	{
		return state == AssetState::Ready || state == AssetState::Failed ||
			state == AssetState::Cancelled;
	}

	bool IsStreamingIdle(const AssetUploadStatistics& statistics) noexcept
	{
		return statistics.m_CpuPayloadQueue.m_PendingCount == 0 &&
			statistics.m_ResourcePublicationQueue.m_PendingCount == 0 &&
			statistics.m_UploadRecordingQueue.m_PendingCount == 0 &&
			statistics.m_GpuFinalizeQueue.m_PendingCount == 0 &&
			statistics.m_PendingCount == 0 && statistics.m_ReadyPayloadBytes == 0 &&
			statistics.m_InFlightBytes == 0;
	}

	bool HasPendingPublication(
		const AssetUploadStatistics& statistics, const AssetStreamingIdentity& identity) noexcept
	{
		return std::ranges::any_of(statistics.m_ResourcePublicationQueue.m_PendingWork,
			[&identity](const AssetStreamingWorkActivity& work) noexcept
			{ return work.m_Identity == identity; });
	}

	bool HasPendingUpload(
		const AssetUploadStatistics& statistics, const AssetStreamingIdentity& identity) noexcept
	{
		return std::ranges::any_of(statistics.m_PendingUploads,
			[&identity](const AssetUploadActivity& upload) noexcept
			{ return upload.m_Identity == identity; });
	}

	void CheckQuiescent(SelfTestContext& context, AssetManagerHarness& harness,
		std::string_view scenario) noexcept
	{
		const bool drained = harness.PumpUntil([&]()
			{
				const AssetOwnershipStatistics ownership = harness.GetAssets().GetOwnershipStatistics();
				return ownership.m_OwnerCount == 0 && ownership.m_LeaseCount == 0 &&
					IsStreamingIdle(harness.GetScheduling().GetStatistics());
			});
		const AssetOwnershipStatistics ownership = harness.GetAssets().GetOwnershipStatistics();
		const AssetUploadStatistics upload = harness.GetScheduling().GetStatistics();
		const auto& publication = upload.m_ResourcePublicationQueue;
		context.Check(drained && ownership.m_OwnerCount == 0 && ownership.m_LeaseCount == 0 &&
			ownership.m_PublicationRetainCount == 0 && ownership.m_ActiveInterests.empty(),
			std::format("{}: released owners leave no owner, lease, retain or interest "
				"(owners={}, leases={}, retains={})", scenario, ownership.m_OwnerCount,
				ownership.m_LeaseCount, ownership.m_PublicationRetainCount));
		context.Check(IsStreamingIdle(upload) && publication.m_NoProgressContinueCount == 0 &&
			publication.m_EnqueuedCount == publication.m_CompletedCount + publication.m_FailedCount +
				publication.m_CancelledCount + publication.m_PendingCount &&
			publication.m_QueueSampleCount == publication.m_EnqueuedCount,
			std::format("{}: publication work is conserved and every queue settles", scenario));
		context.Check(upload.m_SubmittedCount == upload.m_SucceededCount + upload.m_FailedCount &&
			upload.m_CompletionCallbackFailureCount == 0,
			std::format("{}: every submitted upload finalizes without callback failures", scenario));
	}
}
