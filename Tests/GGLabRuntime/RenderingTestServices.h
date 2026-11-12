#pragma once

#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfileFrameSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipeline.h"
#include "GGLabRuntime/Graphics/RHI/RHIPipelineSystem.h"
#include "GGLabRuntime/Graphics/RHI/RHIResource.h"
#include "GGLabRuntime/Graphics/RHI/RHIResourceDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHISampler.h"
#include "GGLabRuntime/Graphics/RHI/RHISwapChain.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureValidation.h"
#include "GGLabRuntime/Graphics/RHI/RHITransferContext.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIUpload.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderPass/RenderPassInfo.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/TransferManager.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderPass/ForwardShadingCommon.h"

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace gglab::rendering_test
{
	constexpr float ProjectionTolerance = 1.0e-4f;

	constexpr float PositionTolerance = 2.0e-3f;

	struct ProjectedPosition
	{
		Vector2 m_UV = Vector2::Zero;
		float m_RawDepth = 0.0f;
	};

	struct StorageAccessPassData
	{
		RGBufferId m_Buffer;
	};

	struct TextureStorageAccessPassData
	{
		RGTextureId m_Texture;
	};

	class RecordingDevice final : public RHIDevice
	{
	public:
		bool m_PassConstantsEnabled = false;
		bool m_ConstantMapFails = false;
		bool m_ConstantCreateFails = false;
		bool m_ConstantPublishFails = false;
		std::vector<std::vector<std::byte>> m_PassConstantStorage;
		std::vector<RHIBufferDesc> m_UploadBufferDescs;
		RHIUploadLimits m_UploadLimits{ 256, 64 * 1024, 16, 128 * 1024 * 1024 };
		uint32_t m_PassConstantMapCount = 0;
		uint32_t m_PassConstantPublishCount = 0;
		uint32_t m_PassConstantUnmapCount = 0;
		uint32_t m_AtmosphereDestroyedBufferCount = 0;
		RHIBackendType GetBackendType() const noexcept override { return {}; }
		std::string_view GetAdapterCompatibilityIdentity() const noexcept override
		{
			return "RenderingContract.RecordingDevice";
		}
		RHIShaderWaveCapabilities GetShaderWaveCapabilities() const noexcept override
		{
			return {
				.m_Supported = true,
				.m_MinLaneCount = 32,
				.m_MaxLaneCount = 64,
			};
		}
		RHITextureSupportResult QueryTextureSupport(const RHITextureDesc&) const noexcept override
		{
			return {};
		}
		RHITextureSupportResult QueryTextureViewSupport(
			const RHITextureDesc& textureDesc,
			const RHITextureViewDesc& viewDesc) const noexcept override
		{
			m_LastTextureViewQueryTextureDesc = textureDesc;
			m_LastTextureViewQueryDesc = viewDesc;
			++m_TextureViewQueryCount;
			return { .m_Supported = m_TextureViewsSupported };
		}
		RHITextureHandle CreateTexture(const RHIOwnedTextureCreateInfo&, const RHIResourceDebugIdentityDesc&) noexcept override
		{
			++m_CreateTextureCount;
			return RHITextureHandle{ m_NextTextureIndex++, 1 };
		}
		RHIBufferHandle CreateBuffer(
			const RHIBufferDesc& desc, const RHIResourceDebugIdentityDesc&) noexcept override
		{
			if (m_PassConstantsEnabled)
			{
				if (m_ConstantCreateFails)
				{
					return {};
				}
				m_PassConstantStorage.emplace_back(desc.m_SizeInBytes);
				m_UploadBufferDescs.push_back(desc);
				return { static_cast<uint32_t>(m_PassConstantStorage.size()), 1 };
			}
			return {};
		}
		RHITextureViewHandle CreateTextureView(RHITextureHandle, const RHITextureViewDesc&) noexcept override
		{
			return m_CreateValidDescriptors ?
				RHITextureViewHandle{ m_NextTextureViewIndex++, 1 } :
				RHITextureViewHandle{};
		}
		RHIBufferViewHandle CreateBufferView(RHIBufferHandle, const RHIBufferViewDesc&) noexcept override
		{
			return {};
		}
		RHISamplerHandle CreateSampler(const RHISamplerDesc&) noexcept override
		{
			return m_CreateValidDescriptors ?
				RHISamplerHandle{ m_NextSamplerIndex++, 1 } :
				RHISamplerHandle{};
		}
		void DestroyTexture(RHITextureHandle) noexcept override
		{
			++m_DestroyTextureCount;
		}
		void DestroyBuffer(RHIBufferHandle) noexcept override { ++m_AtmosphereDestroyedBufferCount; }
		void DestroyTextureView(RHITextureViewHandle) noexcept override {}
		void DestroyBufferView(RHIBufferViewHandle) noexcept override {}
		void DestroySampler(RHISamplerHandle) noexcept override {}
		void SetTextureDebugBinding(RHITextureHandle, const RHIResourceDebugBindingDesc&) noexcept override
		{
		}
		void SetBufferDebugBinding(RHIBufferHandle, const RHIResourceDebugBindingDesc&) noexcept override
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
		void* MapBuffer(RHIBufferHandle buffer, RHIMappedBufferRange) noexcept override
		{
			if (m_PassConstantsEnabled)
			{
				++m_PassConstantMapCount;
				return m_ConstantMapFails ? nullptr : m_PassConstantStorage[buffer.Index() - 1].data();
			}
			return nullptr;
		}
		void UnmapBuffer(RHIBufferHandle, RHIMappedBufferRange) noexcept override
		{
			if (m_PassConstantsEnabled)
			{
				++m_PassConstantUnmapCount;
			}
		}
		uint32_t GetBufferViewAlignment(RHIBufferViewType) const noexcept override
		{
			return m_PassConstantsEnabled ? 256 : 1;
		}
		RHIUploadLimits GetUploadLimits() const noexcept override { return m_UploadLimits; }
		bool IsUploadStorage(RHIBufferHandle buffer) const noexcept override
		{
			return buffer.IsValid() && buffer.Index() > 0 && buffer.Index() <= m_UploadBufferDescs.size() &&
				Test(m_UploadBufferDescs[buffer.Index() - 1].m_CreateFlags, RHIBufferCreateFlags::UploadStorage);
		}
		bool FlushMappedBufferWrites(RHIBufferHandle buffer, RHIMappedBufferRange range) noexcept override
		{
			if (m_PassConstantsEnabled && buffer.IsValid() && range.m_End > range.m_Begin)
			{
				++m_PassConstantPublishCount;
			}
			// Host memory is coherent here; the flag injects a native publication failure.
			return buffer.IsValid() && !m_ConstantPublishFails;
		}
		bool IsAlive(RHITextureHandle texture) const noexcept override
		{
			return texture.IsValid();
		}
		bool IsAlive(RHIBufferHandle buffer) const noexcept override
		{
			return buffer.IsValid();
		}
		bool IsAlive(RHISamplerHandle sampler) const noexcept override
		{
			return sampler.IsValid();
		}
		bool IsAlive(RHIAccelerationStructureHandle handle) const noexcept override { return handle.IsValid(); }
		using RHIDevice::ObserveGpuWaitResult;
		RHIFenceQueryResult QueryFencePointImpl(const RHIFencePoint& fencePoint) const noexcept override
		{
			++m_FenceQueryCount;
			if (m_FenceQueryResult)
			{
				// Unobserved results model caller misuse rejected before any native query.
				return m_ObserveFenceQueryResult ?
					ObserveFenceQueryResult(*m_FenceQueryResult) :
					*m_FenceQueryResult;
			}
			if (!fencePoint.IsValid())
			{
				return RHIFenceQueryResult::Failed;
			}
			return !m_UseControlledFenceCompletion || fencePoint.m_Value <= m_CompletedFenceValue ?
				RHIFenceQueryResult::Completed :
				RHIFenceQueryResult::Pending;
		}
		void RecordTextureUse(RHITextureHandle, const RHIFencePoint& fencePoint) noexcept override
		{
			++m_RecordTextureUseCount;
			m_LastRecordedFencePoint = fencePoint;
		}
		void RecordBufferUse(RHIBufferHandle, const RHIFencePoint&) noexcept override {}
		RHIDescriptorHandle GetTextureViewDescriptor(RHITextureViewHandle view) const noexcept override
		{
			return m_CreateValidDescriptors && view.IsValid() ?
				RHIDescriptorHandle{ RHIDescriptorHeapType::CbvSrvUav, view.Index() } :
				RHIDescriptorHandle{};
		}
		RHIDescriptorHandle GetBufferViewDescriptor(RHIBufferViewHandle) const noexcept override
		{
			return {};
		}
		RHIDescriptorHandle GetSamplerDescriptor(RHISamplerHandle sampler) const noexcept override
		{
			return m_CreateValidDescriptors && sampler.IsValid() ?
				RHIDescriptorHandle{ RHIDescriptorHeapType::Sampler, sampler.Index() } :
				RHIDescriptorHandle{};
		}
		void RetireCompletedWork() noexcept override {}

		bool m_TextureViewsSupported = false;
		bool m_CreateValidDescriptors = false;
		mutable RHITextureDesc m_LastTextureViewQueryTextureDesc{};
		mutable RHITextureViewDesc m_LastTextureViewQueryDesc{};
		mutable uint32_t m_TextureViewQueryCount = 0;
		mutable uint32_t m_FenceQueryCount = 0;
		std::optional<RHIFenceQueryResult> m_FenceQueryResult;
		bool m_ObserveFenceQueryResult = true;
		bool m_UseControlledFenceCompletion = false;
		uint64_t m_CompletedFenceValue = 0;
		uint32_t m_CreateTextureCount = 0;
		uint32_t m_DestroyTextureCount = 0;
		uint32_t m_RecordTextureUseCount = 0;
		RHIFencePoint m_LastRecordedFencePoint{};

	private:
		uint32_t m_NextTextureIndex = 1;
		uint32_t m_NextTextureViewIndex = 1;
		uint32_t m_NextSamplerIndex = 1;
	};

	struct RecordedConstantBinding
	{
		uint32_t m_ParameterIndex = 0;
		RHIBufferHandle m_Buffer{};
		uint64_t m_Offset = 0;
	};

	class RecordingGraphicsCommandContext final : public RHIGraphicsCommandContext
	{
	public:
		void BeginRecordingScope(RHIUploadWriter& writer) noexcept { BindUploadWriter(&writer); }
		void EndRecordingScope() noexcept { BindUploadWriter(nullptr); }
		RHICommandContextHandle GetHandle() const noexcept override { return {}; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
		void TrackTextureUse(RHITextureHandle) noexcept override {}
		void TrackBufferUse(RHIBufferHandle) noexcept override {}
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override
		{
			GGLAB_ASSERT(!m_IsRendering);
			m_TextureBarrierCount += static_cast<uint32_t>(barriers.size());
			m_TextureBarriers.insert(m_TextureBarriers.end(), barriers.begin(), barriers.end());
		}
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override
		{
			GGLAB_ASSERT(!m_IsRendering);
			m_BufferBarrierCount += static_cast<uint32_t>(barriers.size());
		}
		void FlushBarriers() noexcept override
		{
			GGLAB_ASSERT(!m_IsRendering);
			++m_FlushBarrierCount;
		}
		void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override
		{
			GGLAB_ASSERT(!m_IsRendering);
			++m_CopyBufferCount;
		}
		void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override
		{
			GGLAB_ASSERT(!m_IsRendering);
		}
		void BeginGpuProfileScope(GpuProfileScope scope) noexcept override
		{
			++m_BeginProfileCount;
			AccumulateGpuProfileSample(m_Profile, scope, 1.0);
		}
		void EndGpuProfileScope() noexcept override { ++m_EndProfileCount; }
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override
		{
			InvalidatePipelineBindings();
			if (pipeline.IsValid())
			{
				ConfirmBindingPipeline(pipeline);
			}
		}
		void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
		void BeginRendering(const RHIRenderingInfo&) noexcept override
		{
			GGLAB_ASSERT(!m_IsRendering);
			m_IsRendering = true;
			++m_BeginRenderingCount;
		}
		void EndRendering() noexcept override
		{
			GGLAB_ASSERT(m_IsRendering);
			m_IsRendering = false;
			++m_EndRenderingCount;
		}
		bool IsRendering() const noexcept override { return m_IsRendering; }
		void ClearColorAttachment(uint32_t, const std::array<float, 4>&) noexcept override {}
		void ClearDepthAttachment(float, std::optional<uint8_t>) noexcept override
		{
		}
		void SetViewport(const RHIViewport&) noexcept override {}
		void SetScissorRect(const RHIScissorRect&) noexcept override {}
		void SetPrimitiveTopology(RHIPrimitiveTopology) noexcept override {}
		bool SetConstantBuffer(uint32_t parameterIndex, const RHIConstantBufferBinding& binding) noexcept override
		{
			InvalidateScopedConstant(parameterIndex);
			if (!ValidateConstantBinding(binding))
			{
				return false;
			}
			m_ConstantBindings.push_back({ parameterIndex, binding.m_Buffer, binding.m_OffsetInBytes });
			return true;
		}
		using RHIGraphicsCommandContext::SetReadOnlyBuffer;
		bool SetReadOnlyBufferBinding(uint32_t, const RHIReadOnlyBufferBinding& binding) noexcept override
		{
			m_ReadOnlyBindings.push_back(binding);
			return true;
		}
		std::vector<RHIReadOnlyBufferBinding> m_ReadOnlyBindings;
		void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
		void SetInlineConstantWords(uint32_t parameterIndex, std::span<const uint32_t> words, uint32_t offset) noexcept override
		{
			m_LastInlineParameter = parameterIndex;
			m_LastInlineOffset = offset;
			m_LastInlineWords.assign(words.begin(), words.end());
		}
		void SetVertexBuffers(uint32_t, std::span<const RHIVertexBufferBinding>) noexcept override
		{
		}
		void SetIndexBuffer(const RHIIndexBufferBinding&) noexcept override {}
		void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) noexcept override
		{
			m_CommandCount += ValidateConstantBindingReadiness() ? 1u : 0u;
		}
		void Draw(uint32_t, uint32_t, uint32_t, uint32_t) noexcept override
		{
			m_CommandCount += ValidateConstantBindingReadiness() ? 1u : 0u;
		}
		uint32_t m_CommandCount = 0;

		std::vector<RecordedConstantBinding> m_ConstantBindings;
		std::vector<uint32_t> m_LastInlineWords;
		uint32_t m_LastInlineParameter = 0;
		uint32_t m_LastInlineOffset = 0;
		GpuProfileFrameSnapshot m_Profile;
		uint32_t m_BeginProfileCount = 0;
		uint32_t m_EndProfileCount = 0;
		uint32_t m_FlushBarrierCount = 0;
		uint32_t m_TextureBarrierCount = 0;
		uint32_t m_BufferBarrierCount = 0;
		uint32_t m_CopyBufferCount = 0;
		uint32_t m_BeginRenderingCount = 0;
		uint32_t m_EndRenderingCount = 0;
		bool m_IsRendering = false;
		std::vector<RHITextureBarrier> m_TextureBarriers;
	};

	class RecordingComputeCommandContext final : public RHIComputeCommandContext
	{
	public:
		void BeginRecordingScope(RHIUploadWriter& writer) noexcept { BindUploadWriter(&writer); }
		void EndRecordingScope() noexcept { BindUploadWriter(nullptr); }
		RHICommandContextHandle GetHandle() const noexcept override { return {}; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Graphics; }
		void TrackTextureUse(RHITextureHandle) noexcept override {}
		void TrackBufferUse(RHIBufferHandle) noexcept override {}
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override
		{
			m_TextureBarrierCount += static_cast<uint32_t>(barriers.size());
		}
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override
		{
			m_BufferBarrierCount += static_cast<uint32_t>(barriers.size());
		}
		void FlushBarriers() noexcept override { ++m_FlushBarrierCount; }
		void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override
		{
		}
		void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override {}
		void BeginGpuProfileScope(GpuProfileScope scope) noexcept override
		{
			++m_BeginProfileCount;
			AccumulateGpuProfileSample(m_Profile, scope, 1.0);
		}
		void EndGpuProfileScope() noexcept override { ++m_EndProfileCount; }
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override
		{
			InvalidatePipelineBindings();
			if (pipeline.IsValid())
			{
				ConfirmBindingPipeline(pipeline);
			}
		}
		void SetDescriptorTable(const RHIDescriptorTableBinding&) noexcept override {}
		bool SetConstantBuffer(uint32_t parameterIndex, const RHIConstantBufferBinding& binding) noexcept override
		{
			InvalidateScopedConstant(parameterIndex);
			if (!ValidateConstantBinding(binding))
			{
				return false;
			}
			m_ConstantBindings.push_back({ parameterIndex, binding.m_Buffer, binding.m_OffsetInBytes });
			return true;
		}
		using RHIComputeCommandContext::SetReadOnlyBuffer;
		bool SetReadOnlyBufferBinding(uint32_t, const RHIReadOnlyBufferBinding& binding) noexcept override
		{
			m_ReadOnlyBindings.push_back(binding);
			return true;
		}
		std::vector<RHIReadOnlyBufferBinding> m_ReadOnlyBindings;
		void SetReadOnlyBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
		void SetReadWriteBuffer(uint32_t, RHIBufferHandle, uint64_t) noexcept override {}
		void SetInlineConstantWords(uint32_t parameterIndex, std::span<const uint32_t> words, uint32_t offset) noexcept override
		{
			m_LastInlineParameter = parameterIndex;
			m_LastInlineOffset = offset;
			m_LastInlineWords.assign(words.begin(), words.end());
		}
		void Dispatch(uint32_t, uint32_t, uint32_t) noexcept override
		{
			m_CommandCount += ValidateConstantBindingReadiness() ? 1u : 0u;
		}
		uint32_t m_CommandCount = 0;

		std::vector<RecordedConstantBinding> m_ConstantBindings;
		std::vector<uint32_t> m_LastInlineWords;
		uint32_t m_LastInlineParameter = 0;
		uint32_t m_LastInlineOffset = 0;
		GpuProfileFrameSnapshot m_Profile;
		uint32_t m_BeginProfileCount = 0;
		uint32_t m_EndProfileCount = 0;
		uint32_t m_FlushBarrierCount = 0;
		uint32_t m_TextureBarrierCount = 0;
		uint32_t m_BufferBarrierCount = 0;
	};

	class BindingTestPipelineSystem final : public RHIPipelineSystem
	{
	public:
		RHIBindingLayoutHandle CreateBindingLayout(const RHIBindingLayoutDesc&) noexcept override { return { 1, 1 }; }
		RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineCreateInfo&) noexcept override { return {}; }
		RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineCreateInfo&) noexcept override { return {}; }
		bool IsAlive(RHIBindingLayoutHandle handle) const noexcept override { return handle.IsValid(); }
		bool IsAlive(RHIPipelineHandle handle) const noexcept override { return handle.IsValid(); }
		uint64_t GetRevision() const noexcept override { return 1; }
		void Clear() noexcept override {}
	};

	class BindingTestResolver final : public RenderPipelineResolver
	{
	public:
		RHIPipelineHandle Resolve(GraphicsPipelineSlot&, const GraphicsPhysicalPipelineKey&,
			const RenderPassInfo&) noexcept override { return m_Pipeline; }
		RHIPipelineHandle Resolve(ComputePipelineSlot&, const ComputePipelineRecipe&,
			const RenderPassInfo&) noexcept override { return m_Pipeline; }
		void GetPipelineUsages(RHIPipelineHandle, std::vector<RenderPassInfo>&) const noexcept override {}
		RHIPipelineHandle m_Pipeline{ 1, 1 };
	};

	template <typename Context = RHIGraphicsCommandContext, typename Layout>
	ResolvedRenderPipeline ResolveBindingTestPipeline(const Layout& layout, uint32_t index = 1) noexcept
	{
		BindingTestResolver resolver;
		resolver.m_Pipeline = RHIPipelineHandle{ index, 1 };
		if constexpr (std::derived_from<Context, RHIComputeCommandContext>)
		{
			ComputePipelineSlot slot;
			const ComputePipelineRecipe recipe{ .m_BindingLayout = layout.GetNativeLayout() };
			if constexpr (std::same_as<Layout, ResolvedRenderLayout>)
			{
				return layout.Resolve(resolver, slot, recipe, {});
			}
			else
			{
				return layout.ResolvePipeline(resolver, slot, recipe, {});
			}
		}
		else
		{
			GraphicsPipelineSlot slot;
			const GraphicsPhysicalPipelineKey recipe{ .m_BindingLayout = layout.GetNativeLayout() };
			if constexpr (std::same_as<Layout, ResolvedRenderLayout>)
			{
				return layout.Resolve(resolver, slot, recipe, {});
			}
			else
			{
				return layout.ResolvePipeline(resolver, slot, recipe, {});
			}
		}
	}

	class BindingTestRHIContext final : public RHIContext
	{
	public:
		RHIDevice& GetDevice() noexcept override { std::terminate(); }
		const RHIDevice& GetDevice() const noexcept override { std::terminate(); }
		RHISwapChain& GetSwapChain() noexcept override { std::terminate(); }
		const RHISwapChain& GetSwapChain() const noexcept override { std::terminate(); }
		TransferManager& GetTransferManager() noexcept override { std::terminate(); }
		RHIPipelineSystem& GetPipelineSystem() noexcept override { return m_Pipelines; }
		GpuProfiler* GetGpuProfiler() noexcept override { return nullptr; }
		RHIFrameBeginResult BeginFrame() noexcept override { return RHIFrameBeginResult::Unavailable(); }
		RHIFrameEndResult EndFrame(RHIFrameContext&) noexcept override { return RHIFrameEndResult::Fatal(); }
		RHIFrameEndResult AbortFrame(RHIFrameContext&) noexcept override { return RHIFrameEndResult::Fatal(); }
		bool WaitForFence(RHIQueueType, const RHIFencePoint&) noexcept override { return true; }
		void Resize(uint32_t, uint32_t) noexcept override {}
		RHIGpuWaitResult WaitIdleFor(uint32_t) noexcept override { return RHIGpuWaitResult::Completed; }
		void RetireCompletedWork() noexcept override {}
		bool IsPoisoned() const noexcept override { return false; }
		bool IsDeviceLost() const noexcept override { return false; }
		uint32_t GetFrameSlotCount() const noexcept override { return 2; }
		BindingTestPipelineSystem m_Pipelines;
	};

	class BindingTestPresentation final : public RenderPresentationAccess
	{
	public:
		RHIContext* GetRHIContext() const noexcept override { return &m_Context; }
		RHIDevice* GetDevice() const noexcept override { return nullptr; }
		RHISwapChain* GetSwapChain() const noexcept override { return nullptr; }
		const std::array<float, 4>& GetBackBufferClearColor() const noexcept override { return m_Clear; }
		RHIFencePoint GetLastSubmittedFencePoint() const noexcept override { return {}; }
		mutable BindingTestRHIContext m_Context;
		std::array<float, 4> m_Clear{};
	};

	class RecordingTransferContext final : public RHITransferContext
	{
	public:
		RHICommandContextHandle GetHandle() const noexcept override { return {}; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Copy; }
		void TrackTextureUse(RHITextureHandle) noexcept override {}
		void TrackBufferUse(RHIBufferHandle) noexcept override {}
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override
		{
			m_TextureBarriers.insert(m_TextureBarriers.end(), barriers.begin(), barriers.end());
		}
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override
		{
			m_BufferBarriers.insert(m_BufferBarriers.end(), barriers.begin(), barriers.end());
		}
		void FlushBarriers() noexcept override {}
		void CopyBuffer(RHIBufferHandle, uint64_t, RHIBufferHandle, uint64_t, uint64_t) noexcept override
		{
		}
		void CopyTextureToBuffer(const RHITextureToBufferCopy&) noexcept override {}
		void Begin() noexcept override {}
		RHIFencePoint Submit(bool) noexcept override
		{
			++m_SubmitCallCount;
			return m_FailSubmit ? RHIFencePoint{} : RHIFencePoint{ RHIFenceHandle{ 1, 1 }, 7 };
		}
		void Abort() noexcept override { ++m_AbortCallCount; }
		void ReclaimCompleted() noexcept override {}
		bool UploadBuffer(const void*, uint64_t, RHIBufferHandle, uint64_t) noexcept override
		{
			return !m_FailUploads && m_UploadBufferCallCount++ < m_SucceedingUploadBufferCount;
		}
		bool UploadTexture(const RHITextureUploadData&, RHITextureHandle) noexcept override
		{
			return !m_FailUploads;
		}
		RHITextureReadbackRequest ReadbackTexture(RHITextureHandle, const RHITextureDesc& desc) noexcept override
		{
			if (m_FailReadbacks)
			{
				return {};
			}
			RHITextureReadbackRequest request{};
			request.m_Buffer = RHIBufferOwner(nullptr, RHIBufferHandle{ 9, 1 });
			request.m_BufferSizeInBytes = 4;
			request.m_TextureDesc = desc;
			request.m_Subresources.push_back({});
			return request;
		}

		std::vector<RHITextureBarrier> m_TextureBarriers;
		std::vector<RHIBufferBarrier> m_BufferBarriers;
		bool m_FailUploads = false;
		bool m_FailReadbacks = true;
		bool m_FailSubmit = false;
		uint32_t m_SucceedingUploadBufferCount = std::numeric_limits<uint32_t>::max();
		uint32_t m_UploadBufferCallCount = 0;
		uint32_t m_SubmitCallCount = 0;
		uint32_t m_AbortCallCount = 0;
	};

	[[nodiscard]] inline bool NearlyEqual(
		float lhs, float rhs, float tolerance = ProjectionTolerance) noexcept
	{
		return std::abs(lhs - rhs) <= tolerance;
	}

	[[nodiscard]] inline bool NearlyEqual(
		const Vector2& lhs, const Vector2& rhs, float tolerance = ProjectionTolerance) noexcept
	{
		return NearlyEqual(lhs.m_X, rhs.m_X, tolerance) &&
			NearlyEqual(lhs.m_Y, rhs.m_Y, tolerance);
	}

	[[nodiscard]] inline bool NearlyEqual(
		const Vector3& lhs, const Vector3& rhs, float tolerance = PositionTolerance) noexcept
	{
		return NearlyEqual(lhs.m_X, rhs.m_X, tolerance) &&
			NearlyEqual(lhs.m_Y, rhs.m_Y, tolerance) &&
			NearlyEqual(lhs.m_Z, rhs.m_Z, tolerance);
	}

	[[nodiscard]] inline ProjectedPosition ProjectPosition(const Vector3& position, const Matrix& transform) noexcept
	{
		const Vector4 clipPosition = math::Transform(Vector4(position, 1.0f), transform);
		const float inverseW = 1.0f / clipPosition.m_W;
		const Vector2 ndc(clipPosition.m_X * inverseW, clipPosition.m_Y * inverseW);
		return ProjectedPosition{
			.m_UV = screen_space::NDCToUV(ndc),
			.m_RawDepth = clipPosition.m_Z * inverseW,
		};
	}
}
