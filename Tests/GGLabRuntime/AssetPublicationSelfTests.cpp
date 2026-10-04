#include "AssetPublicationSelfTests.h"
#include "AssetTestServices.h"

#include "GGLabRuntime/Diagnostics/AssetSnapshotRead.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/AssetResourcePublication.h"

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		using asset_test::AssetManagerHarness;
		using asset_test::CheckQuiescent;
		using asset_test::FindInterest;
		using asset_test::FindTexture;
		using asset_test::HasPendingPublication;
		using asset_test::HasPendingUpload;
		using asset_test::IsStreamingIdle;
		using asset_test::IsTerminal;

		[[nodiscard]] AssetStreamingIdentity MakeIdentity(
			const AssetManager::ModelLoadRequest& request) noexcept
		{
			return { .m_Kind = AssetStreamingWorkKind::Model,
				.m_StableId = request.m_ModelId.Value(), .m_Generation = request.m_Generation };
		}

		[[nodiscard]] AssetStreamingIdentity MakeIdentity(
			const AssetManager::TextureLoadRequest& request) noexcept
		{
			return { .m_Kind = AssetStreamingWorkKind::Texture,
				.m_StableId = request.m_TextureId.Value(), .m_Generation = request.m_Generation };
		}

		// Two textured triangles: every publication stage runs at least once per asset.
		[[nodiscard]] bool WriteProbeModel(const AssetManagerHarness& harness, std::string_view name,
			std::string_view sharedImage = {}) noexcept
		{
			const auto& root = harness.GetAssetRoot();
			const std::array<std::string, 2> images{
				sharedImage.empty() ? std::format("{}.Red.png", name) : std::string(sharedImage),
				std::format("{}.Blue.png", name) };
			return asset_test::WriteSolidPng(root / images[0], 255, 0, 0) &&
				asset_test::WriteSolidPng(root / images[1], 0, 0, 255) &&
				asset_test::WriteTexturedModel(root / std::format("{}.gltf", name), images);
		}

		[[nodiscard]] bool PumpUntilModelTerminal(
			AssetManagerHarness& harness, const AssetManager::ModelLoadRequest& request) noexcept
		{
			return request.IsValid() && harness.PumpUntil([&]()
				{
					const Model* model = harness.GetAssets().GetModel(request.m_ModelId);
					return model && model->m_ContentGeneration == request.m_Generation &&
						IsTerminal(model->m_State) &&
						IsStreamingIdle(harness.GetScheduling().GetStatistics());
				});
		}

		[[nodiscard]] AssetManagerHarness::CreateInfo OneStepPublicationBudget() noexcept
		{
			// One publication step and one resource creation per frame forces
			// yielding; the millisecond budget never binds.
			return { .m_FrameBudget = {
				.m_MaxResourcePublicationSteps = 1,
				.m_MaxResourcePublicationCreations = 1,
				.m_MaxResourcePublicationMilliseconds = 1000.0,
			} };
		}

		void RunIncrementalPublicationTest(SelfTestContext& context) noexcept
		{
			AssetManagerHarness harness("asset-publication-incremental", OneStepPublicationBudget());
			const bool ready = harness.IsValid() && WriteProbeModel(harness, "Incremental");
			context.Check(ready,
				"Incremental publication composes the harness and its model fixture");
			if (!ready)
			{
				return;
			}
			{
				AssetOwnerScope owner = harness.GetAssets().CreateOwnerScope();
				const AssetManager::ModelLoadRequest request =
					owner.LoadModelAsync("Incremental.gltf", TaskPriority::Normal);
				const AssetStreamingIdentity identity = MakeIdentity(request);
				uint64_t lastProcessed = 0;
				uint32_t framesWithSteps = 0;
				bool sawQueuedJob = false;
				bool halfPublished = false;
				const bool terminal = request.IsValid() && harness.PumpUntil([&]()
					{
						const AssetUploadStatistics statistics = harness.GetScheduling().GetStatistics();
						const uint64_t processed = statistics.m_ResourcePublicationQueue.m_ProcessedCount;
						framesWithSteps += processed > lastProcessed ? 1u : 0u;
						lastProcessed = processed;
						sawQueuedJob |= HasPendingPublication(statistics, identity);
						const Model* model = harness.GetAssets().GetModel(request.m_ModelId);
						halfPublished |= model && model->m_State == AssetState::Publishing &&
							!model->m_MeshInstance.empty();
						return model && IsTerminal(model->m_State) && IsStreamingIdle(statistics);
					});
				const Model* model = harness.GetAssets().GetModel(request.m_ModelId);
				const AssetStreamingQueueStatistics publication =
					harness.GetScheduling().GetStatistics().m_ResourcePublicationQueue;
				context.Check(terminal && model && model->m_State == AssetState::Ready &&
					publication.m_EnqueuedCount == 1 && publication.m_CompletedCount == 1,
					"A model publishes through exactly one resumable publication job");
				context.Check(sawQueuedJob && publication.m_ContinueCount > 1 && framesWithSteps > 1,
					"A one-step budget spreads model publication across multiple yielded frames");
				context.Check(!halfPublished,
					"A model never exposes mesh instances while it is still Publishing");
			}
			CheckQuiescent(context, harness, "Incremental publication");
		}

		struct PublicationFaultCase
		{
			const char* m_Name = nullptr;
			AssetResourcePublicationStage m_Stage = AssetResourcePublicationStage::Unknown;
			AssetResourcePublicationFaultAction m_Action = AssetResourcePublicationFaultAction::Cancel;
			AssetResourcePublicationFaultTiming m_Timing = AssetResourcePublicationFaultTiming::AfterStep;
		};

		void RunPublicationFaultTest(SelfTestContext& context, const PublicationFaultCase& fault) noexcept
		{
			AssetManagerHarness harness("asset-publication-fault", OneStepPublicationBudget());
			const bool ready = harness.IsValid() && WriteProbeModel(harness, "Fault");
			context.Check(ready,
				std::format("{}: the harness and model fixture are composed", fault.m_Name));
			if (!ready)
			{
				return;
			}
			{
				AssetOwnerScope owner = harness.GetAssets().CreateOwnerScope();
				const AssetManager::ModelLoadRequest request =
					owner.LoadModelAsync("Fault.gltf", TaskPriority::Normal);
				harness.GetControl().ArmResourcePublicationFault({
					.m_Identity = MakeIdentity(request),
					.m_Stage = fault.m_Stage,
					.m_Action = fault.m_Action,
					.m_Timing = fault.m_Timing,
					.m_TriggerOccurrence = 1,
					});
				bool halfPublished = false;
				const bool terminal = request.IsValid() && harness.PumpUntil([&]()
					{
						const Model* model = harness.GetAssets().GetModel(request.m_ModelId);
						halfPublished |= model && model->m_State == AssetState::Publishing &&
							!model->m_MeshInstance.empty();
						return model && IsTerminal(model->m_State) &&
							IsStreamingIdle(harness.GetScheduling().GetStatistics());
					});
				harness.GetControl().ClearResourcePublicationFault();
				const bool failed = fault.m_Action == AssetResourcePublicationFaultAction::Fail;
				const Model* model = harness.GetAssets().GetModel(request.m_ModelId);
				const AssetStreamingQueueStatistics publication =
					harness.GetScheduling().GetStatistics().m_ResourcePublicationQueue;
				context.Check(terminal && model &&
					model->m_State == (failed ? AssetState::Failed : AssetState::Cancelled) &&
					publication.m_FaultInjectionCount == 1 &&
					(failed ? publication.m_FailedCount : publication.m_CancelledCount) == 1 &&
					!halfPublished,
					std::format("{}: the injected fault terminates publication without exposing instances",
						fault.m_Name));

				// Rollback keeps only the owner's model lease; dependency leases taken by the
				// publication transaction are released.
				const AssetOwnershipStatistics ownership = harness.GetAssets().GetOwnershipStatistics();
				const AssetInterestActivity* modelInterest =
					FindInterest(ownership, AssetKind::Model, request.m_ModelId.Value());
				context.Check(ownership.m_LeaseCount == 1 && modelInterest &&
					modelInterest->m_LeaseCount == 1 && ownership.m_PublicationRetainCount == 0,
					std::format("{}: rollback releases every temporary dependency lease and retain",
						fault.m_Name));
			}
			CheckQuiescent(context, harness, fault.m_Name);
		}

		void RunPublicationFaultTests(SelfTestContext& context) noexcept
		{
			using Stage = AssetResourcePublicationStage;
			using Action = AssetResourcePublicationFaultAction;
			using Timing = AssetResourcePublicationFaultTiming;
			constexpr std::array cases{
				PublicationFaultCase{ "Cancel during textures", Stage::Textures },
				PublicationFaultCase{ "Cancel during materials", Stage::Materials },
				PublicationFaultCase{ "Cancel during meshes", Stage::Meshes },
				PublicationFaultCase{ "Cancel during mesh instances", Stage::MeshInstances },
				PublicationFaultCase{ "Cancel during dependencies", Stage::Dependencies },
				PublicationFaultCase{ "Cancel before commit", Stage::Commit, Action::Cancel, Timing::BeforeStep },
				PublicationFaultCase{ "Fail during materials", Stage::Materials, Action::Fail },
			};
			for (const PublicationFaultCase& fault : cases)
			{
				RunPublicationFaultTest(context, fault);
			}
		}

		void RunSharedTextureRollbackTest(SelfTestContext& context) noexcept
		{
			AssetManagerHarness harness("asset-publication-shared-rollback", OneStepPublicationBudget());
			const bool ready = harness.IsValid() && WriteProbeModel(harness, "Shared", "Shared.png");
			context.Check(ready,
				"Shared rollback composes the harness and a model sharing a texture");
			if (!ready)
			{
				return;
			}
			{
				AssetOwnerScope textureOwner = harness.GetAssets().CreateOwnerScope();
				const AssetManager::TextureLoadRequest texture = textureOwner.LoadTextureAsync(
					"Shared.png", TextureSemantic::BaseColor, TaskPriority::Normal);
				const bool textureReady = texture.IsValid() && harness.PumpUntil([&]()
					{
						const AssetSnapshot snapshot = BuildAssetSnapshot(harness.GetAssets());
						const AssetSnapshot::Texture* entry = FindTexture(snapshot, texture.m_TextureId);
						return entry && entry->m_State == AssetState::Ready && entry->m_IsUploaded;
					});

				AssetOwnerScope modelOwner = harness.GetAssets().CreateOwnerScope();
				const AssetManager::ModelLoadRequest model =
					modelOwner.LoadModelAsync("Shared.gltf", TaskPriority::Normal);
				harness.GetControl().ArmResourcePublicationFault({
					.m_Identity = MakeIdentity(model),
					.m_Stage = AssetResourcePublicationStage::Materials,
					.m_Action = AssetResourcePublicationFaultAction::Fail,
					.m_TriggerOccurrence = 1,
					});
				const bool modelTerminal = PumpUntilModelTerminal(harness, model);
				harness.GetControl().ClearResourcePublicationFault();
				const Model* modelEntry = harness.GetAssets().GetModel(model.m_ModelId);

				const AssetSnapshot snapshot = BuildAssetSnapshot(harness.GetAssets());
				const AssetSnapshot::Texture* shared = FindTexture(snapshot, texture.m_TextureId);
				const AssetOwnershipStatistics ownership = harness.GetAssets().GetOwnershipStatistics();
				const AssetInterestActivity* sharedInterest =
					FindInterest(ownership, AssetKind::Texture, texture.m_TextureId.Value());
				context.Check(textureReady && modelTerminal && modelEntry &&
					modelEntry->m_State == AssetState::Failed,
					"A model sharing a Ready texture fails at the injected materials stage");
				context.Check(shared && shared->m_ContentGeneration == texture.m_Generation &&
					shared->m_State == AssetState::Ready && shared->m_IsUploaded &&
					shared->m_Texture.IsValid() && sharedInterest && sharedInterest->m_OwnerCount >= 1 &&
					ownership.m_PublicationRetainCount == 0,
					"Rollback keeps the shared Ready texture and its external owner interest intact");
			}
			CheckQuiescent(context, harness, "Shared texture rollback");
		}

		void RunOwnershipPriorityMergeTest(SelfTestContext& context) noexcept
		{
			AssetManagerHarness harness("asset-ownership-priority");
			const bool ready = harness.IsValid() &&
				asset_test::WriteSolidPng(harness.GetAssetRoot() / "Priority.png", 0, 255, 0);
			context.Check(ready,
				"Ownership priority merge composes the harness and its texture fixture");
			if (!ready)
			{
				return;
			}
			{
				AssetOwnerScope background = harness.GetAssets().CreateOwnerScope();
				AssetOwnerScope critical = harness.GetAssets().CreateOwnerScope();
				const AssetManager::TextureLoadRequest backgroundRequest = background.LoadTextureAsync(
					"Priority.png", TextureSemantic::BaseColor, TaskPriority::Background);
				const AssetManager::TextureLoadRequest criticalRequest = critical.LoadTextureAsync(
					"Priority.png", TextureSemantic::BaseColor, TaskPriority::Critical);
				const uint64_t updatesBefore =
					harness.GetAssets().GetOwnershipStatistics().m_PriorityUpdateCount;
				context.Check(backgroundRequest.IsValid() && criticalRequest.IsValid() &&
					backgroundRequest.m_TextureId == criticalRequest.m_TextureId &&
					backgroundRequest.m_Generation == criticalRequest.m_Generation,
					"Two owners acquire the same texture content version");

				const AssetOwnershipStatistics merged = harness.GetAssets().GetOwnershipStatistics();
				const AssetInterestActivity* mergedInterest =
					FindInterest(merged, AssetKind::Texture, backgroundRequest.m_TextureId.Value());
				context.Check(mergedInterest && mergedInterest->m_Generation == backgroundRequest.m_Generation &&
					mergedInterest->m_LeaseCount == 2 && mergedInterest->m_OwnerCount == 2 &&
					mergedInterest->m_EffectivePriority == TaskPriority::Critical,
					"Leases from two owners merge to the highest owner priority");

				critical = {};
				const AssetOwnershipStatistics reduced = harness.GetAssets().GetOwnershipStatistics();
				const AssetInterestActivity* reducedInterest =
					FindInterest(reduced, AssetKind::Texture, backgroundRequest.m_TextureId.Value());
				context.Check(reducedInterest && reducedInterest->m_LeaseCount == 1 &&
					reducedInterest->m_OwnerCount == 1 &&
					reducedInterest->m_EffectivePriority == TaskPriority::Background &&
					reduced.m_OwnerCount == 1 && reduced.m_PriorityUpdateCount >= updatesBefore + 1,
					"Releasing the critical owner restores background priority and its owner count");
				const bool settled = harness.PumpUntil([&]()
					{
						const AssetSnapshot snapshot = BuildAssetSnapshot(harness.GetAssets());
						const AssetSnapshot::Texture* entry =
							FindTexture(snapshot, backgroundRequest.m_TextureId);
						return entry && IsTerminal(entry->m_State) &&
							IsStreamingIdle(harness.GetScheduling().GetStatistics());
					});
				context.Check(settled, "The background-priority texture load settles after the merge");
			}
			CheckQuiescent(context, harness, "Ownership priority merge");
		}

		void RunGpuSubmittedCancellationTest(SelfTestContext& context) noexcept
		{
			AssetManagerHarness harness("asset-gpu-cancellation");
			const bool ready = harness.IsValid() &&
				asset_test::WriteSolidPng(harness.GetAssetRoot() / "Held.png", 255, 255, 0);
			context.Check(ready,
				"GPU submitted cancellation composes the harness and its texture fixture");
			if (!ready)
			{
				return;
			}
			{
				AssetOwnerScope owner = harness.GetAssets().CreateOwnerScope();
				const AssetManager::TextureLoadRequest request =
					owner.LoadTextureAsync("Held.png", TextureSemantic::BaseColor, TaskPriority::Normal);
				const AssetStreamingIdentity identity = MakeIdentity(request);
				harness.GetControl().ArmGpuCompletionHold(identity);
				const bool submitted = request.IsValid() && harness.PumpUntil([&]()
					{ return HasPendingUpload(harness.GetScheduling().GetStatistics(), identity); });

				const AssetSnapshot held = BuildAssetSnapshot(harness.GetAssets());
				const AssetSnapshot::Texture* heldTexture = FindTexture(held, request.m_TextureId);
				context.Check(submitted && heldTexture && heldTexture->m_State == AssetState::GpuProcessing &&
					heldTexture->m_Texture.IsValid(),
					"A held upload remains observable as an allocated GPU resource");

				owner.Reset();
				const AssetSnapshot cancelled = BuildAssetSnapshot(harness.GetAssets());
				const AssetSnapshot::Texture* cancelledTexture = FindTexture(cancelled, request.m_TextureId);
				context.Check(cancelledTexture && cancelledTexture->m_State == AssetState::GpuProcessing &&
					cancelledTexture->m_Texture.IsValid(),
					"Cancelling after submission keeps the texture alive until its fence finalizes");

				harness.GetControl().ClearGpuCompletionHold();
				const bool finalized = harness.PumpUntil([&]()
					{
						const AssetSnapshot snapshot = BuildAssetSnapshot(harness.GetAssets());
						const AssetSnapshot::Texture* entry = FindTexture(snapshot, request.m_TextureId);
						const AssetUploadStatistics statistics = harness.GetScheduling().GetStatistics();
						return entry && entry->m_State == AssetState::Cancelled &&
							!HasPendingUpload(statistics, identity) && IsStreamingIdle(statistics);
					});
				const AssetSnapshot after = BuildAssetSnapshot(harness.GetAssets());
				const AssetSnapshot::Texture* finalTexture = FindTexture(after, request.m_TextureId);
				context.Check(finalized && finalTexture && !finalTexture->m_Texture.IsValid() &&
					!finalTexture->m_IsUploaded &&
					harness.GetAssets().GetOwnershipStatistics().m_GpuDeferredCancellationCount == 1,
					"Fence-safe finalization releases the cancelled texture and records one deferred cancellation");
			}
			CheckQuiescent(context, harness, "GPU submitted cancellation");
		}

		void RunHarnessLoadTests(SelfTestContext& context) noexcept
		{
			AssetManagerHarness harness("asset-lifecycle-load");
			context.Check(harness.IsValid(), "The headless asset harness composes an AssetManager");
			if (!harness.IsValid())
			{
				return;
			}
			const auto& root = harness.GetAssetRoot();
			const std::array<std::string, 2> images{ "Red.png", "Green.png" };
			const bool fixtures = asset_test::WriteSolidPng(root / images[0], 255, 0, 0) &&
				asset_test::WriteSolidPng(root / images[1], 0, 255, 0) &&
				asset_test::WriteTexturedModel(root / "Probe.gltf", images);
			context.Check(fixtures, "Asset fixtures are written into the harness asset root");
			{
				AssetOwnerScope owner = harness.GetAssets().CreateOwnerScope();
				const AssetManager::TextureLoadRequest texture =
					owner.LoadTextureAsync("Red.png", TextureSemantic::BaseColor, TaskPriority::Normal);
				const bool textureReady = texture.IsValid() && harness.PumpUntil([&]()
					{
						const AssetSnapshot snapshot = BuildAssetSnapshot(harness.GetAssets());
						const AssetSnapshot::Texture* entry = FindTexture(snapshot, texture.m_TextureId);
						return entry && IsTerminal(entry->m_State);
					});
				const AssetSnapshot textureSnapshot = BuildAssetSnapshot(harness.GetAssets());
				const AssetSnapshot::Texture* textureEntry =
					FindTexture(textureSnapshot, texture.m_TextureId);
				context.Check(textureReady && textureEntry && textureEntry->m_State == AssetState::Ready &&
					textureEntry->m_IsUploaded && textureEntry->m_Texture.IsValid(),
					"A texture loads, uploads and publishes through the headless harness");

				const AssetManager::ModelLoadRequest model =
					owner.LoadModelAsync("Probe.gltf", TaskPriority::Normal);
				const bool modelTerminal = PumpUntilModelTerminal(harness, model);
				const Model* modelEntry = harness.GetAssets().GetModel(model.m_ModelId);
				context.Check(modelTerminal && modelEntry && modelEntry->m_State == AssetState::Ready &&
					modelEntry->m_MeshInstance.size() == images.size(),
					"A textured model publishes every mesh instance through the headless harness");
			}
			CheckQuiescent(context, harness, "Harness load");
		}
	}

	void RunAssetPublicationSelfTests(SelfTestContext& context) noexcept
	{
		RunHarnessLoadTests(context);
		RunIncrementalPublicationTest(context);
		RunPublicationFaultTests(context);
		RunSharedTextureRollbackTest(context);
		RunOwnershipPriorityMergeTest(context);
		RunGpuSubmittedCancellationTest(context);
	}
}
