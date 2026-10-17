#include "Application/Content/RuntimeAssetBundle.h"
#include "GGLabFoundation/Hash/Sha256.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <fstream>
#include <set>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::string HashFile(const std::filesystem::path& path) noexcept
		{
			std::ifstream input(path, std::ios::binary);
			Sha256Builder hash;
			std::array<char, 65536> bytes{};
			if (!input || !hash.IsValid())
			{
				return {};
			}
			while (input)
			{
				input.read(bytes.data(), bytes.size());
				if (!hash.AddBytes(std::as_bytes(std::span(bytes.data(),
					static_cast<size_t>(input.gcount())))))
				{
					return {};
				}
			}
			return input.eof() ? Sha256DigestToHex(hash.Finish()) : std::string{};
		}
	}

	std::optional<RuntimeAssetBundle> LoadRuntimeAssetBundle(
		const std::filesystem::path& manifestPath, std::string& failure) noexcept
	{
		const auto fail = [&](std::string message) -> std::optional<RuntimeAssetBundle>
			{
				failure = std::move(message);
				return std::nullopt;
			};
		std::ifstream input(manifestPath, std::ios::binary);
		if (!input)
		{
			return fail("Installed asset bundle manifest is missing.");
		}
		const auto document = nlohmann::json::parse(input, nullptr, false);
		if (document.is_discarded() || !document.is_object() ||
			!document.contains("schemaVersion") || document["schemaVersion"] != 1 ||
			!document.contains("id") || !document["id"].is_string() ||
			!document.contains("revision") || !document["revision"].is_string() ||
			!document.contains("entryPoint") || !document["entryPoint"].is_string() ||
			!document.contains("files") || !document["files"].is_array())
		{
			return fail("Installed asset bundle manifest is invalid.");
		}
		RuntimeAssetBundle bundle{
			.m_Id = document["id"].get<std::string>(),
			.m_Revision = document["revision"].get<std::string>(),
		};
		const std::string entryPoint = document["entryPoint"].get<std::string>();
		std::string canonical = "gglab.asset-bundle.v1\n" + bundle.m_Id + "\n" + entryPoint + "\n";
		std::string previous;
		std::set<std::string> recordedFiles;
		bool foundEntryPoint = false;
		std::error_code error;
		const auto root = std::filesystem::canonical(manifestPath.parent_path(), error);
		if (error || !bundle.m_Id.starts_with("gglab.") || bundle.m_Id.size() <= 6 ||
			bundle.m_Id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos)
		{
			return fail("Installed asset bundle root or id is invalid.");
		}
		for (const auto& file : document["files"])
		{
			if (!file.is_object() || !file.contains("path") || !file["path"].is_string() ||
				!file.contains("sha256") || !file["sha256"].is_string())
			{
				return fail("Installed asset bundle file record is invalid.");
			}
			const std::string name = file["path"].get<std::string>();
			const std::string digest = file["sha256"].get<std::string>();
			const std::filesystem::path relative(name);
			if (name.empty() || name <= previous || relative.is_absolute() ||
				name.find('\0') != std::string::npos ||
				name.find_first_of("\\:\r\n") != std::string::npos ||
				relative.lexically_normal() != relative || name.starts_with("../") || name == "..")
			{
				return fail("Installed asset bundle path is invalid: " + name);
			}
			const auto path = std::filesystem::canonical(root / relative, error);
			const auto withinRoot = path.lexically_relative(root);
			if (error || withinRoot.empty() || *withinRoot.begin() == ".." ||
				digest.size() != 64 || HashFile(path) != digest)
			{
				return fail("Installed asset bundle file identity mismatch: " + name);
			}
			canonical += name + '\0' + digest + '\n';
			previous = name;
			recordedFiles.insert(name);
			if (name == entryPoint)
			{
				foundEntryPoint = true;
				bundle.m_EntryPoint = path;
			}
		}
		if (!foundEntryPoint || Sha256DigestToHex(ComputeSha256(std::as_bytes(
			std::span(canonical.data(), canonical.size())))) != bundle.m_Revision)
		{
			return fail("Installed asset bundle revision is invalid.");
		}
		std::ifstream sceneInput(bundle.m_EntryPoint, std::ios::binary);
		const auto scene = nlohmann::json::parse(sceneInput, nullptr, false);
		if (scene.is_discarded() || !scene.is_object())
		{
			return fail("Installed asset bundle entry point is invalid.");
		}
		std::set<std::string> dependencies{ entryPoint };
		for (const auto field : { "buffers", "images" })
		{
			if (!scene.contains(field) || !scene[field].is_array())
			{
				return fail("Installed asset bundle requires external buffers and images.");
			}
			for (const auto& resource : scene[field])
			{
				if (!resource.is_object() || !resource.contains("uri") || !resource["uri"].is_string())
				{
					return fail("Installed asset bundle resource URI is invalid.");
				}
				dependencies.insert(resource["uri"].get<std::string>());
			}
		}
		if (dependencies != recordedFiles)
		{
			return fail("Installed asset bundle manifest does not cover its glTF dependencies.");
		}
		failure.clear();
		return bundle;
	}
}
