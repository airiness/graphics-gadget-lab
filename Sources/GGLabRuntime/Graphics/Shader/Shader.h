#pragma once
#include "Contracts/ShaderArtifact.h"
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Shader/ShaderTypes.h"

#include <cstdint>
#include <filesystem>

namespace gglab
{
	class ShaderManager;
	class Shader
	{
	public:
		explicit Shader(const ShaderDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Shader);
		~Shader() = default;

		ShaderBytecode GetBytecode() const noexcept;
		const ShaderDesc& GetDesc() const noexcept { return m_Desc; }
		const ShaderArtifact& GetArtifact() const noexcept { return m_Artifact; }
		ShaderHash128 GetHash() const noexcept { return m_Hash; }
		uint64_t GetGeneration() const noexcept { return m_Generation; }
		bool IsValid() const noexcept { return m_Artifact.m_Binary.IsValid(); }

	private:
		// Compile authority is the resolved recipe; m_Desc remains the raw
		// caller description used only for origin/display purposes.
		void SetCompileArtifact(ShaderArtifact artifact, ShaderHash128 hash, bool changed) noexcept;

	private:
		ShaderDesc m_Desc;
		ShaderArtifact m_Artifact;
		ShaderHash128 m_Hash{};
		uint64_t m_Generation = 0;

		friend class ShaderManager;
	};
}
