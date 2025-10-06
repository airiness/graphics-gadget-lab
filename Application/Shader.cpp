#include "Precompiled.h"
#include "Shader.h"
#include "ShaderCompiler.h"

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
		byteCode.pShaderBytecode = m_Artifact.m_Blob->GetBufferPointer();
		byteCode.BytecodeLength = m_Artifact.m_Blob->GetBufferSize();

		return byteCode;
	}


	bool Shader::EnsureCompiled(ShaderCompiler& compiler) noexcept
	{
		
	}
}
