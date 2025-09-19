#include "Precompiled.h"
#include "PipelineDesc.h"

namespace gglab
{
	D3D12_PIPELINE_STATE_STREAM_DESC GraphicsPipelineDesc::ToStreamDesc(void** outStreamStorage, size_t outStreamCount) const noexcept
	{
		const auto makeInputLayout = [this](const std::vector<D3D12_INPUT_ELEMENT_DESC>& inputLayout) -> D3D12_INPUT_LAYOUT_DESC
			{
				return { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
			};

		struct SubObjRootSignature
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			ID3D12RootSignature* m_RootSignature;
		} rootSignature{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE , m_RootSignature };

		struct SubObjInputLayout
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_INPUT_LAYOUT_DESC m_InputLayoutDesc;
		} inputLayout{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT , makeInputLayout(m_InputLayout) };

		struct SubObjTopology
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_PRIMITIVE_TOPOLOGY_TYPE m_TopologyType;
		} primitiveTopology{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY , m_Topology };

		struct SubObjRasterizer
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_RASTERIZER_DESC m_RasterizerDesc;
		} rasterizer{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER , m_RasterizerDesc };

		struct SubObjDepthStencil
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_DEPTH_STENCIL_DESC1 m_DepthStencilDesc;
		} depthStencil{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1 , m_DepthDesc };

		struct SubObjBlend
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_BLEND_DESC m_BlendDesc;
		} blend{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND , m_BlendDesc };

		D3D12_RT_FORMAT_ARRAY rtvFormats{};
		rtvFormats.NumRenderTargets = m_RtvCount;
		for (uint32_t i = 0; i < m_RtvCount && i < 8; ++i)
		{
			rtvFormats.RTFormats[i] = m_RtvFormats[i];
		}
		struct SubObjRenderTargetFormats
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_RT_FORMAT_ARRAY m_RtvFormats;
		} rtvFormat{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS , rtvFormats };

		struct SubObjDepthStencilFormat
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			DXGI_FORMAT m_DsvFormat;
		} dsvFormat{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT , m_DsvFormat };

		struct SubObjSampleDesc
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			DXGI_SAMPLE_DESC m_SampleDesc;
		} sampleDesc{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC , { m_SampleCount, m_SampleQuality } };

		struct SubObjSampleMask
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			UINT m_SampleMask;
		} sampleMask{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK , m_SampleMask };

		struct SubObjVS
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_SHADER_BYTECODE m_ShaderBytecode;
		} vs{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS , m_VertexShader };

		struct SubObjPS
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_SHADER_BYTECODE m_ShaderBytecode;
		} ps{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS , m_PixelShader };

		struct SubObjDS
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_SHADER_BYTECODE m_ShaderBytecode;
		} ds{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS , m_DomainShader };

		struct SubObjHS
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_SHADER_BYTECODE m_ShaderBytecode;
		} hs{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS , m_HullShader };

		struct SubObjGS
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_SHADER_BYTECODE m_ShaderBytecode;
		} gs{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS , m_GeometryShader };

		// Collect sub-objects
		void* subObjects[] = {
			&rootSignature,
			m_InputLayout.size() > 0 ? static_cast<void*>(&inputLayout) : nullptr,
			(m_VertexShader.BytecodeLength) > 0 ? static_cast<void*>(&vs) : nullptr,
			(m_PixelShader.BytecodeLength) > 0 ? static_cast<void*>(&ps) : nullptr,
			(m_DomainShader.BytecodeLength) > 0 ? static_cast<void*>(&ds) : nullptr,
			(m_HullShader.BytecodeLength) > 0 ? static_cast<void*>(&hs) : nullptr,
			(m_GeometryShader.BytecodeLength) > 0 ? static_cast<void*>(&gs) : nullptr,
			&primitiveTopology,
			&rasterizer,
			&depthStencil,
			&blend,
			(m_RtvCount > 0) ? static_cast<void*>(&rtvFormat) : nullptr,
			m_DsvFormat != DXGI_FORMAT_UNKNOWN ? static_cast<void*>(&dsvFormat) : nullptr,
			(m_SampleCount > 1 || m_SampleQuality > 0) ? static_cast<void*>(&sampleDesc) : nullptr,
			m_SampleMask != std::numeric_limits<uint32_t>::max() ? static_cast<void*>(&sampleMask) : nullptr,
		};

		// temp storage for non-null sub-objects
		size_t count = 0;
		for (void* obj : subObjects)
		{
			if (obj != nullptr)
			{
				outStreamStorage[count++] = obj;
			}
		}

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
		streamDesc.pPipelineStateSubobjectStream = outStreamStorage;
		streamDesc.SizeInBytes = count * sizeof(void*);
		return streamDesc;
	}

	GraphicsPSOKey GraphicsPipelineDesc::MakeKey(ShaderHash128 vsHash, ShaderHash128 psHash,
		ShaderHash128 dsHash, ShaderHash128 hsHash, ShaderHash128 gsHash) const noexcept
	{
		GraphicsPSOKey key{};
		key.m_RootSignatureId = m_RootSignatureId;
		key.m_VertexShader = vsHash;
		key.m_PixelShader = psHash;
		key.m_DomainShader = dsHash;
		key.m_HullShader = hsHash;
		key.m_GeometryShader = gsHash;
		key.m_RtvCount = m_RtvCount;
		for (uint32_t i = 0; i < m_RtvCount && i < 8; ++i)
		{
			key.m_Rtv[i] = m_RtvFormats[i];
		}
		key.m_Dsv = m_DsvFormat;
		key.m_Topology = m_Topology;
		key.m_SampleCount = m_SampleCount;
		key.m_SampleQuality = m_SampleQuality;
		key.m_SampleMask = m_SampleMask;
		key.m_Rasterizer.PackRasterizerBits(m_RasterizerDesc);
		key.m_Depth.PackDepthBits(m_DepthDesc);
		key.m_Blend.PackBlendBits(m_BlendDesc, m_RtvCount);
		return key;
	}

	bool GraphicsPipelineDesc::Validate() const noexcept
	{
		// RootSignature must be set
		if (m_RootSignature == nullptr)
		{
			return false;
		}

		// VertexShader must be set
		if (m_VertexShader.BytecodeLength == 0 || m_VertexShader.pShaderBytecode == nullptr)
		{
			return false;
		}

		// If PixelShader is set, it must have valid bytecode
		if ((m_PixelShader.BytecodeLength > 0 && m_PixelShader.pShaderBytecode == nullptr) ||
			(m_PixelShader.BytecodeLength == 0 && m_PixelShader.pShaderBytecode != nullptr))
		{
			return false;
		}

		// If DomainShader is set, it must have valid bytecode
		if ((m_DomainShader.BytecodeLength > 0 && m_DomainShader.pShaderBytecode == nullptr) ||
			(m_DomainShader.BytecodeLength == 0 && m_DomainShader.pShaderBytecode != nullptr))
		{
			return false;
		}

		// If HullShader is set, it must have valid bytecode
		if ((m_HullShader.BytecodeLength > 0 && m_HullShader.pShaderBytecode == nullptr) ||
			(m_HullShader.BytecodeLength == 0 && m_HullShader.pShaderBytecode != nullptr))
		{
			return false;
		}

		// If GeometryShader is set, it must have valid bytecode
		if ((m_GeometryShader.BytecodeLength > 0 && m_GeometryShader.pShaderBytecode == nullptr) ||
			(m_GeometryShader.BytecodeLength == 0 && m_GeometryShader.pShaderBytecode != nullptr))
		{
			return false;
		}

		// RtvCount must be between 0 and 8
		if (m_RtvCount > 8)
		{
			return false;
		}

		// If RtvCount > 0, RtvFormats must be valid
		for (uint32_t i = 0; i < m_RtvCount; ++i)
		{
			if (m_RtvFormats[i] == DXGI_FORMAT_UNKNOWN)
			{
				return false;
			}
		}

		return true;
	}

	D3D12_PIPELINE_STATE_STREAM_DESC ComputePipelineDesc::ToStreamDesc(void** outStreamStorage, size_t outStreamCount) const noexcept
	{
		struct SubObjRootSignature
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			ID3D12RootSignature* m_RootSignature;
		} rootSignature{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE , m_RootSignature };

		struct SubObjCS
		{
			D3D12_PIPELINE_STATE_SUBOBJECT_TYPE m_Type;
			D3D12_SHADER_BYTECODE m_ShaderBytecode;
		} cs{ D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS , m_ComputeShader };

		// Collect sub-objects
		void* subObjects[] = {
			&rootSignature,
			(m_ComputeShader.BytecodeLength > 0) ? static_cast<void*>(&cs) : nullptr,
		};

		// temp storage for non-null sub-objects
		size_t count = 0;
		for (void* obj : subObjects)
		{
			if (obj != nullptr)
			{
				outStreamStorage[count++] = obj;
			}
		}

		D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
		streamDesc.pPipelineStateSubobjectStream = outStreamStorage;
		streamDesc.SizeInBytes = count * sizeof(void*);
		return streamDesc;
	}

	ComputePSOKey ComputePipelineDesc::MakeKey(ShaderHash128 csHash) const noexcept
	{		
		ComputePSOKey key{};
		key.m_RootSignatureId = m_RootSignatureId;
		key.m_ComputeShader = csHash;
		return key;
	}

	bool ComputePipelineDesc::Validate() const noexcept
	{	
		// RootSignature must be set
		if (m_RootSignature == nullptr)
		{
			return false;
		}
		// ComputeShader must be set
		if (m_ComputeShader.BytecodeLength == 0 || m_ComputeShader.pShaderBytecode == nullptr)
		{
			return false;
		}
		return true;
	}
}