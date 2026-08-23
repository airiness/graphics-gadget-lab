#pragma once
#include "Contracts/ShaderCompileTypes.h"
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	// Field ownership boundary:
	//   backend-owned   - binaryFormat, spirvEnvironment, bindingAbiRevision,
	//                     coordinateOptions (profile application overwrites them).
	//   authoring-owned - shaderModel, hlslVersion, flags, optimization
	//                     (backend policy never touches them).
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

	// Compile request ("what to compile"). Normalization may canonicalize this
	// request, but producer identity is never stored here.
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
}
