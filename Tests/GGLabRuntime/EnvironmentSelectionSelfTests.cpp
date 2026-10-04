#include "EnvironmentSelectionSelfTests.h"
#include "AssetTestServices.h"

#include "GGLabRuntime/Graphics/Asset/ReservedTexture.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/EnvironmentSourceControl.h"
#include "GGLabRuntime/Graphics/EnvironmentTextureSource.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace gglab
{
	namespace
	{
		using asset_test::AssetManagerHarness;

		class RecordingEnvironmentSource final : public EnvironmentSourceControl
		{
		public:
			void CommitEnvironmentSource(EnvironmentTextureSource source) noexcept override
			{
				m_Committed = source;
				++m_CommitCount;
			}

			EnvironmentTextureSource m_Committed{};
			uint32_t m_CommitCount = 0;
		};

		// Writes a 4x2 Radiance RGBE image; widths below eight use flat scanlines.
		[[nodiscard]] bool WriteEnvironmentHdr(const std::filesystem::path& path, uint8_t level) noexcept
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 4\n";
			for (uint32_t pixel = 0; pixel < 8; ++pixel)
			{
				const std::array<char, 4> rgbe{ static_cast<char>(level), static_cast<char>(level),
					static_cast<char>(level), static_cast<char>(128) };
				file.write(rgbe.data(), rgbe.size());
			}
			return static_cast<bool>(file);
		}

		void RunEnvironmentSelectionTest(SelfTestContext& context) noexcept
		{
			AssetManagerHarness harness("environment-selection");
			const std::filesystem::path root = harness.IsValid() ? harness.GetAssetRoot() : std::filesystem::path{};
			std::error_code errorCode;
			bool fixtures = harness.IsValid() &&
				std::filesystem::create_directories(root / "Skybox", errorCode) &&
				std::filesystem::create_directories(root / "Probes", errorCode);
			for (uint8_t index = 0; index < 4 && fixtures; ++index)
			{
				fixtures = WriteEnvironmentHdr(
					root / "Skybox" / ("Environment" + std::to_string(index) + ".hdr"),
					static_cast<uint8_t>(64 + index * 32));
			}
			if (fixtures)
			{
				std::ofstream invalid(root / "Probes" / "InvalidDecode.hdr", std::ios::binary);
				invalid << "not a radiance image";
				fixtures = static_cast<bool>(invalid) &&
					asset_test::WriteSolidPng(root / "Probes" / "Square.png", 64, 64, 64);
			}
			context.Check(fixtures, "Environment selection composes the harness and its HDR fixtures");
			if (!fixtures)
			{
				return;
			}
			// The pinned fallback cubemap must be resident before the controller may reset.
			const TextureContentRef fallback = harness.GetAssets().GetTextureContentRef(
				ToTextureId(ReservedTextureIDIndex::FallbackEnvironmentCubemap));
			const bool fallbackResident = harness.PumpUntil([&]()
				{ return harness.GetAssets().GetResidentTextureResource(fallback).has_value(); });
			context.Check(fallbackResident, "The reserved fallback environment cubemap becomes resident");
			if (!fallbackResident)
			{
				return;
			}

			RecordingEnvironmentSource source;
			{
				EnvironmentAssetController controller({
					.m_AssetManager = &harness.GetAssets(),
					.m_EnvironmentLighting = &source,
					.m_AssetRoot = root,
					});
				const auto settle = [&](auto&& condition)
					{
						return harness.PumpUntil([&]()
							{
								controller.Tick();
								return condition();
							});
					};
				const auto noPending = [&]()
					{ return controller.GetPendingEnvironmentIndex() == EnvironmentAssetController::InvalidEntryIndex; };

				controller.Initialize("Skybox");
				const bool initial = settle(noPending);
				const auto entries = controller.GetEntries();
				const size_t active = controller.GetActiveEnvironmentIndex();
				context.Check(initial && entries.size() == 4 && active < entries.size() &&
					source.m_Committed.m_Type == EnvironmentTextureSourceType::Equirectangular,
					"Initialization commits the default equirectangular environment");
				if (!initial || entries.size() != 4 || active >= entries.size())
				{
					return;
				}

				// Rapid A-to-B-to-C selection commits only the last ready candidate.
				const size_t third = (active + 3) % entries.size();
				const bool rapid = controller.SelectEnvironment((active + 1) % entries.size()) &&
					controller.SelectEnvironment((active + 2) % entries.size()) &&
					controller.SelectEnvironment(third);
				context.Check(rapid && controller.GetActiveEnvironmentIndex() == active &&
					controller.GetPendingEnvironmentIndex() == third,
					"Rapid selection keeps the committed source until the last candidate is ready");
				bool activeStable = true;
				const bool rapidSettled = settle([&]()
					{
						activeStable &= noPending() || controller.GetActiveEnvironmentIndex() == active;
						return noPending();
					});
				context.Check(rapidSettled && activeStable && controller.GetActiveEnvironmentIndex() == third,
					"Rapid selection commits the last candidate without exposing intermediate ones");

				// An immediate failure invalidates an older pending candidate.
				const size_t target = (third + 1) % entries.size();
				const uint64_t serial = controller.GetSelectionSerial();
				const bool targetSelected = controller.SelectEnvironment(target);
				const bool missingRejected =
					!controller.SelectEnvironmentFile("Skybox/__gglab_missing_environment__.hdr", "Missing");
				const auto afterFailure = controller.GetEntries();
				context.Check(targetSelected && missingRejected &&
					controller.GetActiveEnvironmentIndex() == third && noPending() &&
					controller.GetSelectionSerial() > serial && !afterFailure.empty() &&
					afterFailure.back().m_State == EnvironmentAssetEntryState::Failed,
					"An immediate selection failure invalidates the older pending candidate");

				// A transactional switch keeps the active source until the replacement is ready.
				const bool switched = controller.SelectEnvironment(target) &&
					controller.GetActiveEnvironmentIndex() == third;
				activeStable = true;
				const bool switchSettled = settle([&]()
					{
						activeStable &= noPending() || controller.GetActiveEnvironmentIndex() == third;
						return noPending();
					});
				context.Check(switched && switchSettled && activeStable &&
					controller.GetActiveEnvironmentIndex() == target,
					"A transactional switch commits the replacement only once it is ready");

				// Rejected candidates never replace the active environment.
				const auto probe = [&](const char* path, EnvironmentAssetEntryState expected)
					{
						if (!controller.SelectEnvironmentFile(path))
						{
							return false;
						}
						const size_t probeIndex = controller.GetPendingEnvironmentIndex();
						bool kept = true;
						const bool settled = settle([&]()
							{
								kept &= controller.GetActiveEnvironmentIndex() == target;
								return noPending();
							});
						const auto probed = controller.GetEntries();
						return settled && kept && probeIndex < probed.size() &&
							probed[probeIndex].m_State == expected;
					};
				context.Check(probe("Probes/InvalidDecode.hdr", EnvironmentAssetEntryState::Failed),
					"A decode failure ends Failed without replacing the active environment");
				context.Check(probe("Probes/Square.png", EnvironmentAssetEntryState::InvalidShape),
					"A non-2:1 candidate ends InvalidShape without replacing the active environment");

				// Reset synchronously commits the pinned fallback; reinitialization replaces it.
				controller.Reset();
				context.Check(!controller.GetActiveEnvironment() &&
					source.m_Committed.m_Type == EnvironmentTextureSourceType::Cubemap &&
					IsReservedTextureId(source.m_Committed.m_Content.m_Id),
					"Reset synchronously commits the pinned fallback environment");
				controller.Initialize("Skybox");
				const bool reselected = settle(noPending);
				context.Check(reselected && controller.GetActiveEnvironment() &&
					source.m_Committed.m_Type == EnvironmentTextureSourceType::Equirectangular,
					"Reinitialization replaces the fallback with a selected environment");
				controller.Reset();
			}
			asset_test::CheckQuiescent(context, harness, "Environment selection");
		}
	}

	void RunEnvironmentSelectionSelfTests(SelfTestContext& context) noexcept
	{
		RunEnvironmentSelectionTest(context);
	}
}
