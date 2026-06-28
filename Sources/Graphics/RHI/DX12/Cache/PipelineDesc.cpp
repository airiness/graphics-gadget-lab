#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/Cache/PipelineDesc.h"

namespace gglab
{
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

		const auto validIfSet = [](const D3D12_SHADER_BYTECODE& shader) noexcept
			{
				return (shader.BytecodeLength == 0 && shader.pShaderBytecode == nullptr) ||
					(shader.BytecodeLength > 0 && shader.pShaderBytecode != nullptr);
			};

		if (!validIfSet(m_PixelShader) ||
			!validIfSet(m_DomainShader) ||
			!validIfSet(m_HullShader) ||
			!validIfSet(m_GeometryShader))
		{
			return false;
		}

		// RtvCount must be between 0 and 8
		const auto& rtvFormats = m_Formats.m_RtvFormats;
		if (rtvFormats.NumRenderTargets > 8)
		{
			return false;
		}

		// If RtvCount > 0, RtvFormats must be valid
		for (uint32_t i = 0; i < rtvFormats.NumRenderTargets; ++i)
		{
			if (rtvFormats.RTFormats[i] == DXGI_FORMAT_UNKNOWN)
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
