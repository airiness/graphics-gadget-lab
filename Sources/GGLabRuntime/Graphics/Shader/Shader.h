#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/Shader/ShaderTypes.h"

#include <cstdint>
#include <filesystem>

namespace gglab
{
	// Transitional compile hand-off between ShaderCompiler and ShaderManager.
	// The semantic split into ShaderArtifact / ShaderCompileResult /
	// runtime tracking belongs to S2; S1 keeps this representation unchanged.
	struct ShaderCompileArtifact
	{
		std::filesystem::path m_BinaryPath{};
		std::filesystem::path m_MetaPath{};
		ShaderBinary m_Binary{};
		ShaderCompileTarget m_Target{ .m_BinaryFormat = ShaderBinaryFormat::Unknown };
		ShaderHash128 m_Hash{};
		std::filesystem::file_time_type m_SourceTimeStamp{};
		bool m_FromCache = false;

		[[nodiscard]] ShaderBinaryFormat GetBinaryFormat() const noexcept
		{
			return m_Target.m_BinaryFormat;
		}

		void Reset() noexcept
		{
			m_BinaryPath.clear();
			m_MetaPath.clear();
			m_Binary.Reset();
			m_Target = {};
			m_Target.m_BinaryFormat = ShaderBinaryFormat::Unknown;
			m_Hash = {};
			m_SourceTimeStamp = {};
			m_FromCache = false;
		}
	};

	class ShaderManager;
	class Shader
	{
	public:
		explicit Shader(const ShaderDesc& desc) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(Shader);
		~Shader() = default;

		ShaderBytecode GetBytecode() const noexcept;
		const ShaderDesc& GetDesc() const noexcept { return m_Desc; }
		const ShaderCompileArtifact& GetCompileArtifact() const noexcept { return m_Artifact; }
		uint64_t GetGeneration() const noexcept { return m_Generation; }
		bool IsValid() const noexcept { return m_Artifact.m_Binary.IsValid(); }

	private:
		void SetCompileArtifact(ShaderCompileArtifact artifact, bool changed) noexcept;

	private:
		ShaderDesc m_Desc;
		ShaderCompileArtifact m_Artifact;
		uint64_t m_Generation = 0;

		friend class ShaderManager;
	};
}
