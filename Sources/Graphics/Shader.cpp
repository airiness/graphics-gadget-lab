#include "Core/Precompiled.h"
#include "Graphics/Shader.h"

namespace gglab
{
	Shader::Shader(const ShaderDesc& desc) noexcept :
		m_Desc(desc)
	{
	}

	ShaderBytecode Shader::GetBytecode() const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "Shader must be compiled.");
		return {
			.m_Data = m_Artifact.m_Binary.Data(),
			.m_SizeInBytes = m_Artifact.m_Binary.SizeInBytes(),
			.m_Format = m_Artifact.m_Format,
			.m_Hash = m_Artifact.m_Hash,
		};
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
