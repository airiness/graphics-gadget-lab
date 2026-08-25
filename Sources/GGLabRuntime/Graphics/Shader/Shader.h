#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Shader/ShaderTypes.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <cstdint>
namespace gglab
{
	class ShaderManager;
	class Shader
	{
	public:
		explicit Shader(ShaderProgramRef programRef) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Shader);
		~Shader() = default;

		ShaderBytecode GetBytecode() const noexcept;
		const ShaderProgramRef& GetProgramRef() const noexcept { return m_ProgramRef; }
		const ShaderArtifactRef& GetArtifactRef() const noexcept { return m_ArtifactRef; }
		const ShaderRuntimeArtifact& GetArtifact() const noexcept { return m_Artifact; }
		ShaderHash128 GetHash() const noexcept { return m_Hash; }
		uint64_t GetGeneration() const noexcept { return m_Generation; }
		bool IsValid() const noexcept
		{
			return m_ArtifactRef.IsValid() && m_Artifact.m_Binary.IsValid() &&
				m_Artifact.m_Manifest.m_Stage == m_ProgramRef.m_Stage &&
				IsValidShaderRuntimeEntryPoint(m_Artifact.m_Manifest.m_EntryPoint);
		}

	private:
		void SetRuntimeArtifact(ShaderRuntimeArtifact artifact,
			ShaderArtifactRef artifactRef, ShaderHash128 hash, bool changed) noexcept;

	private:
		ShaderProgramRef m_ProgramRef;
		ShaderArtifactRef m_ArtifactRef{};
		ShaderRuntimeArtifact m_Artifact;
		ShaderHash128 m_Hash{};
		uint64_t m_Generation = 0;

		friend class ShaderManager;
	};
}
