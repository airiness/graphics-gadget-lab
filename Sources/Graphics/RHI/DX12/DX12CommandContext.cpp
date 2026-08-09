#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12PipelineState.h"
#include "Graphics/RHI/DX12/DX12PipelineSystem.h"
#include "Graphics/RHI/DX12/DX12RootSignature.h"
#include "Graphics/RHI/DX12/DX12GpuProfiler.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

#include <algorithm>

namespace gglab
{
	namespace
	{
		struct RenderingAttachmentProperties
		{
			uint64_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_SampleCount = 0;
		};

		[[nodiscard]] bool AreAttachmentPropertiesCompatible(
			const RenderingAttachmentProperties& lhs,
			const RenderingAttachmentProperties& rhs) noexcept
		{
			return lhs.m_Width == rhs.m_Width && lhs.m_Height == rhs.m_Height &&
				lhs.m_SampleCount == rhs.m_SampleCount;
		}

		D3D12_PRIMITIVE_TOPOLOGY ToD3D12PrimitiveTopology(RHIPrimitiveTopology topology) noexcept
		{
			switch (topology)
			{
			case RHIPrimitiveTopology::PointList:
				return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
			case RHIPrimitiveTopology::LineList:
				return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
			case RHIPrimitiveTopology::LineStrip:
				return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
			case RHIPrimitiveTopology::TriangleList:
				return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			case RHIPrimitiveTopology::TriangleStrip:
				return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
			case RHIPrimitiveTopology::Unknown:
			default:
				return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			}
		}

		bool ResolveRenderingAttachment(DX12Device& device,
			const RHIRenderingAttachment& attachment, RHITextureViewType expectedType,
			DX12DescriptorView& nativeView, RenderingAttachmentProperties& properties,
			RHIFormat& format) noexcept
		{
			RHITextureViewKey key{};
			if (!device.ResolveTextureViewInfo(attachment.m_View, nativeView, key) ||
				key.m_Desc.m_Type != expectedType)
			{
				return false;
			}

			const DX12Texture* texture = device.ResolveTexture(key.m_Texture);
			if (!texture)
			{
				return false;
			}

			const D3D12_RESOURCE_DESC desc = texture->GetDesc();
			const uint32_t mip = key.m_Desc.m_Subresources.m_BaseMip;
			if (mip >= desc.MipLevels)
			{
				return false;
			}

			properties.m_Width = std::max<uint64_t>(1, desc.Width >> mip);
			properties.m_Height = std::max<uint32_t>(1, desc.Height >> mip);
			properties.m_SampleCount = desc.SampleDesc.Count;
			format = key.m_Desc.m_Format == RHIFormat::Unknown ?
				ToRHIFormat(desc.Format) : key.m_Desc.m_Format;
			if (format == RHIFormat::Unknown)
			{
				return false;
			}
			return true;
		}
	}

	DX12CommandContext::DX12CommandContext(
		DX12Device* device, DX12CommandList* commandList, RHIQueueType queueType) noexcept :
		m_Handle(AllocateRHICommandContextHandle()), m_Device(device),
		m_CommandList(commandList), m_QueueType(queueType)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "DX12CommandContext requires a valid DX12Device.");
		GGLAB_ASSERT_MSG(
			m_CommandList != nullptr, "DX12CommandContext requires a valid DX12CommandList.");
	}

	ID3D12GraphicsCommandList* DX12CommandContext::Get() const noexcept
	{
		return m_CommandList ? m_CommandList->Get() : nullptr;
	}

	void DX12CommandContext::ClearTrackedResourceUses() noexcept
	{
		GGLAB_ASSERT_MSG(!m_IsRendering,
			"DX12 command context reset requires the active rendering scope to be ended.");
		m_IsRendering = false;
		m_UsedBuffers.clear();
		m_UsedTextures.clear();
	}

	void DX12CommandContext::BeginRenderingScope() noexcept
	{
		GGLAB_ASSERT_MSG(!m_IsRendering,
			"DX12CommandContext::BeginRenderingScope does not allow nested rendering scopes.");
		if (!m_IsRendering)
		{
			m_IsRendering = true;
		}
	}

	void DX12CommandContext::EndRenderingScope() noexcept
	{
		GGLAB_ASSERT_MSG(m_IsRendering,
			"DX12CommandContext::EndRenderingScope requires an active rendering scope.");
		m_IsRendering = false;
	}

	void DX12CommandContext::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		GGLAB_ASSERT_MSG(!m_IsRendering,
			"Texture barriers cannot be encoded inside an active rendering scope.");
		if (m_IsRendering)
		{
			return;
		}
		if (!m_Device || !m_CommandList || barriers.empty())
		{
			return;
		}

		for (const RHITextureBarrier& barrier : barriers)
		{
			if (!IsRHIResourceStateValid(
				barrier.m_Before, RHIResourceStateUsage::TextureBarrierBefore) ||
				!IsRHIResourceStateValid(
					barrier.m_After, RHIResourceStateUsage::TextureBarrierAfter))
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12CommandContext::TextureBarrier rejected an invalid resource state.");
				continue;
			}
			DX12Texture* texture = m_Device->ResolveTexture(barrier.m_Texture);
			if (!texture)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12CommandContext::TextureBarrier received a non-live texture handle.");
				continue;
			}

			m_CommandList->AddTextureBarrier(
				BuildD3D12TextureBarrier(barrier, texture->Get(), texture->GetDesc()));
			TrackTextureUse(barrier.m_Texture);
		}
	}

	void DX12CommandContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		GGLAB_ASSERT_MSG(!m_IsRendering,
			"Buffer barriers cannot be encoded inside an active rendering scope.");
		if (m_IsRendering)
		{
			return;
		}
		if (!m_Device || !m_CommandList || barriers.empty())
		{
			return;
		}

		for (const RHIBufferBarrier& barrier : barriers)
		{
			if (!IsRHIResourceStateValid(barrier.m_Before, RHIResourceStateUsage::Buffer) ||
				!IsRHIResourceStateValid(barrier.m_After, RHIResourceStateUsage::Buffer))
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12CommandContext::BufferBarrier rejected an invalid resource state.");
				continue;
			}
			DX12Buffer* buffer = m_Device->ResolveBuffer(barrier.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12CommandContext::BufferBarrier received a non-live buffer handle.");
				continue;
			}

			m_CommandList->AddBufferBarrier(
				CD3DX12_BUFFER_BARRIER(ToD3D12BarrierSync(barrier.m_Before.m_Stages),
					ToD3D12BarrierSync(barrier.m_After.m_Stages),
					ToD3D12BarrierAccess(barrier.m_Before.m_Access),
					ToD3D12BarrierAccess(barrier.m_After.m_Access), buffer->Get()));
			TrackBufferUse(barrier.m_Buffer);
		}
	}

	void DX12CommandContext::FlushBarriers() noexcept
	{
		GGLAB_ASSERT_MSG(!m_IsRendering,
			"Barrier flushing cannot occur inside an active rendering scope.");
		if (m_IsRendering)
		{
			return;
		}
		if (m_CommandList)
		{
			m_CommandList->FlushBarriers();
		}
	}

	void DX12CommandContext::CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
		RHIBufferHandle source, uint64_t sourceOffset, uint64_t sizeInBytes) noexcept
	{
		GGLAB_ASSERT_MSG(!m_IsRendering,
			"Buffer copies cannot be encoded inside an active rendering scope.");
		if (m_IsRendering)
		{
			return;
		}
		DX12Buffer* destinationBuffer = m_Device ? m_Device->ResolveBuffer(destination) : nullptr;
		DX12Buffer* sourceBuffer = m_Device ? m_Device->ResolveBuffer(source) : nullptr;
		if (!destinationBuffer || !sourceBuffer || sizeInBytes == 0 ||
			destinationOffset > destinationBuffer->SizeInBytes() ||
			sizeInBytes > destinationBuffer->SizeInBytes() - destinationOffset ||
			sourceOffset > sourceBuffer->SizeInBytes() ||
			sizeInBytes > sourceBuffer->SizeInBytes() - sourceOffset)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12CommandContext::CopyBuffer received an invalid buffer or copy range.");
			return;
		}

		Get()->CopyBufferRegion(destinationBuffer->Get(), destinationOffset, sourceBuffer->Get(),
			sourceOffset, sizeInBytes);
		TrackBufferUse(destination);
		TrackBufferUse(source);
	}

	void DX12CommandContext::TrackBufferUse(RHIBufferHandle buffer) noexcept
	{
		if (std::ranges::find(m_UsedBuffers, buffer) == m_UsedBuffers.end())
		{
			m_UsedBuffers.push_back(buffer);
		}
	}

	void DX12CommandContext::TrackTextureUse(RHITextureHandle texture) noexcept
	{
		if (std::ranges::find(m_UsedTextures, texture) == m_UsedTextures.end())
		{
			m_UsedTextures.push_back(texture);
		}
	}

	DX12GraphicsCommandContext::DX12GraphicsCommandContext(DX12Device* device,
		DX12PipelineSystem* pipelineSystem, DX12CommandList* commandList) noexcept :
		m_Backend(device, commandList, RHIQueueType::Graphics), m_PipelineSystem(pipelineSystem)
	{
	}

	void DX12GraphicsCommandContext::TextureBarrier(
		std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_Backend.TextureBarrier(barriers);
	}

	void DX12GraphicsCommandContext::BufferBarrier(
		std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_Backend.BufferBarrier(barriers);
	}

	void DX12GraphicsCommandContext::SetPipeline(RHIPipelineHandle pipeline) noexcept
	{
		DX12PipelineState* pipelineState = nullptr;
		DX12RootSignature* rootSignature = nullptr;
		RHIGraphicsPipelineDesc pipelineDesc{};
		if (!m_PipelineSystem ||
			!m_PipelineSystem->ResolveGraphicsPipeline(
				pipeline, pipelineState, rootSignature, pipelineDesc))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12GraphicsCommandContext::SetPipeline received an invalid pipeline handle.");
			m_CurrentPipelineDesc.reset();
			return;
		}

		if (m_CurrentRootSignature != rootSignature)
		{
			m_Backend.GetCommandList()->SetGraphicsRootSignature(*rootSignature);
			m_CurrentRootSignature = rootSignature;
		}
		m_Backend.GetCommandList()->SetPipelineState(*pipelineState);
		m_CurrentPipelineDesc = pipelineDesc;
	}

	void DX12GraphicsCommandContext::SetDescriptorTable(
		const RHIDescriptorTableBinding& binding) noexcept
	{
		GGLAB_ASSERT_MSG(m_CurrentRootSignature,
			"SetPipeline must bind a graphics root signature before root arguments.");
		const D3D12_GPU_DESCRIPTOR_HANDLE table =
			m_Backend.GetDevice()->ResolveShaderVisibleDescriptor(
				binding.m_HeapType, binding.m_TableIndex);
		if (table.ptr == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12GraphicsCommandContext::SetDescriptorTable received an invalid descriptor table binding.");
			return;
		}
		m_Backend.Get()->SetGraphicsRootDescriptorTable(binding.m_ParameterIndex, table);
	}

	void DX12GraphicsCommandContext::BeginRendering(const RHIRenderingInfo& info) noexcept
	{
		GGLAB_ASSERT_MSG(!m_Backend.IsRendering(),
			"DX12GraphicsCommandContext::BeginRendering does not allow nested scopes.");
		if (m_Backend.IsRendering())
		{
			return;
		}

		DX12Device* device = m_Backend.GetDevice();
		GGLAB_ASSERT_NOT_NULL(device);
		if (!device)
		{
			return;
		}
		GGLAB_ASSERT_MSG(info.m_ColorAttachments.size() <= RHIGraphicsPipelineDesc::MaxRenderTargets,
			"BeginRendering color attachment count exceeds the RHI limit.");
		if (info.m_ColorAttachments.size() > RHIGraphicsPipelineDesc::MaxRenderTargets)
		{
			return;
		}

		std::vector<DX12DescriptorView> nativeRenderTargets;
		nativeRenderTargets.reserve(info.m_ColorAttachments.size());
		std::optional<RenderingAttachmentProperties> attachmentProperties;
		RHIRenderingSignature renderingSignature{};
		for (const RHIRenderingAttachment& attachment : info.m_ColorAttachments)
		{
			DX12DescriptorView nativeView{};
			RenderingAttachmentProperties properties{};
			RHIFormat format = RHIFormat::Unknown;
			const bool resolved = ResolveRenderingAttachment(*device, attachment,
				RHITextureViewType::RenderTarget, nativeView, properties, format);
			GGLAB_ASSERT_MSG(resolved,
				"A rendering color attachment must resolve to a live render-target view.");
			if (!resolved)
			{
				return;
			}
			const bool compatible = !attachmentProperties ||
				AreAttachmentPropertiesCompatible(*attachmentProperties, properties);
			GGLAB_ASSERT_MSG(compatible,
				"All rendering attachments must have matching mip extents and sample counts.");
			if (!compatible)
			{
				return;
			}
			attachmentProperties = properties;
			renderingSignature.m_ColorFormats[renderingSignature.m_ColorAttachmentCount++] =
				format;
			nativeRenderTargets.push_back(nativeView);
		}

		GGLAB_ASSERT_MSG(!nativeRenderTargets.empty() || info.m_DepthAttachment.has_value(),
			"DX12GraphicsCommandContext::BeginRendering requires an attachment.");
		if (nativeRenderTargets.empty() && !info.m_DepthAttachment)
		{
			return;
		}

		std::optional<DX12DescriptorView> nativeDepth;
		if (info.m_DepthAttachment)
		{
			DX12DescriptorView nativeView{};
			RenderingAttachmentProperties properties{};
			RHIFormat format = RHIFormat::Unknown;
			const bool resolved = ResolveRenderingAttachment(*device, *info.m_DepthAttachment,
				RHITextureViewType::DepthStencil, nativeView, properties, format);
			GGLAB_ASSERT_MSG(resolved,
				"A rendering depth attachment must resolve to a live depth-stencil view.");
			if (!resolved)
			{
				return;
			}
			const bool compatible = !attachmentProperties ||
				AreAttachmentPropertiesCompatible(*attachmentProperties, properties);
			GGLAB_ASSERT_MSG(compatible,
				"All rendering attachments must have matching mip extents and sample counts.");
			if (!compatible)
			{
				return;
			}
			renderingSignature.m_DepthFormat = format;
			attachmentProperties = properties;
			nativeDepth = nativeView;
		}
		renderingSignature.m_SampleCount = attachmentProperties->m_SampleCount;

		m_Backend.BeginRenderingScope();
		m_ActiveRenderingSignature = renderingSignature;
		m_ActiveColorAttachments = nativeRenderTargets;
		m_ActiveDepthAttachment = nativeDepth;
		if (nativeDepth)
		{
			m_Backend.GetCommandList()->SetRenderTargets(nativeRenderTargets, *nativeDepth);
		}
		else
		{
			m_Backend.GetCommandList()->SetRenderTargets(nativeRenderTargets);
		}
	}

	void DX12GraphicsCommandContext::EndRendering() noexcept
	{
		m_Backend.EndRenderingScope();
		m_ActiveRenderingSignature.reset();
		m_ActiveColorAttachments.clear();
		m_ActiveDepthAttachment.reset();
	}

	bool DX12GraphicsCommandContext::ValidateActivePipelineCompatibility(
		std::string_view operation) const noexcept
	{
		const bool compatible = m_CurrentPipelineDesc && m_ActiveRenderingSignature &&
			IsGraphicsPipelineCompatible(*m_CurrentPipelineDesc, *m_ActiveRenderingSignature);
		if (compatible)
		{
			return true;
		}

		if (!m_CurrentPipelineDesc || !m_ActiveRenderingSignature)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"{} requires a bound graphics pipeline and an active rendering signature.", operation);
			return false;
		}

		const auto& pipeline = *m_CurrentPipelineDesc;
		const auto& rendering = *m_ActiveRenderingSignature;
		GGLAB_LOG_GRAPHICS_ERROR(
			"{} pipeline/rendering mismatch: colorCount={}/{}, depth={}/{}, samples={}/{}.",
			operation, pipeline.m_RenderTargetCount, rendering.m_ColorAttachmentCount,
			static_cast<uint32_t>(pipeline.m_DepthStencilFormat),
			static_cast<uint32_t>(rendering.m_DepthFormat), pipeline.m_SampleCount,
			rendering.m_SampleCount);
		for (uint32_t index = 0;
			index < std::max(pipeline.m_RenderTargetCount, rendering.m_ColorAttachmentCount); ++index)
		{
			GGLAB_LOG_GRAPHICS_ERROR("{} color format {}: pipeline={}, rendering={}.", operation,
				index, static_cast<uint32_t>(pipeline.m_RenderTargetFormats[index]),
				static_cast<uint32_t>(rendering.m_ColorFormats[index]));
		}
		return false;
	}

	void DX12GraphicsCommandContext::ClearColorAttachment(
		uint32_t colorAttachmentIndex, const std::array<float, 4>& color) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_Backend.IsRendering(), "ClearColorAttachment requires an active rendering scope.");
		GGLAB_ASSERT_MSG(colorAttachmentIndex < m_ActiveColorAttachments.size(),
			"ClearColorAttachment index must identify an active color attachment.");
		if (!m_Backend.IsRendering() || colorAttachmentIndex >= m_ActiveColorAttachments.size())
		{
			return;
		}
		m_Backend.Get()->ClearRenderTargetView(
			m_ActiveColorAttachments[colorAttachmentIndex].m_CpuHandle, color.data(), 0, nullptr);
	}

	void DX12GraphicsCommandContext::ClearDepthAttachment(
		float depth, std::optional<uint8_t> stencil) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_Backend.IsRendering(), "ClearDepthAttachment requires an active rendering scope.");
		GGLAB_ASSERT_MSG(m_ActiveDepthAttachment.has_value(),
			"ClearDepthAttachment requires an active depth attachment.");
		if (!m_Backend.IsRendering() || !m_ActiveDepthAttachment)
		{
			return;
		}
		m_Backend.GetCommandList()->ClearDepthStencil(*m_ActiveDepthAttachment, depth, stencil);
	}

	void DX12GraphicsCommandContext::SetViewport(const RHIViewport& viewport) noexcept
	{
		D3D12_VIEWPORT nativeViewport{
			.TopLeftX = viewport.m_X,
			.TopLeftY = viewport.m_Y,
			.Width = viewport.m_Width,
			.Height = viewport.m_Height,
			.MinDepth = viewport.m_MinDepth,
			.MaxDepth = viewport.m_MaxDepth,
		};
		m_Backend.Get()->RSSetViewports(1, &nativeViewport);
	}

	void DX12GraphicsCommandContext::SetScissorRect(const RHIScissorRect& rect) noexcept
	{
		D3D12_RECT nativeRect{ rect.m_Left, rect.m_Top, rect.m_Right, rect.m_Bottom };
		m_Backend.Get()->RSSetScissorRects(1, &nativeRect);
	}

	void DX12GraphicsCommandContext::SetPrimitiveTopology(RHIPrimitiveTopology topology) noexcept
	{
		const D3D12_PRIMITIVE_TOPOLOGY nativeTopology = ToD3D12PrimitiveTopology(topology);
		GGLAB_ASSERT_MSG(
			nativeTopology != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED, "Invalid RHI primitive topology.");
		m_Backend.GetCommandList()->SetPrimitiveTopology(nativeTopology);
	}

	void DX12GraphicsCommandContext::SetConstantBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		GGLAB_ASSERT_MSG(m_CurrentRootSignature,
			"SetPipeline must bind a graphics root signature before root arguments.");
		DX12Buffer* nativeBuffer = m_Backend.GetDevice()->ResolveBuffer(buffer);
		if (!nativeBuffer || offset >= nativeBuffer->SizeInBytes())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12GraphicsCommandContext::SetConstantBuffer received an invalid buffer binding.");
			return;
		}
		m_Backend.GetCommandList()->SetGraphicsConstantBuffer(
			parameterIndex, nativeBuffer->GPUVirtualAddress() + offset);
		m_Backend.TrackBufferUse(buffer);
	}

	void DX12GraphicsCommandContext::SetReadOnlyBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		GGLAB_ASSERT_MSG(m_CurrentRootSignature,
			"SetPipeline must bind a graphics root signature before root arguments.");
		DX12Buffer* nativeBuffer = m_Backend.GetDevice()->ResolveBuffer(buffer);
		if (!nativeBuffer || offset >= nativeBuffer->SizeInBytes())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12GraphicsCommandContext::SetReadOnlyBuffer received an invalid buffer binding.");
			return;
		}
		m_Backend.Get()->SetGraphicsRootShaderResourceView(
			parameterIndex, nativeBuffer->GPUVirtualAddress() + offset);
		m_Backend.TrackBufferUse(buffer);
	}

	void DX12GraphicsCommandContext::SetPushConstants(
		uint32_t parameterIndex, std::span<const uint32_t> values, uint32_t destOffset) noexcept
	{
		GGLAB_ASSERT_MSG(m_CurrentRootSignature,
			"SetPipeline must bind a graphics root signature before root arguments.");
		m_Backend.GetCommandList()->SetGraphicsRoot32BitConstants(
			parameterIndex, values, destOffset);
	}

	void DX12GraphicsCommandContext::SetVertexBuffers(
		uint32_t startSlot, std::span<const RHIVertexBufferBinding> bindings) noexcept
	{
		if (bindings.empty())
		{
			return;
		}

		DX12Device* device = m_Backend.GetDevice();
		GGLAB_ASSERT_NOT_NULL(device);

		std::vector<D3D12_VERTEX_BUFFER_VIEW> nativeBindings;
		nativeBindings.reserve(bindings.size());
		for (const RHIVertexBufferBinding& binding : bindings)
		{
			DX12Buffer* buffer = device->ResolveBuffer(binding.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12GraphicsCommandContext::SetVertexBuffers received a non-live buffer handle.");
				continue;
			}

			D3D12_VERTEX_BUFFER_VIEW view{};
			view.BufferLocation = buffer->GPUVirtualAddress() + binding.m_Offset;
			view.StrideInBytes = binding.m_Stride;
			view.SizeInBytes = binding.m_SizeInBytes;
			nativeBindings.push_back(view);
			m_Backend.TrackBufferUse(binding.m_Buffer);
		}

		if (!nativeBindings.empty())
		{
			m_Backend.GetCommandList()->SetVertexBuffers(startSlot,
				std::span<D3D12_VERTEX_BUFFER_VIEW>(nativeBindings.data(), nativeBindings.size()));
		}
	}

	void DX12GraphicsCommandContext::SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept
	{
		DX12Device* device = m_Backend.GetDevice();
		GGLAB_ASSERT_NOT_NULL(device);

		DX12Buffer* buffer = device->ResolveBuffer(binding.m_Buffer);
		if (!buffer)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12GraphicsCommandContext::SetIndexBuffer received a non-live buffer handle.");
			return;
		}

		D3D12_INDEX_BUFFER_VIEW view{};
		view.BufferLocation = buffer->GPUVirtualAddress() + binding.m_Offset;
		view.SizeInBytes = binding.m_SizeInBytes;
		view.Format = ToDXGIFormat(binding.m_Format);
		m_Backend.GetCommandList()->SetIndexBuffer(view);
		m_Backend.TrackBufferUse(binding.m_Buffer);
	}

	void DX12GraphicsCommandContext::DrawIndexed(uint32_t indexCount, uint32_t instanceCount,
		uint32_t startIndexLocation, int32_t baseVertexLocation,
		uint32_t startInstanceLocation) noexcept
	{
		GGLAB_ASSERT_MSG(m_Backend.IsRendering(), "DrawIndexed requires an active rendering scope.");
		if (!m_Backend.IsRendering())
		{
			return;
		}
		const bool compatible = ValidateActivePipelineCompatibility("DrawIndexed");
		GGLAB_ASSERT_MSG(compatible,
			"DrawIndexed requires a pipeline compatible with the active rendering signature.");
		if (!compatible)
		{
			return;
		}
		m_Backend.Get()->DrawIndexedInstanced(indexCount, instanceCount, startIndexLocation,
			baseVertexLocation, startInstanceLocation);
	}

	void DX12GraphicsCommandContext::Draw(uint32_t vertexCount, uint32_t instanceCount,
		uint32_t startVertexLocation, uint32_t startInstanceLocation) noexcept
	{
		GGLAB_ASSERT_MSG(m_Backend.IsRendering(), "Draw requires an active rendering scope.");
		if (!m_Backend.IsRendering())
		{
			return;
		}
		const bool compatible = ValidateActivePipelineCompatibility("Draw");
		GGLAB_ASSERT_MSG(compatible,
			"Draw requires a pipeline compatible with the active rendering signature.");
		if (!compatible)
		{
			return;
		}
		m_Backend.Get()->DrawInstanced(
			vertexCount, instanceCount, startVertexLocation, startInstanceLocation);
	}

	void DX12GraphicsCommandContext::BeginGpuProfileScope(std::string_view name) noexcept
	{
		if (m_GpuProfiler)
		{
			m_GpuProfiler->BeginScope(*m_Backend.GetCommandList(), name);
		}
	}

	void DX12GraphicsCommandContext::EndGpuProfileScope() noexcept
	{
		if (m_GpuProfiler)
		{
			m_GpuProfiler->EndScope(*m_Backend.GetCommandList());
		}
	}

	void DX12GraphicsCommandContext::SetRootSignature(
		const DX12RootSignature& rootSignature) noexcept
	{
		m_Backend.GetCommandList()->SetGraphicsRootSignature(rootSignature);
	}

	void DX12GraphicsCommandContext::SetPipelineState(
		const DX12PipelineState& pipelineState) noexcept
	{
		m_Backend.GetCommandList()->SetPipelineState(pipelineState);
	}

	void DX12GraphicsCommandContext::SetDescriptor(
		uint32_t parameterIndex, const DX12DescriptorView& descriptor) noexcept
	{
		m_Backend.GetCommandList()->SetGraphicsDescriptor(parameterIndex, descriptor);
	}

	DX12ComputeCommandContext::DX12ComputeCommandContext(DX12Device* device,
		DX12PipelineSystem* pipelineSystem, DX12CommandList* commandList) noexcept :
		m_PipelineSystem(pipelineSystem)
	{
		m_OwnedBackend =
			std::make_unique<DX12CommandContext>(device, commandList, RHIQueueType::Compute);
		m_Backend = m_OwnedBackend.get();
	}

	DX12ComputeCommandContext::DX12ComputeCommandContext(
		DX12GraphicsCommandContext& graphicsContext, DX12PipelineSystem* pipelineSystem) noexcept :
		m_Backend(&graphicsContext.m_Backend), m_PipelineSystem(pipelineSystem)
	{
		GGLAB_ASSERT_NOT_NULL(m_Backend);
		GGLAB_ASSERT_MSG(m_Backend->GetQueueType() == RHIQueueType::Graphics,
			"A direct compute encoder must wrap a graphics/direct command context.");
	}

	void DX12ComputeCommandContext::TextureBarrier(
		std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_Backend->TextureBarrier(barriers);
	}

	void DX12ComputeCommandContext::BufferBarrier(
		std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_Backend->BufferBarrier(barriers);
	}

	void DX12ComputeCommandContext::SetPipeline(RHIPipelineHandle pipeline) noexcept
	{
		DX12PipelineState* pipelineState = nullptr;
		DX12RootSignature* rootSignature = nullptr;
		if (!m_PipelineSystem ||
			!m_PipelineSystem->ResolveComputePipeline(pipeline, pipelineState, rootSignature))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ComputeCommandContext::SetPipeline received an invalid pipeline handle.");
			return;
		}
		if (m_CurrentRootSignature != rootSignature)
		{
			m_Backend->GetCommandList()->SetComputeRootSignature(*rootSignature);
			m_CurrentRootSignature = rootSignature;
		}
		m_Backend->GetCommandList()->SetPipelineState(*pipelineState);
	}

	void DX12ComputeCommandContext::SetDescriptorTable(
		const RHIDescriptorTableBinding& binding) noexcept
	{
		const D3D12_GPU_DESCRIPTOR_HANDLE table =
			m_Backend->GetDevice()->ResolveShaderVisibleDescriptor(
				binding.m_HeapType, binding.m_TableIndex);
		if (table.ptr == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ComputeCommandContext::SetDescriptorTable received an invalid descriptor table.");
			return;
		}
		m_Backend->GetCommandList()->SetComputeDescriptorTable(binding.m_ParameterIndex, table);
	}

	void DX12ComputeCommandContext::SetConstantBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		DX12Buffer* nativeBuffer = m_Backend->GetDevice()->ResolveBuffer(buffer);
		if (!nativeBuffer || offset >= nativeBuffer->SizeInBytes())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ComputeCommandContext::SetConstantBuffer received an invalid buffer binding.");
			return;
		}
		m_Backend->GetCommandList()->SetComputeConstantBuffer(
			parameterIndex, nativeBuffer->GPUVirtualAddress() + offset);
		m_Backend->TrackBufferUse(buffer);
	}

	void DX12ComputeCommandContext::SetReadOnlyBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		DX12Buffer* nativeBuffer = m_Backend->GetDevice()->ResolveBuffer(buffer);
		if (!nativeBuffer || offset >= nativeBuffer->SizeInBytes())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ComputeCommandContext::SetReadOnlyBuffer received an invalid buffer binding.");
			return;
		}
		m_Backend->GetCommandList()->SetComputeReadOnlyBuffer(
			parameterIndex, nativeBuffer->GPUVirtualAddress() + offset);
		m_Backend->TrackBufferUse(buffer);
	}

	void DX12ComputeCommandContext::SetReadWriteBuffer(
		uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset) noexcept
	{
		DX12Buffer* nativeBuffer = m_Backend->GetDevice()->ResolveBuffer(buffer);
		if (!nativeBuffer || offset >= nativeBuffer->SizeInBytes())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12ComputeCommandContext::SetReadWriteBuffer received an invalid buffer binding.");
			return;
		}
		m_Backend->GetCommandList()->SetComputeReadWriteBuffer(
			parameterIndex, nativeBuffer->GPUVirtualAddress() + offset);
		m_Backend->TrackBufferUse(buffer);
	}

	void DX12ComputeCommandContext::SetPushConstants(
		uint32_t parameterIndex, std::span<const uint32_t> values, uint32_t destOffset) noexcept
	{
		m_Backend->GetCommandList()->SetComputeRoot32BitConstants(
			parameterIndex, values, destOffset);
	}

	void DX12ComputeCommandContext::Dispatch(
		uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept
	{
		GGLAB_ASSERT_MSG(!m_Backend->IsRendering(),
			"Dispatch cannot be encoded inside an active rendering scope.");
		if (m_Backend->IsRendering())
		{
			return;
		}
		m_Backend->GetCommandList()->Dispatch(groupCountX, groupCountY, groupCountZ);
	}

	void DX12ComputeCommandContext::SetPipelineState(
		const DX12PipelineState& pipelineState) noexcept
	{
		m_Backend->GetCommandList()->SetPipelineState(pipelineState);
	}

	void DX12ComputeCommandContext::SetDescriptor(
		uint32_t parameterIndex, const DX12DescriptorView& descriptor) noexcept
	{
		m_Backend->GetCommandList()->SetComputeDescriptorTable(
			parameterIndex, descriptor.m_GpuHandle);
	}

	void DX12ComputeCommandContext::BeginGpuProfileScope(std::string_view name) noexcept
	{
		if (m_GpuProfiler)
		{
			m_GpuProfiler->BeginScope(*m_Backend->GetCommandList(), name);
		}
	}

	void DX12ComputeCommandContext::EndGpuProfileScope() noexcept
	{
		if (m_GpuProfiler)
		{
			m_GpuProfiler->EndScope(*m_Backend->GetCommandList());
		}
	}
}
