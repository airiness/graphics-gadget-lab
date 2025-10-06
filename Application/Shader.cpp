#include "Precompiled.h"
#include "Shader.h"

namespace gglab
{
	Shader::Shader(const ShaderDesc& desc) noexcept
	{
	}

	bool Shader::EnsureCompiled(ShaderCompiler& compiler) noexcept
	{
		ShaderCompileSnapshot

		return false;
	}

	bool Shader::ForceRecompile(ShaderCompiler& compiler) noexcept
	{
		return false;
	}

	D3D12_SHADER_BYTECODE Shader::GetByteCode() const noexcept
	{
	}
}
