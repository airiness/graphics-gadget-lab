#include "Core/Precompiled.h"
#include "Graphics/Shader.h"

namespace gglab
{
	Shader::Shader(const ShaderDesc& desc) noexcept :
		m_Desc(desc)
	{
	}

	D3D12_SHADER_BYTECODE Shader::GetByteCode() const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "Shader must be compiled.");

		D3D12_SHADER_BYTECODE byteCode{};
		byteCode.pShaderBytecode = m_Artifact.m_DxilBlob->GetBufferPointer();
		byteCode.BytecodeLength = m_Artifact.m_DxilBlob->GetBufferSize();

		return byteCode;
	}

	void Shader::SetCompileArtifact(ShaderCompileArtifact artifact, bool changed) noexcept
	{
		m_Artifact = std::move(artifact);
		if (changed)
		{
			++m_Generation;
		}
	}
}
