#pragma once
#include "ShaderArtifactRuntime/ShaderRuntimeArtifact.h"

#include <cstdint>

namespace gglab
{
	struct ShaderArtifactCompatibilityRequest final
	{
		ShaderTargetProfile m_TargetProfile = ShaderTargetProfile::GGLabDX12;
		ShaderBinaryFormat m_BinaryFormat = ShaderBinaryFormat::Dxil;
		ShaderSpirVTargetEnvironment m_SpirVTargetEnvironment =
			ShaderSpirVTargetEnvironment::None;
		uint32_t m_BindingABIRevision = 0;
		ShaderCoordinateOptions m_CoordinateOptions = ShaderCoordinateOptions::None;
		ShaderStage m_Stage = ShaderStage::Vertex;
	};

	enum class ShaderArtifactCompatibilityStatus : uint8_t
	{
		Compatible,
		InvalidRequest,
		UnsupportedManifestSchema,
		InvalidManifestTarget,
		InvalidEntryPoint,
		TargetProfileMismatch,
		BinaryFormatMismatch,
		TargetEnvironmentMismatch,
		BindingABIRevisionMismatch,
		CoordinateOptionsMismatch,
		ShaderStageMismatch,
	};

	struct ShaderArtifactCompatibilityResult final
	{
		ShaderArtifactCompatibilityStatus m_Status =
			ShaderArtifactCompatibilityStatus::InvalidRequest;

		[[nodiscard]] constexpr bool IsCompatible() const noexcept
		{
			return m_Status == ShaderArtifactCompatibilityStatus::Compatible;
		}
	};

	[[nodiscard]] ShaderArtifactCompatibilityResult ValidateShaderArtifactCompatibility(
		const ShaderRuntimeArtifactManifest& manifest,
		const ShaderArtifactCompatibilityRequest& request) noexcept;

	// Storage-neutral read seam for loose and packaged artifact implementations.
	// Success returns one parsed portable Runtime manifest and the exact binary
	// bytes read with it; validation remains the Store's authority.
	enum class ShaderArtifactReadStatus : uint8_t
	{
		Success,
		NotFound,
		IOFailure,
		MalformedArtifact,
	};

	struct ShaderArtifactReadResult final
	{
		ShaderArtifactReadStatus m_Status = ShaderArtifactReadStatus::IOFailure;
		ShaderRuntimeArtifact m_Artifact{};
	};

	class ShaderArtifactReaderBase
	{
	public:
		virtual ~ShaderArtifactReaderBase() = default;

		[[nodiscard]] virtual ShaderArtifactReadResult ReadArtifact(
			const ShaderArtifactRef& artifactRef) noexcept = 0;
	};

	enum class ShaderArtifactLoadStatus : uint8_t
	{
		Success,
		InvalidReference,
		InvalidCompatibilityRequest,
		NotFound,
		ReadFailure,
		MalformedArtifact,
		IncompatibleArtifact,
		ArtifactIdentityMismatch,
		BinaryDigestMismatch,
		InvalidBinary,
	};

	struct ShaderArtifactLoadResult final
	{
		ShaderArtifactLoadStatus m_Status = ShaderArtifactLoadStatus::ReadFailure;
		ShaderArtifactCompatibilityResult m_Compatibility{};
		ShaderRuntimeArtifact m_Artifact{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderArtifactLoadStatus::Success;
		}
	};

	class ShaderArtifactStore final
	{
	public:
		explicit ShaderArtifactStore(ShaderArtifactReaderBase& reader) noexcept;

		[[nodiscard]] ShaderArtifactLoadResult LoadArtifact(
			const ShaderArtifactRef& artifactRef,
			const ShaderArtifactCompatibilityRequest& compatibility) noexcept;

	private:
		ShaderArtifactReaderBase& m_Reader;
	};
}
