#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactStore.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistryArtifact.h"

#include <cstddef>
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t ShaderRuntimeArtifactFileFormatVersion = 2;
	inline constexpr size_t SerializedShaderRuntimeArtifactManifestFixedSize = 96;
	inline constexpr size_t MaxSerializedShaderRuntimeArtifactManifestSize =
		SerializedShaderRuntimeArtifactManifestFixedSize + MaxShaderRuntimeEntryPointSize;
	inline constexpr uintmax_t MaxLooseShaderArtifactBinarySize = 256u * 1024u * 1024u;
	inline constexpr uint32_t ShaderProgramRegistryArtifactFileFormatVersion = 1;
	inline constexpr size_t SerializedShaderProgramRegistryArtifactHeaderSize = 52;
	inline constexpr size_t SerializedShaderProgramRegistryEntryFixedSize = 45;
	inline constexpr uintmax_t MaxSerializedShaderProgramRegistryArtifactSize =
		SerializedShaderProgramRegistryArtifactHeaderSize +
		static_cast<uintmax_t>(MaxShaderProgramRegistryEntryCount) *
			(SerializedShaderProgramRegistryEntryFixedSize +
				2u * MaxShaderProgramIdentityComponentSize);
	inline constexpr uint32_t ActiveShaderProgramRegistryFileFormatVersion = 1;
	inline constexpr uint32_t ActiveShaderProgramRegistrySchemaVersion = 1;
	inline constexpr size_t SerializedActiveShaderProgramRegistrySize = 48;

	using SerializedShaderRuntimeArtifactManifest =
		std::vector<std::byte>;
	using SerializedShaderProgramRegistryArtifact = std::vector<std::byte>;
	using SerializedActiveShaderProgramRegistry =
		std::array<std::byte, SerializedActiveShaderProgramRegistrySize>;

	[[nodiscard]] SerializedShaderRuntimeArtifactManifest
		SerializeShaderRuntimeArtifactManifest(
			const ShaderRuntimeArtifactManifest& manifest) noexcept;
	[[nodiscard]] std::optional<ShaderRuntimeArtifactManifest>
		DeserializeShaderRuntimeArtifactManifest(std::span<const std::byte> bytes) noexcept;
	[[nodiscard]] SerializedShaderProgramRegistryArtifact
		SerializeShaderProgramRegistryArtifact(
			const ShaderProgramRegistryArtifact& artifact) noexcept;
	[[nodiscard]] std::optional<ShaderProgramRegistryArtifact>
		DeserializeShaderProgramRegistryArtifact(
			std::span<const std::byte> bytes) noexcept;
	[[nodiscard]] SerializedActiveShaderProgramRegistry
		SerializeActiveShaderProgramRegistry(
			const ShaderProgramRegistryArtifactRef& registryRef) noexcept;
	[[nodiscard]] std::optional<ShaderProgramRegistryArtifactRef>
		DeserializeActiveShaderProgramRegistry(
			std::span<const std::byte> bytes) noexcept;

	struct ShaderLooseArtifactPaths final
	{
		std::filesystem::path m_BinaryPath{};
		std::filesystem::path m_ManifestPath{};
	};

	class ShaderLooseArtifactLocator final
	{
	public:
		explicit ShaderLooseArtifactLocator(std::filesystem::path root);

		[[nodiscard]] const std::filesystem::path& GetRoot() const noexcept;
		[[nodiscard]] ShaderLooseArtifactPaths GetPaths(
			const ShaderArtifactRef& artifactRef) const;

	private:
		std::filesystem::path m_Root;
	};

	class ShaderLooseArtifactReader final : public ShaderArtifactReaderBase
	{
	public:
		explicit ShaderLooseArtifactReader(ShaderLooseArtifactLocator locator);

		[[nodiscard]] const ShaderLooseArtifactLocator& GetLocator() const noexcept;
		[[nodiscard]] ShaderArtifactReadResult ReadArtifact(
			const ShaderArtifactRef& artifactRef) noexcept override;

	private:
		ShaderLooseArtifactLocator m_Locator;
	};

	struct ShaderLooseProgramRegistryArtifactPath final
	{
		std::filesystem::path m_Path{};
	};

	class ShaderLooseProgramRegistryArtifactLocator final
	{
	public:
		explicit ShaderLooseProgramRegistryArtifactLocator(std::filesystem::path root);

		[[nodiscard]] const std::filesystem::path& GetRoot() const noexcept;
		[[nodiscard]] ShaderLooseProgramRegistryArtifactPath GetPath(
			const ShaderProgramRegistryArtifactRef& registryRef) const;

	private:
		std::filesystem::path m_Root;
	};

	enum class ShaderProgramRegistryArtifactReadStatus : uint8_t
	{
		Success,
		NotFound,
		IOFailure,
		MalformedArtifact,
	};

	struct ShaderProgramRegistryArtifactReadResult final
	{
		ShaderProgramRegistryArtifactReadStatus m_Status =
			ShaderProgramRegistryArtifactReadStatus::IOFailure;
		ShaderProgramRegistryArtifact m_Artifact{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ShaderProgramRegistryArtifactReadStatus::Success;
		}
	};

	class ShaderLooseProgramRegistryArtifactReader final
	{
	public:
		explicit ShaderLooseProgramRegistryArtifactReader(
			ShaderLooseProgramRegistryArtifactLocator locator);

		[[nodiscard]] const ShaderLooseProgramRegistryArtifactLocator&
			GetLocator() const noexcept;
		[[nodiscard]] ShaderProgramRegistryArtifactReadResult ReadArtifact(
			const ShaderProgramRegistryArtifactRef& registryRef) noexcept;

	private:
		ShaderLooseProgramRegistryArtifactLocator m_Locator;
	};

	class ShaderLooseActiveProgramRegistryLocator final
	{
	public:
		ShaderLooseActiveProgramRegistryLocator(
			std::filesystem::path root, ShaderTargetProfile targetProfile);

		[[nodiscard]] const std::filesystem::path& GetRoot() const noexcept;
		[[nodiscard]] ShaderTargetProfile GetTargetProfile() const noexcept;
		[[nodiscard]] std::filesystem::path GetPath() const;

	private:
		std::filesystem::path m_Root;
		ShaderTargetProfile m_TargetProfile = ShaderTargetProfile::GGLabDX12;
	};

	enum class ActiveShaderProgramRegistryReadStatus : uint8_t
	{
		Success,
		NotFound,
		IOFailure,
		MalformedRecord,
	};

	struct ActiveShaderProgramRegistryReadResult final
	{
		ActiveShaderProgramRegistryReadStatus m_Status =
			ActiveShaderProgramRegistryReadStatus::IOFailure;
		ShaderProgramRegistryArtifactRef m_RegistryRef{};

		[[nodiscard]] constexpr bool IsSuccess() const noexcept
		{
			return m_Status == ActiveShaderProgramRegistryReadStatus::Success;
		}
	};

	class ShaderLooseActiveProgramRegistryReader final
	{
	public:
		explicit ShaderLooseActiveProgramRegistryReader(
			ShaderLooseActiveProgramRegistryLocator locator);

		[[nodiscard]] const ShaderLooseActiveProgramRegistryLocator&
			GetLocator() const noexcept;
		[[nodiscard]] ActiveShaderProgramRegistryReadResult Read() noexcept;

	private:
		ShaderLooseActiveProgramRegistryLocator m_Locator;
	};
}
