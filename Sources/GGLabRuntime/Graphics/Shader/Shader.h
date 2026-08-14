#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "Graphics/Shader/ShaderTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	enum class ShaderStage : uint32_t
	{
		Vertex,
		Pixel,
		Hull,
		Domain,
		Geometry,
		Mesh,
		Compute
	};

	enum class ShaderModel : uint32_t
	{
		SM_6_6,
		SM_6_7,
		SM_6_8
	};

	enum class ShaderCompileFlags : uint32_t
	{
		None = 0u,
		Debug = 1u << 0,
		Optimization = 1u << 1,
	};
	GGLAB_ENUM_FLAGS(ShaderCompileFlags);

	enum class ShaderSpirVTargetEnvironment : uint8_t
	{
		None,
		Vulkan1_3,
	};

	enum class ShaderCoordinateOptions : uint8_t
	{
		None = 0u,
		InvertY = 1u << 0,
		UseDxPositionW = 1u << 1,
	};
	GGLAB_ENUM_FLAGS(ShaderCoordinateOptions);

	struct ShaderCompileTarget
	{
		ShaderBinaryFormat m_BinaryFormat = ShaderBinaryFormat::Dxil;
		ShaderModel m_Model = ShaderModel::SM_6_7;
		std::wstring m_HlslVersion = L"2021";
		ShaderCompileFlags m_Flags = ShaderCompileFlags::None;
		std::wstring m_OptimizationLevel = L"O3";
		ShaderSpirVTargetEnvironment m_SpirVTargetEnvironment =
			ShaderSpirVTargetEnvironment::None;
		uint32_t m_BindingABIRevision = 0;
		ShaderCoordinateOptions m_CoordinateOptions = ShaderCoordinateOptions::None;
		std::wstring m_DxcVersion;

		bool operator==(const ShaderCompileTarget&) const noexcept = default;
	};

	enum class ShaderCompileValidationError : uint8_t
	{
		None,
		UnsupportedBinaryFormat,
		EmptySourcePath,
		SourcePathNotCanonical,
		EmptyEntryPoint,
		CompilerIdentityMismatch,
		UnsupportedSpirVTargetEnvironment,
		UnsupportedBindingABIRevision,
		InvalidCoordinateOptions,
		UnexpectedSpirVTargetState,
		ReservedExtraArgument,
	};

	struct ShaderCompileValidationResult
	{
		ShaderCompileValidationError m_Error = ShaderCompileValidationError::None;
		std::wstring m_Message;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Error == ShaderCompileValidationError::None;
		}
	};

	struct ShaderDefine
	{
		std::wstring m_Name{};
		std::wstring m_Value{};

		bool operator==(const ShaderDefine&) const noexcept = default;
		auto operator<=>(const ShaderDefine&) const noexcept = default;
	};

	struct ShaderDesc
	{
		std::filesystem::path m_SourcePath{};
		ShaderStage m_Stage = ShaderStage::Pixel;
		ShaderCompileTarget m_Target{};
		std::wstring m_Entry{};
		std::vector<std::filesystem::path> m_IncludeDirs;
		std::vector<ShaderDefine> m_Defines;
		std::vector<std::wstring> m_ExtraArgs;
	};

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
