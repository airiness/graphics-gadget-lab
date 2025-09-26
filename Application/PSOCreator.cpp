#include "Precompiled.h"
#include "PSOCreator.h"

namespace gglab
{
	std::unique_ptr<DX12PipelineState> StreamPSOCreator::CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc)
	{
		if (!desc.Validate())
		{
			GGLAB_LOG_ERROR("Invalid GraphicsPipelineDesc");
			return nullptr;
		}

		alignas(void*) std::array<std::byte, 1024> storage{};
		//storage.fill(std::byte{ 0 });	// initialize with 0 for safety
		StreamWriter writer(storage.data(), storage.size());
		auto streamDesc = BuildGraphicsStream(desc, writer);

		return std::make_unique<DX12PipelineState>(dx12Device, streamDesc);
	}

	std::unique_ptr<DX12PipelineState> StreamPSOCreator::CreateComputePSO(DX12Device* dx12Device, const ComputePipelineDesc& desc)
	{	
		if (!desc.Validate())
		{
			GGLAB_LOG_ERROR("Invalid ComputePipelineDesc");
			return nullptr;
		}

		alignas(void*) std::array<std::byte, 1024> storage{};
		StreamWriter writer(storage.data(), storage.size());
		auto streamDesc = BuildComputeStream(desc, writer);

		return std::make_unique<DX12PipelineState>(dx12Device, streamDesc);
	}

	D3D12_PIPELINE_STATE_STREAM_DESC StreamPSOCreator::BuildGraphicsStream(const GraphicsPipelineDesc& desc, StreamWriter& writer) noexcept
	{
		auto rs = CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE(desc.m_RootSignature);
		writer.Add(rs);
		writer.AddIf(desc.m_InputLayoutDesc.NumElements > 0, CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT(desc.m_InputLayoutDesc));
		writer.Add(CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY(desc.m_Topology));
		writer.AddIf(desc.m_VertexShader.BytecodeLength > 0, CD3DX12_PIPELINE_STATE_STREAM_VS(desc.m_VertexShader));
		writer.AddIf(desc.m_PixelShader.BytecodeLength > 0, CD3DX12_PIPELINE_STATE_STREAM_PS(desc.m_PixelShader));
		writer.AddIf(desc.m_HullShader.BytecodeLength > 0, CD3DX12_PIPELINE_STATE_STREAM_HS(desc.m_HullShader));
		writer.AddIf(desc.m_DomainShader.BytecodeLength > 0, CD3DX12_PIPELINE_STATE_STREAM_DS(desc.m_DomainShader));
		writer.AddIf(desc.m_GeometryShader.BytecodeLength > 0, CD3DX12_PIPELINE_STATE_STREAM_GS(desc.m_GeometryShader));
		writer.AddIf(desc.m_Formats.m_DsvFormat != DXGI_FORMAT_UNKNOWN, CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT(desc.m_Formats.m_DsvFormat));
		writer.Add(CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS(desc.m_Formats.m_RtvFormats));
		writer.Add(CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER(desc.m_RasterizerDesc));
		writer.Add(CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC(desc.m_BlendDesc));
		writer.Add(CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1(desc.m_DepthDesc));
		writer.AddIf((desc.m_Formats.m_SampleCount > 1 || desc.m_Formats.m_SampleQuality > 0),
			CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC({ desc.m_Formats.m_SampleCount, desc.m_Formats.m_SampleQuality }));
		writer.AddIf(desc.m_SampleMask != std::numeric_limits<uint32_t>::max(),
			CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK(desc.m_SampleMask));

		return writer.GetDesc();
	}

	D3D12_PIPELINE_STATE_STREAM_DESC StreamPSOCreator::BuildComputeStream(const ComputePipelineDesc& desc, StreamWriter& writer) noexcept
	{
		writer.Add(CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE(desc.m_RootSignature));
		writer.Add(CD3DX12_PIPELINE_STATE_STREAM_CS(desc.m_ComputeShader));

		return writer.GetDesc();		
	}

	std::unique_ptr<DX12PipelineState> LibraryPSOCreator::CreateGraphicsPSO(DX12Device* dx12Device, const GraphicsPipelineDesc& desc)
	{
		if (!desc.Validate())
		{
			GGLAB_LOG_ERROR("Invalid GraphicsPipelineDesc");
			return nullptr;
		}
		return nullptr;
		// TODO : Management PSO by Library
	}

	std::unique_ptr<DX12PipelineState> LibraryPSOCreator::CreateComputePSO(DX12Device* dx12Device, const ComputePipelineDesc& desc)
	{		
		if (!desc.Validate())
		{
			GGLAB_LOG_ERROR("Invalid ComputePipelineDesc");
			return nullptr;
		}
		return nullptr;
		// TODO : Management PSO by Library
	}
}
