#include "GGLabRuntime/Graphics/RHI/DX12/DX12ResourceLifecycleTools.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

namespace gglab
{
	namespace
	{
		using TestResourceType = DX12TestResourceType;
		struct TestResourceEntry
		{
			uint64_t m_Id = 0;
			TestResourceType m_Type = TestResourceType::Texture;
			std::string m_Name;
			RHITextureHandle m_Texture{};
			RHIBufferHandle m_Buffer{};
			bool m_DestroyRequested = false;
		};

		struct ResourceLifecycleState
		{
			std::vector<TestResourceEntry> m_Resources;
			std::string m_LastTestResult = "No lifecycle test has run.";
			uint32_t m_TextureSerial = 0;
			uint32_t m_BufferSerial = 0;
			uint64_t m_NextEntryId = 1;
		};

		RHITextureHandle CreateTestTexture(
			DX12Device& device, uint32_t serial, std::string* outName = nullptr) noexcept
		{
			const std::string name = std::format("ResourceManagement.TestTexture.{}", serial);
			if (outName)
			{
				*outName = name;
			}
			RHITextureDesc desc{};
			desc.m_Format = RHIFormat::R8G8B8A8Unorm;
			desc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest;
			desc.m_Extent = { 16, 16, 1 };
			const RHIResourceDebugIdentityDesc debugIdentity{
				.m_Domain = RHIResourceDebugDomain::DevTools,
				.m_Category = "TestTexture",
				.m_Label = name,
				.m_StableId = serial,
			};
			return device.CreateTexture({ .m_Desc = desc }, debugIdentity);
		}

		RHIBufferHandle CreateTestBuffer(
			DX12Device& device, uint32_t serial, std::string* outName = nullptr) noexcept
		{
			const std::string name = std::format("ResourceManagement.TestBuffer.{}", serial);
			if (outName)
			{
				*outName = name;
			}
			RHIBufferDesc desc{};
			desc.m_SizeInBytes = 4096;
			desc.m_StrideInBytes = 16;
			desc.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest;
			const RHIResourceDebugIdentityDesc debugIdentity{
				.m_Domain = RHIResourceDebugDomain::DevTools,
				.m_Category = "TestBuffer",
				.m_Label = name,
				.m_StableId = serial,
			};
			return device.CreateBuffer(desc, debugIdentity);
		}

		void AddTestTexture(DX12Device& device, ResourceLifecycleState& state) noexcept
		{
			TestResourceEntry entry{};
			entry.m_Id = state.m_NextEntryId++;
			entry.m_Type = TestResourceType::Texture;
			entry.m_Texture = CreateTestTexture(device, ++state.m_TextureSerial, &entry.m_Name);
			if (entry.m_Texture.IsValid())
			{
				state.m_Resources.push_back(std::move(entry));
			}
		}

		void AddTestBuffer(DX12Device& device, ResourceLifecycleState& state) noexcept
		{
			TestResourceEntry entry{};
			entry.m_Id = state.m_NextEntryId++;
			entry.m_Type = TestResourceType::Buffer;
			entry.m_Buffer = CreateTestBuffer(device, ++state.m_BufferSerial, &entry.m_Name);
			if (entry.m_Buffer.IsValid())
			{
				state.m_Resources.push_back(std::move(entry));
			}
		}

		void DestroyTestResource(DX12Device& device, TestResourceEntry& entry) noexcept
		{
			if (entry.m_DestroyRequested)
			{
				return;
			}

			if (entry.m_Type == TestResourceType::Texture)
			{
				device.DestroyTexture(entry.m_Texture);
			}
			else
			{
				device.DestroyBuffer(entry.m_Buffer);
			}
			entry.m_DestroyRequested = true;
		}

		void RecordTestResourcesUse(DX12Device& device, DX12QueueSystem& queueSystem,
			ResourceLifecycleState& state) noexcept
		{
			const DX12FencePoint fencePoint =
				queueSystem.GetQueue(DX12QueueType::Graphics).Signal();
			for (const auto& entry : state.m_Resources)
			{
				if (entry.m_DestroyRequested)
				{
					continue;
				}

				if (entry.m_Type == TestResourceType::Texture)
				{
					device.RecordTextureUse(entry.m_Texture, fencePoint);
				}
				else
				{
					device.RecordBufferUse(entry.m_Buffer, fencePoint);
				}
			}
		}

		void RunLifecycleProbe(DX12Device& device, DX12QueueSystem& queueSystem,
			DX12ResourceManager& manager, ResourceLifecycleState& state) noexcept
		{
			const RHITextureHandle first = CreateTestTexture(device, ++state.m_TextureSerial);
			const bool created = first.IsValid() && device.IsAlive(first);
			device.DestroyTexture(first);
			const bool invalidatedImmediately = !device.IsAlive(first);

			queueSystem.WaitIdle();
			manager.RetireCompletedResources();

			const RHITextureHandle replacement = CreateTestTexture(device, ++state.m_TextureSerial);
			const bool reusedSlot = replacement.IsValid() && replacement.Index() == first.Index();
			const bool generationChanged =
				replacement.IsValid() && replacement.Generation() != first.Generation();

			device.DestroyTexture(first);
			device.DestroyTexture(replacement);
			queueSystem.WaitIdle();
			manager.RetireCompletedResources();

			auto wrappedGeneration = std::numeric_limits<RHITextureHandle::GenerationType>::max();
			++wrappedGeneration;
			if (wrappedGeneration == RHITextureHandle::InvalidGeneration)
			{
				++wrappedGeneration;
			}
			const bool rolloverValid = wrappedGeneration != RHITextureHandle::InvalidGeneration;
			const bool passed = created && invalidatedImmediately && reusedSlot &&
				generationChanged && rolloverValid;

			state.m_LastTestResult = std::format(
				"{} | create={} immediate-invalidate={} slot-reuse={} generation-change={} rollover={}",
				passed ? "PASS" : "FAIL", created, invalidatedImmediately, reusedSlot,
				generationChanged, rolloverValid);
		}

		class DX12ResourceLifecycleTools final : public DX12ResourceLifecycleToolsBase
		{
		public:
			explicit DX12ResourceLifecycleTools(DX12Context& context) noexcept :
				m_Context(context), m_Device(context.GetDX12Device()),
				m_QueueSystem(context.GetQueueSystem()), m_Manager(*m_Device.GetResourceManager())
			{
			}

			~DX12ResourceLifecycleTools() override
			{
				// Only this adapter owns these handles; the context outlives the adapter.
				// Destroy requests preserve the resource manager's recorded fence points.
				DestroyAll();
			}

			DX12ResourceLifecycleSnapshot GetSnapshot() const noexcept override
			{
				DX12ResourceLifecycleSnapshot snapshot;
				snapshot.m_LastTestResult = m_State.m_LastTestResult;
				snapshot.m_Resources.reserve(m_State.m_Resources.size());
				for (const auto& entry : m_State.m_Resources)
				{
					const bool texture = entry.m_Type == TestResourceType::Texture;
					snapshot.m_Resources.push_back({
						.m_Id = entry.m_Id,
						.m_Type = entry.m_Type,
						.m_Name = entry.m_Name,
						.m_Index = texture ? entry.m_Texture.Index() : entry.m_Buffer.Index(),
						.m_Generation = texture ? entry.m_Texture.Generation() : entry.m_Buffer.Generation(),
						.m_Alive = texture ? m_Device.IsAlive(entry.m_Texture) : m_Device.IsAlive(entry.m_Buffer),
						.m_DestroyRequested = entry.m_DestroyRequested,
						});
				}
				return snapshot;
			}

			void AddTexture() noexcept override { AddTestTexture(m_Device, m_State); }
			void AddBuffer() noexcept override { AddTestBuffer(m_Device, m_State); }
			void DestroyResource(uint64_t id) noexcept override
			{
				for (auto& entry : m_State.m_Resources)
				{
					if (entry.m_Id == id)
					{
						DestroyTestResource(m_Device, entry);
						return;
					}
				}
			}
			void DestroyAll() noexcept override
			{
				for (auto& entry : m_State.m_Resources)
				{
					DestroyTestResource(m_Device, entry);
				}
			}
			void SignalAndRecordUse() noexcept override
			{
				RecordTestResourcesUse(m_Device, m_QueueSystem, m_State);
			}
			void RemoveDestroyedRow(uint64_t id) noexcept override
			{
				std::erase_if(m_State.m_Resources, [id](const TestResourceEntry& entry) {
					return entry.m_Id == id && entry.m_DestroyRequested;
					});
			}
			void ClearDestroyedRows() noexcept override
			{
				std::erase_if(m_State.m_Resources, [](const TestResourceEntry& entry) {
					return entry.m_DestroyRequested;
					});
			}
			void ProbeInvalidDestroy() noexcept override
			{
				m_Device.DestroyTexture({});
				m_Device.DestroyBuffer({});
			}
			void ProbeInvalidCreate() noexcept override
			{
				RHITextureDesc texture{};
				RHIBufferDesc buffer{};
				GGLAB_UNUSED(m_Device.CreateTexture({ .m_Desc = texture }));
				GGLAB_UNUSED(m_Device.CreateBuffer(buffer));
			}
			void CollectCompleted() noexcept override { m_Manager.RetireCompletedResources(); }
			void FlushAndCollect() noexcept override
			{
				m_Context.WaitIdle();
				CollectCompleted();
			}
			void RunLifecycleTest() noexcept override
			{
				RunLifecycleProbe(m_Device, m_QueueSystem, m_Manager, m_State);
			}

		private:
			DX12Context& m_Context;
			DX12Device& m_Device;
			DX12QueueSystem& m_QueueSystem;
			DX12ResourceManager& m_Manager;
			ResourceLifecycleState m_State;
		};
	}

	std::unique_ptr<DX12ResourceLifecycleToolsBase>
		CreateDX12ResourceLifecycleTools(RHIContext& context) noexcept
	{
		auto* dx12 = dynamic_cast<DX12Context*>(&context);
		return dx12 ? std::make_unique<DX12ResourceLifecycleTools>(*dx12) : nullptr;
	}
}
