#include "Precompiled.h"
#include "PipelineDesc.h"

namespace gglab
{
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