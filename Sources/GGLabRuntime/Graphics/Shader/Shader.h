#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Shader/ShaderTypes.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <cstdint>
#include <string>

namespace gglab
{
	class ShaderManager;
	class Shader
	{
	public:
		explicit Shader(ShaderProgramRef programRef, std::wstring runtimeEntryPoint) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Shader);
		~Shader() = default;

		ShaderBytecode GetBytecode() const noexcept;
		const ShaderProgramRef& GetProgramRef() const noexcept { return m_ProgramRef; }
		const ShaderArtifactRef& GetArtifactRef() const noexcept { return m_ArtifactRef; }
		const ShaderRuntimeArtifact& GetArtifact() const noexcept { return m_Artifact; }
		ShaderHash128 GetHash() const noexcept { return m_Hash; }
		uint64_t GetGeneration() const noexcept { return m_Generation; }
		bool IsValid() const noexcept { return m_Artifact.m_Binary.IsValid(); }

	private:
		void SetRuntimeArtifact(ShaderRuntimeArtifact artifact,
			ShaderArtifactRef artifactRef, ShaderHash128 hash, bool changed) noexcept;

	private:
		ShaderProgramRef m_ProgramRef;
		std::wstring m_RuntimeEntryPoint;
		ShaderArtifactRef m_ArtifactRef{};
		ShaderRuntimeArtifact m_Artifact;
		ShaderHash128 m_Hash{};
		uint64_t m_Generation = 0;

		friend class ShaderManager;
	};
}
