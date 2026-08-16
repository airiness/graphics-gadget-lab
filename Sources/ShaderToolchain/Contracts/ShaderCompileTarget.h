#pragma once
#include "Contracts/ShaderCompileTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	// A target profile selects the target environment and consumes a Shader ABI
	// revision. Profile version and Shader ABI revision are independent
	// evolution axes; an environment change without an ABI change must not
	// duplicate the ABI contract.
	enum class ShaderTargetProfile : uint8_t
	{
		GGLabDX12,
		GGLabVulkan13,
	};

	// Derives the target profile from the resolved target semantics. Profile
	// version and Shader ABI revision stay independent axes: this maps the
	// resolved fields back to the profile vocabulary for manifest/CLI
	// serialization.
	[[nodiscard]] constexpr ShaderTargetProfile GetShaderTargetProfile(
		ShaderBinaryFormat binaryFormat, ShaderSpirVTargetEnvironment environment) noexcept
	{
		if (binaryFormat == ShaderBinaryFormat::SpirV &&
			environment == ShaderSpirVTargetEnvironment::Vulkan1_3)
		{
			return ShaderTargetProfile::GGLabVulkan13;
		}
		return ShaderTargetProfile::GGLabDX12;
	}

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

	enum class ShaderCompilerKind : uint8_t
	{
		Dxc,
	};

	// Producer identity ("who produces it"). Keep the single canonical
	// identity produced by QueryDxcVersion() (major.minor plus commit
	// count/hash when available); the type split must not introduce new
	// producer-identity inputs. Identity is an authority of the active
	// compiler instance and is never written back into ShaderDesc or
	// ShaderCompileTarget.
	struct ShaderCompilerIdentity
	{
		ShaderCompilerKind m_Kind = ShaderCompilerKind::Dxc;
		std::wstring m_CanonicalIdentity;
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
