#include "Graphics/Shader/Shader.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <utility>

namespace gglab
{
	Shader::Shader(ShaderProgramRef programRef) noexcept : m_ProgramRef(std::move(programRef))
	{
	}

	ShaderBytecode Shader::GetBytecode() const noexcept
	{
		GGLAB_ASSERT_MSG(IsValid(), "Shader must be compiled.");
		return {
			.m_Data = m_Artifact.m_Binary.Data(),
			.m_SizeInBytes = m_Artifact.m_Binary.SizeInBytes(),
			.m_Format = m_Artifact.m_Manifest.m_BinaryFormat,
			.m_Hash = m_Hash,
			.m_EntryPoint = m_Artifact.m_Manifest.m_EntryPoint,
		};
	}

	void Shader::SetRuntimeArtifact(ShaderRuntimeArtifact artifact,
		ShaderArtifactRef artifactRef, ShaderHash128 hash, bool changed) noexcept
	{
		m_Artifact = std::move(artifact);
		m_ArtifactRef = artifactRef;
		m_Hash = hash;
		if (changed)
		{
			++m_Generation;
		}
	}
}
