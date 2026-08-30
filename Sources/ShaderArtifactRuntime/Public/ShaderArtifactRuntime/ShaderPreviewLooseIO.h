#pragma once
#include "ShaderArtifactRuntime/ShaderPreviewPublication.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t ShaderPreviewPublicationFileFormatVersion = 1;
	inline constexpr size_t SerializedShaderPreviewPublicationFixedSize = 233;
	inline constexpr size_t MaxSerializedShaderPreviewPublicationSize =
		SerializedShaderPreviewPublicationFixedSize +
		4 * MaxShaderPreviewIdentityComponentSize;
	inline constexpr uint32_t ShaderPreviewActivePublicationFileFormatVersion = 1;
	inline constexpr size_t SerializedShaderPreviewActivePublicationSize = 56;
	inline constexpr uint32_t ShaderPreviewObservationFileFormatVersion = 1;
	inline constexpr size_t SerializedShaderPreviewObservationSize = 90;

	using SerializedShaderPreviewPublication = std::vector<std::byte>;
	using SerializedShaderPreviewActivePublication =
		std::array<std::byte, SerializedShaderPreviewActivePublicationSize>;
	using SerializedShaderPreviewObservation =
		std::array<std::byte, SerializedShaderPreviewObservationSize>;

	[[nodiscard]] SerializedShaderPreviewPublication
		SerializeShaderPreviewPublication(
			const ShaderPreviewPublicationArtifact& artifact) noexcept;
	[[nodiscard]] std::optional<ShaderPreviewPublicationArtifact>
		DeserializeShaderPreviewPublication(
			std::span<const std::byte> bytes) noexcept;
	[[nodiscard]] SerializedShaderPreviewActivePublication
		SerializeShaderPreviewActivePublication(
			const ShaderPreviewActivePublication& activePublication) noexcept;
	[[nodiscard]] std::optional<ShaderPreviewActivePublication>
		DeserializeShaderPreviewActivePublication(
			std::span<const std::byte> bytes) noexcept;
	[[nodiscard]] SerializedShaderPreviewObservation
		SerializeShaderPreviewObservation(
			const ShaderPreviewObservation& observation) noexcept;
	[[nodiscard]] std::optional<ShaderPreviewObservation>
		DeserializeShaderPreviewObservation(
			std::span<const std::byte> bytes) noexcept;

	struct ShaderLoosePreviewPublicationPath final
	{
		std::filesystem::path m_Path{};
	};

	class ShaderLoosePreviewPublicationLocator final
	{
	public:
		explicit ShaderLoosePreviewPublicationLocator(std::filesystem::path root);

		[[nodiscard]] const std::filesystem::path& GetRoot() const noexcept;
		[[nodiscard]] ShaderLoosePreviewPublicationPath GetPath(
			const ShaderPreviewPublicationRef& publicationRef) const;

	private:
		std::filesystem::path m_Root;
	};

	struct ShaderLoosePreviewSessionPaths final
	{
		std::filesystem::path m_ActivePublicationPath{};
		std::filesystem::path m_ObservationPath{};
	};

	class ShaderLoosePreviewSessionLocator final
	{
	public:
		ShaderLoosePreviewSessionLocator(
			std::filesystem::path root, std::string sessionId);

		[[nodiscard]] const std::filesystem::path& GetRoot() const noexcept;
		[[nodiscard]] std::string_view GetSessionId() const noexcept;
		[[nodiscard]] ShaderLoosePreviewSessionPaths GetPaths() const;

	private:
		std::filesystem::path m_Root;
		std::string m_SessionId;
	};
}
