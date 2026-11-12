#include "Diagnostics/Builders/RenderGraphSnapshotBuilder.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Base/EnumFlags.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAO.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/LayoutContract.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFramePlan.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalFrameTransaction.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessColorState.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RHI/RHIBindingLayout.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineBlackboard.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/RenderViewTypes.h"
#include "GGLabRuntime/Graphics/Resource/TransientResourcePool.h"
#include "GGLabTestCore/SelfTest.h"
#include "Graphics/Pipeline/GTAOTemporalHistory.h"
#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "Graphics/Pipeline/TemporalReferenceAccumulator.h"
#include "Graphics/RenderPass/GTAOGraphResources.h"
#include "Graphics/RenderPass/RenderPassTemporalReference.h"
#include "Graphics/RenderPass/TemporalAAGraphResources.h"
#include "Graphics/RenderPass/TemporalReferenceGraphResources.h"
#include "Graphics/Resource/PersistentTexturePool.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"
#include "RenderingTestServices.h"
#include "ShaderArtifactRuntime/ShaderProgramRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>

namespace gglab::rendering_test
{
	struct TemporalHistoryContractPassData
	{
		TemporalHistoryRenderGraphResources m_History;
	};

	struct TemporalReferenceContractPassData
	{
		TemporalReferenceRenderGraphResources m_Sums;
	};

	struct GTAOTemporalHistoryContractPassData
	{
		GTAOTemporalHistoryRenderGraphResources m_History;
	};

	void RunTemporalReferenceContractTests(SelfTestContext& context) noexcept
	{
		const auto disabled = RenderPassTemporalReference::GetSourceEligibility(
			TemporalReferenceSignal::AmbientOcclusion, GTAOFrameStatus::Disabled);
		const auto unsupported = RenderPassTemporalReference::GetSourceEligibility(
			TemporalReferenceSignal::AmbientOcclusion, GTAOFrameStatus::CoreCapabilityUnavailable);
		context.Check(disabled == TemporalReferenceSourceEligibility::GTAODisabled &&
			unsupported == TemporalReferenceSourceEligibility::GTAOCoreCapabilityUnavailable,
			"AO source eligibility distinguishes disabled GTAO from missing core capabilities");
		const std::array gtaoStatuses{ GTAOFrameStatus::Disabled, GTAOFrameStatus::Active,
			GTAOFrameStatus::CoreCapabilityUnavailable, GTAOFrameStatus::NoOpaqueDraws };
		context.Check(RenderPassTemporalReference::GetSourceEligibility(
			TemporalReferenceSignal::AmbientOcclusion, GTAOFrameStatus::Active) == TemporalReferenceSourceEligibility::Eligible &&
			RenderPassTemporalReference::GetSourceEligibility(
				TemporalReferenceSignal::AmbientOcclusion, GTAOFrameStatus::NoOpaqueDraws) == TemporalReferenceSourceEligibility::Eligible &&
			std::ranges::all_of(gtaoStatuses, [](GTAOFrameStatus status)
				{
					return RenderPassTemporalReference::GetSourceEligibility(
						TemporalReferenceSignal::SceneColor, status) == TemporalReferenceSourceEligibility::Eligible;
				}),
			"AO references accept active and empty input; scene-color references do not require GTAO");

		bool productionPhasesMatch = true;
		for (uint32_t index = 0; index < temporal::JitterSampleCount; ++index)
		{
			const Vector2 reference = GetTemporalReferenceJitterPixels(index);
			const Vector2 production = temporal::GetJitterSamplePixels(index);
			productionPhasesMatch = productionPhasesMatch &&
				std::abs(reference.m_X - production.m_X) < 1.0e-6f &&
				std::abs(reference.m_Y - production.m_Y) < 1.0e-6f;
		}
		bool phasesInsidePixel = true;
		for (uint32_t index = 0; index < 256; ++index)
		{
			const Vector2 phase = GetTemporalReferenceJitterPixels(index);
			phasesInsidePixel = phasesInsidePixel && phase.m_X >= -0.5f && phase.m_X < 0.5f &&
				phase.m_Y >= -0.5f && phase.m_Y < 0.5f;
		}
		context.Check(productionPhasesMatch && phasesInsidePixel &&
			TemporalReferenceSample{ .m_Index = 3, .m_Count = 4 }.IsValid() &&
			!TemporalReferenceSample{ .m_Index = 4, .m_Count = 4 }.IsValid() &&
			!TemporalReferenceSample{ .m_Index = 0, .m_Count = 0 }.IsValid() &&
			!TemporalReferenceSample{ .m_Index = 0,
				.m_Count = MaxTemporalReferenceSamples + 1 }.IsValid(),
			"Reference phases extend the production Halton(2, 3) sequence inside the pixel");

		RecordingDevice device;
		device.m_UseControlledFenceCompletion = true;
		PersistentTexturePool pool(&device);
		TemporalReferenceAccumulator accumulator(&pool);
		const RHIFencePoint fence{ RHIFenceHandle{ 1, 1 }, 5 };
		auto accumulate = [&](uint32_t index, uint32_t count, bool submit,
			bool& outPreviousValid, uint32_t width = 8, uint32_t height = 4)
		{
			if (!accumulator.BeginFrame(TemporalReferenceSample{ .m_Index = index,
				.m_Count = count }, width, height, fence))
			{
				return false;
			}
			RenderGraph graph({
				.m_Device = &device,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			bool imported = false;
			bool exported = false;
			graph.AddPass<TemporalReferenceContractPassData>("TemporalReference.Contract",
				[&](RenderGraph::RGBuilder& builder, TemporalReferenceContractPassData& data)
				{
					imported = accumulator.ImportRenderGraphResources(builder, data.m_Sums);
					outPreviousValid = data.m_Sums.m_PreviousValid;
					if (data.m_Sums.m_PreviousValid)
					{
						data.m_Sums.m_PreviousSum = builder.Read(data.m_Sums.m_PreviousSum, RGTextureAccess::Sample);
					}
					builder.WriteInPlace(data.m_Sums.m_NextSum, RGTextureAccess::StorageWrite);
					exported = accumulator.ExportRenderGraphResources(builder, data.m_Sums);
				});
			const bool compiled = graph.Compile();
			if (submit)
			{
				accumulator.CommitFrame();
			}
			else
			{
				accumulator.AbortFrame();
			}
			return imported && exported && compiled;
		};

		bool firstPrevious = true;
		bool secondPrevious = false;
		bool repeatedPrevious = false;
		const bool first = accumulate(0, 3, true, firstPrevious);
		const bool unsubmitted = accumulate(1, 3, false, secondPrevious);
		const uint32_t afterAbort = accumulator.GetCommittedSampleCount();
		const bool repeated = accumulate(1, 3, true, repeatedPrevious);
		context.Check(first && !firstPrevious && unsubmitted && secondPrevious &&
			afterAbort == 1 && repeated && repeatedPrevious &&
			accumulator.GetCommittedSampleCount() == 2 &&
			pool.GetDiagnostics().m_ActiveTextureCount == 2,
			"Each submitted sample commits the next sum; an unsubmitted sample is repeated");

		bool skippedPrevious = false;
		const bool skipped = accumulator.BeginFrame(
			TemporalReferenceSample{ .m_Index = 3, .m_Count = 4 }, 8, 4, fence);
		bool restartPrevious = true;
		const bool restarted = accumulate(0, 4, true, restartPrevious);
		context.Check(!skipped && !skippedPrevious && restarted && !restartPrevious &&
			accumulator.GetCommittedSampleCount() == 1,
			"A sample that does not follow the committed sum is rejected; sample 0 restarts");

		bool resizedPrevious = true;
		const bool resized = accumulate(0, 2, true, resizedPrevious, 16, 8);
		const PersistentTexturePoolDiagnostics resizedPool = pool.GetDiagnostics();
		const bool idle = accumulator.BeginFrame(std::nullopt, 16, 8, fence);
		const PersistentTexturePoolDiagnostics idlePool = pool.GetDiagnostics();
		context.Check(resized && !resizedPrevious && resizedPool.m_ActiveTextureCount == 2 &&
			resizedPool.m_PendingRetirementTextureCount == 2 && idle &&
			idlePool.m_ActiveTextureCount == 0 && idlePool.m_PendingRetirementTextureCount == 4 &&
			accumulator.GetCommittedSampleCount() == 0,
			"A new extent reallocates the sum pair and a frame without a sample retires it "
			"through the fence");

		TemporalViewHistory viewHistory;
		TemporalObjectHistory objectHistory;
		TemporalFrameTransaction transaction;
		const ResolvedTemporalFramePlan referencePlan = ResolveTemporalFramePlan({ .m_ReferenceRequested = true });
		transaction.Begin(viewHistory, objectHistory, referencePlan, ResolveNativeViewResolution({ 8, 4 }),
			nullptr, 1.0f, TemporalReferenceSample{ .m_Index = 5, .m_Count = 16 }, nullptr);
		const Vector2 expected = GetTemporalReferenceJitterPixels(5);
		context.Check(transaction.GetReferenceSample() &&
			transaction.GetJitterIndex() == 5 && !transaction.CanAccumulateReference() &&
			transaction.GetJitterPixels().m_X == expected.m_X &&
			transaction.GetJitterPixels().m_Y == expected.m_Y,
			"A reference frame jitters with its sample phase while Temporal AA is inactive");
		transaction.Abort();

		class ShaderAccess final : public RenderShaderProgramAccess
		{
		public:
			ShaderID LoadProgram(const ShaderProgramRef&) noexcept override { return ShaderID{ 1 }; }
			uint64_t GetGeneration(ShaderID) const noexcept override { return 1; }
		} shaders;
		class BindingAccess final : public RenderBindingLayoutAccess
		{
		public:
			RHIBindingLayoutHandle GetCommonBindingLayout() const noexcept override { return { 1, 1 }; }
			RHIBindingLayoutDesc GetCommonBindingLayoutDesc() const noexcept override { return {}; }
			const ComposedLayout* GetCommonLayoutContract() const noexcept override
			{
				return &scene_bindings::GetCommonLayoutContract();
			}
		} bindings;
		const RenderServices services{ .m_ShaderPrograms = &shaders, .m_BindingLayout = &bindings };
		RenderPassTemporalReference pass;
		pass.Prepare(services);
		auto recordAO = [&](uint32_t index, bool empty, bool submit)
		{
			const TemporalReferenceSample sample{ .m_Index = index, .m_Count = 3,
				.m_Signal = TemporalReferenceSignal::AmbientOcclusion };
			if (!accumulator.BeginFrame(sample, 8, 4, fence))
			{
				return false;
			}
			transaction.Begin(viewHistory, objectHistory, referencePlan,
				ResolveNativeViewResolution({ 8, 4 }), nullptr, 1.0f, sample, &accumulator);
			RenderGraph graph({ .m_Device = &device,
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }) });
			graph.AddPass<TemporalReferenceContractPassData>("TemporalReference.Input",
				[empty](RenderGraph::RGBuilder& builder, TemporalReferenceContractPassData&)
				{
					auto& targets = builder.GetBlackboard().Create<RGViewTargetsTable>(ViewTargetsTableName).GetViewTargets(RenderViewID::Main);
					targets.m_DisplayWidth = 8;
					targets.m_DisplayHeight = 4;
					targets.m_RenderWidth = 8;
					targets.m_RenderHeight = 4;
					const RHITextureDesc desc{ .m_Format = RHIFormat::R16G16B16A16Float,
						.m_Extent = { 8, 4, 1 } };
					targets.m_DisplayColor = builder.CreateTexture("ReferenceTest.Scene", desc);
					builder.WriteInPlace(targets.m_DisplayColor, RGTextureAccess::RenderTarget);
					auto& gtao = builder.GetBlackboard().Create<RGGTAOResources>(GTAOResourcesName);
					gtao.m_Status = empty ? GTAOFrameStatus::NoOpaqueDraws : GTAOFrameStatus::Active;
					if (!empty)
					{
						gtao.m_FinalAO = builder.CreateTexture("ReferenceTest.AO", desc);
						builder.WriteInPlace(gtao.m_FinalAO, RGTextureAccess::StorageWrite);
						gtao.m_RawAO = gtao.m_HalfDepthViewZ = gtao.m_DenoiseX =
							gtao.m_DenoiseY = gtao.m_FinalAO;
						gtao.m_HalfWidth = 4;
						gtao.m_HalfHeight = 2;
						gtao.m_FinalAOFormat = desc.m_Format;
					}
				});
			RenderFrameBuildResult frame{};
			frame.m_TemporalFrameTransaction = &transaction;
			const auto frameContext = frame.MakeRenderFrameContext();
			pass.AddPass(graph, frameContext, services);
			auto& blackboard = graph.GetBlackboard();
			const auto* reference = blackboard.TryGet<RGTemporalReferenceResources>(TemporalReferenceResourcesName);
			bool valid = reference && reference->m_MeanAmbientOcclusion.IsValid() &&
				blackboard.Get<RGGTAOResources>(GTAOResourcesName).IsComplete() != empty &&
				graph.Compile();
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const auto clear = std::ranges::find(snapshot.m_Passes, "TemporalReference.UnoccludedAO", &RGSnapshotPassInfo::m_Name);
			const auto accumulate = std::ranges::find(snapshot.m_Passes, "PostProcess.TemporalReference", &RGSnapshotPassInfo::m_Name);
			valid = valid && accumulate != snapshot.m_Passes.end() && !accumulate->m_Culled;
			if (empty)
			{
				const auto visibility = std::ranges::find(snapshot.m_Resources,
					"TemporalReference.UnoccludedAO", &RGSnapshotResourceInfo::m_Name);
				valid = valid && clear != snapshot.m_Passes.end() && !clear->m_Culled &&
					clear->m_ExecutionOrder < accumulate->m_ExecutionOrder &&
					visibility != snapshot.m_Resources.end() &&
					visibility->m_TextureExtent.m_Width == 8 &&
					visibility->m_TextureExtent.m_Height == 4 &&
					visibility->m_TextureClearValue &&
					visibility->m_TextureClearValue->m_Color[0] == 1.0f;
			}
			else
			{
				valid = valid && clear == snapshot.m_Passes.end();
			}
			if (submit)
			{
				transaction.CommitCompleted(fence);
			}
			else
			{
				transaction.Abort(fence);
			}
			return valid && accumulator.GetCommittedSampleCount() == (submit ? index + 1 : index);
		};
		const bool activeAO = recordAO(0, false, true);
		const bool abortedEmptyAO = recordAO(1, true, false);
		const bool emptyAO = recordAO(1, true, true);
		const bool resumedAO = recordAO(2, false, true);
		context.Check(activeAO && abortedEmptyAO && emptyAO && resumedAO,
			"The reference pass accumulates full-size unoccluded input between active AO samples, "
			"publishes its mean without fabricating GTAO outputs, and repeats aborted samples");
		accumulator.Release(fence);
		device.m_CompletedFenceValue = fence.m_Value;
		pool.Tick();
	}

	void RunGTAOTemporalHistoryContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		device.m_UseControlledFenceCompletion = true;
		PersistentTexturePool pool(&device);
		GTAOTemporalHistory history(&pool);
		const RHIFencePoint fence{ RHIFenceHandle{ 1, 1 }, 5 };
		const GTAOExtent halfExtent = MakeGTAOHalfResolutionExtent(16, 8);
		struct HistoryFrame
		{
			bool m_Begun = false;
			bool m_Imported = false;
			bool m_Exported = false;
			bool m_PreviousValid = false;
			bool m_PreviousFrameCompatible = false;
			bool m_Compiled = false;
		};
		// Records one active temporal GTAO frame. A frame that does not fully write the
		// next pair cannot export it.
		const auto recordFrame = [&](bool continuesPreviousView, bool writesNext) noexcept
		{
			HistoryFrame frame{ .m_Begun = history.BeginFrame(true, halfExtent, fence) };
			frame.m_PreviousFrameCompatible = history.HasCompatiblePreviousFrame(continuesPreviousView);
			RenderGraph graph({
				.m_Device = &device,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			graph.AddPass<GTAOTemporalHistoryContractPassData>("GTAO.TemporalHistory.Contract",
				[&](RenderGraph::RGBuilder& builder, GTAOTemporalHistoryContractPassData& data)
				{
					frame.m_Imported = history.ImportRenderGraphResources(builder, continuesPreviousView, data.m_History);
					frame.m_PreviousValid = data.m_History.m_PreviousValid;
					if (data.m_History.m_PreviousValid)
					{
						data.m_History.m_PreviousVisibility = builder.Read(data.m_History.m_PreviousVisibility, RGTextureAccess::Sample);
						data.m_History.m_PreviousViewZ = builder.Read(data.m_History.m_PreviousViewZ, RGTextureAccess::Sample);
					}
					if (writesNext)
					{
						builder.WriteInPlace(data.m_History.m_NextVisibility, RGTextureAccess::StorageWrite);
						builder.WriteInPlace(data.m_History.m_NextViewZ, RGTextureAccess::StorageWrite);
					}
					frame.m_Exported = history.ExportRenderGraphResources(builder, data.m_History);
				});
			frame.m_Compiled = graph.Compile();
			return frame;
		};
		const auto recorded = [](const HistoryFrame& frame, bool exported) noexcept
		{
			return frame.m_Begun && frame.m_Imported && frame.m_Exported == exported &&
				frame.m_Compiled;
		};

		const HistoryFrame unsubmitted = recordFrame(true, true);
		history.AbortFrame();
		const bool committedAfterAbort = history.HasCommittedHistory();
		const HistoryFrame first = recordFrame(true, true);
		history.CommitFrame();
		const bool committedAfterSubmit = history.HasCommittedHistory();
		const HistoryFrame continued = recordFrame(true, true);
		history.CommitFrame();
		context.Check(recorded(unsubmitted, true) && !unsubmitted.m_PreviousValid &&
			!committedAfterAbort && recorded(first, true) && !first.m_PreviousValid &&
			committedAfterSubmit && recorded(continued, true) && continued.m_PreviousValid &&
			pool.GetDiagnostics().m_ActiveTextureCount == 4,
			"Temporal GTAO commits an exported history only when its frame is submitted, and "
			"the next frame that continues the view reads it");

		const auto stageEmptyFrame = [&]() noexcept
		{
			return history.BeginFrame(true, halfExtent, fence) && history.StageEmptyFrame();
		};
		const bool abortedEmptyStaged = stageEmptyFrame();
		history.AbortFrame();
		const HistoryFrame afterEmptyAbort = recordFrame(true, true);
		const bool stagedAfterImport = history.StageEmptyFrame();
		history.CommitFrame();
		context.Check(abortedEmptyStaged && recorded(afterEmptyAbort, true) &&
			afterEmptyAbort.m_PreviousValid && !stagedAfterImport,
			"An aborted empty GTAO frame preserves samples, and an imported frame cannot "
			"be relabeled empty");

		const bool emptyStaged = stageEmptyFrame();
		bool importedAfterEmpty = true;
		RenderGraph emptyGraph({ .m_Device = &device,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }) });
		emptyGraph.AddPass<GTAOTemporalHistoryContractPassData>("GTAO.TemporalHistory.Empty",
			[&](RenderGraph::RGBuilder& builder, GTAOTemporalHistoryContractPassData& data)
			{
				importedAfterEmpty = history.ImportRenderGraphResources(builder, true, data.m_History);
				builder.SideEffect();
			});
		const bool emptyCompiled = emptyGraph.Compile();
		history.CommitFrame();
		const bool samplesAfterEmpty = history.HasCommittedHistory();
		const bool nextEmptyBegun = history.BeginFrame(true, halfExtent, fence);
		const bool emptyFrameCompatible = history.HasCompatiblePreviousFrame(true);
		const bool emptySamplesCompatible = history.HasCompatiblePreviousHistory(true);
		const bool emptyCutCompatible = history.HasCompatiblePreviousFrame(false);
		const bool nextEmptyStaged = history.StageEmptyFrame();
		history.CommitFrame();
		const HistoryFrame resumed = recordFrame(true, true);
		history.CommitFrame();
		const HistoryFrame afterResumed = recordFrame(true, true);
		history.CommitFrame();
		context.Check(emptyStaged && !importedAfterEmpty && emptyCompiled && !samplesAfterEmpty &&
			nextEmptyBegun && emptyFrameCompatible && !emptySamplesCompatible && !emptyCutCompatible &&
			nextEmptyStaged && recorded(resumed, true) && resumed.m_PreviousFrameCompatible &&
			!resumed.m_PreviousValid && recorded(afterResumed, true) && afterResumed.m_PreviousValid,
			"Submitted empty GTAO frames preserve continuity without importing resources or "
			"reusing samples from before the empty interval; returning geometry starts fresh samples");

		const HistoryFrame cut = recordFrame(false, true);
		history.CommitFrame();
		const HistoryFrame unexported = recordFrame(true, false);
		history.CommitFrame();
		const bool committedAfterUnexported = history.HasCommittedHistory();
		const HistoryFrame afterUnexported = recordFrame(true, true);
		history.CommitFrame();
		context.Check(recorded(cut, true) && !cut.m_PreviousValid &&
			recorded(unexported, false) && unexported.m_PreviousValid &&
			!committedAfterUnexported && recorded(afterUnexported, true) &&
			!afterUnexported.m_PreviousValid && !afterUnexported.m_PreviousFrameCompatible &&
			history.HasCommittedHistory(),
			"Temporal GTAO reads no previous history when the view does not continue or the "
			"last submitted frame exported none");

		const PersistentTexturePoolDiagnostics activePool = pool.GetDiagnostics();
		const bool inactiveBegun = history.BeginFrame(false, halfExtent, fence);
		const PersistentTexturePoolDiagnostics inactivePool = pool.GetDiagnostics();
		bool inactiveImported = true;
		RenderGraph inactiveGraph({
			.m_Device = &device,
			.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		inactiveGraph.AddPass<GTAOTemporalHistoryContractPassData>(
			"GTAO.TemporalHistory.Inactive",
			[&](RenderGraph::RGBuilder& builder, GTAOTemporalHistoryContractPassData& data)
			{
				inactiveImported = history.ImportRenderGraphResources(builder, true, data.m_History);
				builder.SideEffect();
			});
		const bool inactiveCompiled = inactiveGraph.Compile();
		const HistoryFrame reacquired = recordFrame(true, true);
		history.CommitFrame();
		context.Check(inactiveBegun && activePool.m_ActiveTextureCount == 4 &&
			inactivePool.m_ActiveTextureCount == 0 &&
			inactivePool.m_PendingRetirementTextureCount == activePool.m_PendingRetirementTextureCount + 4 &&
			!inactiveImported && inactiveCompiled && recorded(reacquired, true) &&
			!reacquired.m_PreviousValid && pool.GetDiagnostics().m_ActiveTextureCount == 4,
			"An inactive frame retires the temporal GTAO pairs through the fence, and a later "
			"active frame reallocates them without previous history");

		const ResolvedTemporalFramePlan ambientOcclusionPlan = ResolveTemporalFramePlan({
			.m_DisplayViewId = RenderViewID::Main,
			.m_DisplayViewEligible = true,
			.m_DepthVelocityPathAvailable = true,
			.m_AmbientOcclusionRequested = true,
			.m_AmbientOcclusionAvailable = true,
		});
		TemporalViewHistory viewHistory;
		TemporalObjectHistory objectHistory;
		const bool committedBeforeFatal = history.HasCommittedHistory();
		const bool fatalBegun = history.BeginFrame(true, halfExtent, fence);
		TemporalFrameTransaction transaction;
		transaction.Begin(viewHistory, objectHistory, ambientOcclusionPlan,
			ResolveNativeViewResolution({ 16, 8 }), nullptr, SceneColorStoragePreExposureV1,
			std::nullopt, nullptr, &history);
		const bool fatalCanAccumulate = transaction.CanAccumulateAmbientOcclusion();
		const PersistentTexturePoolDiagnostics beforeFatal = pool.GetDiagnostics();
		transaction.InvalidateAfterFatal(fence);
		const PersistentTexturePoolDiagnostics afterFatal = pool.GetDiagnostics();
		context.Check(ambientOcclusionPlan.UsesAmbientOcclusionHistory() &&
			committedBeforeFatal && fatalBegun && fatalCanAccumulate &&
			transaction.GetState() == TemporalFrameTransactionState::Invalidated &&
			!history.HasCommittedHistory() && beforeFatal.m_ActiveTextureCount == 4 &&
			afterFatal.m_ActiveTextureCount == 0 &&
			afterFatal.m_PendingRetirementTextureCount == beforeFatal.m_PendingRetirementTextureCount + 4,
			"A fatal submission releases the temporal GTAO history through the fence instead "
			"of committing or keeping it");
		device.m_CompletedFenceValue = fence.m_Value;
		pool.Tick();
	}

	void RunTemporalCaptureContinuityContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice device;
		PersistentTexturePool pool(&device);
		TemporalHistoryManager colorHistory(&pool);
		GTAOTemporalHistory ambientOcclusionHistory(&pool);
		TemporalViewHistory viewHistory;
		TemporalObjectHistory objectHistory;
		TemporalFramePlanResolveInfo info{
			.m_TemporalAA = {
				.m_Settings = { .m_Enabled = true },
				.m_Capabilities = {
					.m_MotionRenderTarget = true, .m_MotionShaderResource = true,
					.m_ResolvedColorRenderTarget = true, .m_ResolvedColorShaderResource = true,
					.m_ResolvedColorTypedUavStore = true, .m_HistoryColorShaderResource = true,
					.m_HistoryColorTypedUavStore = true, .m_HistoryDepthShaderResource = true,
					.m_HistoryDepthTypedUavStore = true, .m_HistoryReliabilityShaderResource = true,
					.m_HistoryReliabilityTypedUavStore = true },
				.m_UpscalingAvailable = true },
			.m_DisplayViewId = RenderViewID::Main,
			.m_SceneExtensionParticipation = SceneExtensionTemporalParticipation::TemporalIntegrated,
			.m_ResetIdentity = 7, .m_SessionIdentity = 11,
			.m_DisplayViewEligible = true, .m_DepthVelocityPathAvailable = true,
			.m_AmbientOcclusionAvailable = true,
		};
		uint64_t fenceValue = 1;
		const auto record = [&](bool submit = true, float preExposure = 1.0f,
			bool hasOpaqueDraws = true)
		{
			const ResolvedTemporalFramePlan plan = ResolveTemporalFramePlan(info);
			const ViewResolution resolution = ResolveTemporalAAViewResolution({ 64, 32 }, plan.m_TemporalAA.m_ResolutionPreset);
			const bool aoReady = ambientOcclusionHistory.BeginFrame(plan.UsesAmbientOcclusionHistory(),
				MakeGTAOHalfResolutionExtent(resolution.m_Render.m_Width, resolution.m_Render.m_Height), {});
			TemporalFrameTransaction transaction;
			transaction.Begin(viewHistory, objectHistory, plan, resolution, &colorHistory,
				preExposure, std::nullopt, nullptr, aoReady ? &ambientOcclusionHistory : nullptr);
			const TemporalFrameContinuity continuity = transaction.GetContinuity();
			RenderView view{ .m_ViewId = RenderViewID::Main };
			view.m_Width = resolution.m_Render.m_Width;
			view.m_Height = resolution.m_Render.m_Height;
			view.m_IsValid = true;
			transaction.PrepareDisplayView(view);
			RenderGraph graph({ .m_Device = &device,
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }) });
			bool recorded = !plan.HasService(TemporalService::FrameContinuity);
			if (plan.HasService(TemporalService::ColorDepthHistory))
			{
				graph.AddPass<TemporalHistoryContractPassData>("Continuity.Color",
					[&](RenderGraph::RGBuilder& builder, TemporalHistoryContractPassData& data)
					{
						if (!transaction.ImportHistoryResources(builder, data.m_History))
						{
							return;
						}
						builder.WriteInPlace(data.m_History.m_NextColor, RGTextureAccess::StorageWrite);
						builder.WriteInPlace(data.m_History.m_NextDepth, RGTextureAccess::StorageWrite);
						builder.WriteInPlace(data.m_History.m_NextReliability, RGTextureAccess::StorageWrite);
						recorded = transaction.ExportHistoryResources(builder, data.m_History);
					});
			}
			else if (plan.UsesAmbientOcclusionHistory() && !hasOpaqueDraws)
			{
				recorded = transaction.StageEmptyAmbientOcclusionFrame();
			}
			else if (plan.UsesAmbientOcclusionHistory())
			{
				graph.AddPass<GTAOTemporalHistoryContractPassData>("Continuity.AmbientOcclusion",
					[&](RenderGraph::RGBuilder& builder, GTAOTemporalHistoryContractPassData& data)
					{
						if (!transaction.ImportAmbientOcclusionHistory(builder, data.m_History))
						{
							return;
						}
						builder.WriteInPlace(data.m_History.m_NextVisibility, RGTextureAccess::StorageWrite);
						builder.WriteInPlace(data.m_History.m_NextViewZ, RGTextureAccess::StorageWrite);
						recorded = transaction.ExportAmbientOcclusionHistory(builder, data.m_History);
					});
			}
			context.Check(recorded && graph.Compile(), "Continuity fixture records its active history outcome");
			if (submit)
			{
				transaction.CommitCompleted({ RHIFenceHandle{ 1, 1 }, fenceValue++ });
			}
			else
			{
				transaction.Abort();
			}
			context.Check(transaction.GetContinuity().m_HasCompatiblePreviousHistory == continuity.m_HasCompatiblePreviousHistory,
				"Continuity remains the Begin-time observation after commit or abort");
			return continuity;
		};

		const auto cold = record();
		const auto native = record();
		context.Check(!cold.m_HasCompatiblePreviousHistory && native.Continues(cold),
			"Continuity starts on the first committed frame, not on an uncommitted allocation");
		const auto aborted = record(false);
		context.Check(record().Continues(aborted), "Aborting a compatible frame preserves committed continuity");
		info.m_TemporalAA.m_Settings.m_ResolutionPreset = TemporalAAResolutionPreset::Quality;
		const auto qualityAborted = record(false);
		const auto qualityCold = record();
		context.Check(!qualityAborted.Continues(native) && !qualityCold.Continues(qualityAborted) &&
			!qualityCold.m_HasCompatiblePreviousHistory && record().Continues(qualityCold),
			"A render-extent change cold-starts history and an aborted cold frame cannot warm it");
		info.m_TemporalAA.m_Settings.m_HistoryAccumulation = TemporalAAHistoryAccumulation::CompatibilityAge;
		const auto age = record();
		context.Check(!age.Continues(qualityCold) && !age.m_HasCompatiblePreviousHistory && record().Continues(age),
			"Changing accumulation resets the owner history at a fixed display extent");
		++info.m_ResetIdentity;
		const auto cut = record();
		context.Check(!cut.Continues(age) && cut.Continues(age, true),
			"Only an expected camera cut permits its reset identity and cold history");
		++info.m_ResetIdentity;
		info.m_TemporalAA.m_Settings.m_ResolutionPreset = TemporalAAResolutionPreset::Native;
		const auto cutAndResize = record();
		context.Check(!cutAndResize.Continues(cut, true), "A camera cut cannot hide a simultaneous resolution change");
		const auto beforeInvalidation = record();
		colorHistory.Invalidate(TemporalHistoryResetReason::ResolveProgramChanged);
		const auto invalidated = record();
		context.Check(!invalidated.Continues(beforeInvalidation) &&
			invalidated.m_Identity == beforeInvalidation.m_Identity,
			"Owner invalidation breaks capture continuity even when its effective identity is unchanged");
		context.Check(record(true, 4.0f).Continues(invalidated), "A valid exposure-scale change preserves continuity");
		info.m_TemporalAA.m_Settings.m_Enabled = false;
		const auto inactive = record();
		context.Check(record().Continues(inactive), "Frames without a temporal consumer still settle");
		info.m_AmbientOcclusionRequested = true;
		const auto aoCold = record();
		const auto aoContinued = record();
		context.Check(!aoCold.Continues(inactive) && !aoCold.m_HasCompatiblePreviousHistory &&
			aoContinued.Continues(aoCold), "Standalone temporal GTAO reports its actual committed read history");
		const auto aoEmptyAborted = record(false, 1.0f, false);
		const bool samplesAfterEmptyAbort = ambientOcclusionHistory.HasCommittedHistory();
		const auto aoEmpty = record(true, 1.0f, false);
		const bool samplesAfterEmpty = ambientOcclusionHistory.HasCommittedHistory();
		const auto aoStillEmpty = record(true, 1.0f, false);
		const auto aoResumed = record();
		const auto aoAfterResumed = record();
		context.Check(aoEmptyAborted.Continues(aoContinued) && samplesAfterEmptyAbort &&
			aoEmpty.Continues(aoContinued) && !samplesAfterEmpty &&
			aoStillEmpty.Continues(aoEmpty) && aoResumed.Continues(aoStillEmpty) &&
			aoAfterResumed.Continues(aoResumed),
			"Standalone GTAO capture continuity survives draws, aborted and submitted empty "
			"views, and returning draws without a camera cut");
		ambientOcclusionHistory.Release({});
		const auto aoColdEmptyAborted = record(false, 1.0f, false);
		const auto aoLost = record(true, 1.0f, false);
		const auto aoColdEmptyContinued = record(true, 1.0f, false);
		context.Check(!aoColdEmptyAborted.Continues(aoAfterResumed) &&
			!aoLost.Continues(aoColdEmptyAborted) && aoLost.m_Identity == aoAfterResumed.m_Identity &&
			aoColdEmptyContinued.Continues(aoLost),
			"A lost GTAO allocation breaks continuity even while geometric view history survives");
		ambientOcclusionHistory.Release({});
		const auto aoLostEmpty = record();
		context.Check(!aoLostEmpty.Continues(aoColdEmptyContinued),
			"Losing an empty GTAO history owner still breaks continuity");
		++info.m_ResetIdentity;
		const auto aoEmptyCut = record(true, 1.0f, false);
		context.Check(!aoEmptyCut.Continues(aoLostEmpty) && aoEmptyCut.Continues(aoLostEmpty, true) &&
			record().Continues(aoEmptyCut), "Empty GTAO frames obey camera-cut continuity rules");
		ambientOcclusionHistory.Release({});
		colorHistory.Shutdown();
		pool.Tick();
	}

	void RunTemporalHistoryTransactionContractTests(SelfTestContext& context) noexcept
	{
		RecordingDevice writeProofDevice;
		RHITextureDesc writeProofDesc{};
		writeProofDesc.m_Dimension = RHITextureDimension::Texture2D;
		writeProofDesc.m_Format = TemporalHistoryColorFormat;
		writeProofDesc.m_Usage =
			RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess;
		writeProofDesc.m_Extent = { 8, 8, 1 };
		const RHITextureHandle writeProofTexture = writeProofDevice.CreateTexture(
			RHIOwnedTextureCreateInfo{ .m_Desc = writeProofDesc }, {});
		const RHITextureHandle writeProofNextTexture = writeProofDevice.CreateTexture(
			RHIOwnedTextureCreateInfo{ .m_Desc = writeProofDesc }, {});
		RenderGraph writeProofGraph({
			.m_Device = &writeProofDevice,
			.m_TransientResourcePool =
				reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		bool initiallyDefinedCountsAsCurrentWrite = true;
		bool fullCurrentWriteRecognized = false;
		writeProofGraph.AddPass<RGTextureId>("TemporalHistory.WriteProof",
			[&](RenderGraph::RGBuilder& builder, RGTextureId& texture)
			{
				texture = builder.ImportTexture("TemporalHistory.WriteProof.Texture",
					writeProofTexture, writeProofDesc, CommonRHIResourceState(),
					RGContentValidity::Defined);
				initiallyDefinedCountsAsCurrentWrite = builder.IsTextureFullyWrittenByCurrentPass(texture);
				builder.WriteInPlace(texture, RGTextureAccess::StorageWrite);
				fullCurrentWriteRecognized = builder.IsTextureFullyWrittenByCurrentPass(texture);
				builder.Export(texture, RGTextureAccess::None);
			});
		context.Check(!initiallyDefinedCountsAsCurrentWrite && fullCurrentWriteRecognized &&
			writeProofGraph.Compile(),
			"RenderGraph distinguishes prior Defined contents from a full current-pass write");

		RenderGraph historyColorPreviewGraph({
			.m_Device = &writeProofDevice,
			.m_TransientResourcePool =
				reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
		});
		RHITextureDesc historyColorPreviewPayloadDesc = writeProofDesc;
		historyColorPreviewPayloadDesc.m_Usage = RHITextureUsage::None;
		RGTemporalAAResources historyColorPreviewResources{};
		bool historyColorPreviewUsesTransientPayload = false;
		bool historySamplesPreviewUsesTransientPayload = false;
		historyColorPreviewGraph.AddPass<TextureStorageAccessPassData>(
			"TemporalHistory.ExportPrevious",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				historyColorPreviewResources.m_History.m_PreviousColor =
					builder.ImportTexture("TAA.Preview.PreviousColor", writeProofTexture,
						writeProofDesc, CommonRHIResourceState(), RGContentValidity::Defined);
				historyColorPreviewResources.m_History.m_PreviousColor = builder.Read(
					historyColorPreviewResources.m_History.m_PreviousColor,
					RGTextureAccess::Sample);
				builder.Export(historyColorPreviewResources.m_History.m_PreviousColor, RGTextureAccess::None);
				historyColorPreviewResources.m_History.m_NextColor =
					builder.ImportTexture("TAA.Preview.NextColor", writeProofNextTexture,
						writeProofDesc, CommonRHIResourceState(), RGContentValidity::Defined);
				builder.WriteInPlace(historyColorPreviewResources.m_History.m_NextColor, RGTextureAccess::StorageWrite);
				builder.Export(historyColorPreviewResources.m_History.m_NextColor, RGTextureAccess::None);

				historyColorPreviewResources.m_ReprojectionDiagnostics =
					builder.CreateTexture("TAA.Preview.AccumulatedHistory", historyColorPreviewPayloadDesc);
				builder.WriteInPlace(historyColorPreviewResources.m_ReprojectionDiagnostics, RGTextureAccess::StorageWrite);
				data.m_Texture =
					historyColorPreviewResources.m_ReprojectionDiagnostics;
			});
		historyColorPreviewGraph.AddPass<TextureStorageAccessPassData>(
			"PostProcess.Preview.HistoryColor",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				const RGTextureId previewSource = ResolveTemporalAAPreviewSource(
					historyColorPreviewResources,
					PostProcessDebugTap::TemporalHistoryColor);
				historyColorPreviewUsesTransientPayload =
					previewSource == historyColorPreviewResources.m_ReprojectionDiagnostics &&
					previewSource != historyColorPreviewResources.m_History.m_PreviousColor &&
					previewSource != historyColorPreviewResources.m_History.m_NextColor;
				data.m_Texture = builder.Read(previewSource, RGTextureAccess::Sample);
				builder.SideEffect();
			});
		historyColorPreviewGraph.AddPass<TextureStorageAccessPassData>(
			"PostProcess.Preview.HistorySamples",
			[&](RenderGraph::RGBuilder& builder, TextureStorageAccessPassData& data)
			{
				const RGTextureId previewSource = ResolveTemporalAAPreviewSource(
					historyColorPreviewResources,
					PostProcessDebugTap::TemporalHistorySamples);
				historySamplesPreviewUsesTransientPayload =
					previewSource == historyColorPreviewResources.m_ReprojectionDiagnostics &&
					previewSource != historyColorPreviewResources.m_History.m_PreviousColor &&
					previewSource != historyColorPreviewResources.m_History.m_NextColor;
				data.m_Texture = builder.Read(previewSource, RGTextureAccess::Sample);
				builder.SideEffect();
			});
		context.Check(historyColorPreviewUsesTransientPayload &&
			historySamplesPreviewUsesTransientPayload &&
			UsesTemporalAAHistoryColorPreviewPayload(PostProcessDebugTap::TemporalHistoryColor) &&
			!UsesTemporalAAHistoryColorPreviewPayload(PostProcessDebugTap::TemporalHistorySamples) &&
			UsesTemporalAAHistorySamplesPreviewPayload(PostProcessDebugTap::TemporalHistorySamples) &&
			!UsesTemporalAAHistorySamplesPreviewPayload(PostProcessDebugTap::TemporalHistoryColor) &&
			UsesTemporalAAClipDistancePreviewPayload(PostProcessDebugTap::TemporalClipDistance) &&
			!UsesTemporalAAClipDistancePreviewPayload(PostProcessDebugTap::TemporalHistorySamples) &&
			IsTemporalAADiagnosticsTap(PostProcessDebugTap::TemporalClipDistance) &&
			UsesTemporalAAHistoryRelaxationPreviewPayload(PostProcessDebugTap::TemporalHistoryRelaxation) &&
			!UsesTemporalAAHistoryRelaxationPreviewPayload(PostProcessDebugTap::TemporalClipDistance) &&
			IsTemporalAADiagnosticsTap(PostProcessDebugTap::TemporalHistoryRelaxation) &&
			IsTemporalAADiagnosticsTap(PostProcessDebugTap::TemporalHistoryWeight) &&
			!IsTemporalAADiagnosticsTap(PostProcessDebugTap::TemporalMotionMagnitude) &&
			historyColorPreviewGraph.Compile(),
			"TAA history-color and history-age previews read selected transient payloads instead of exported previous or next history color");

		RecordingDevice device;
		device.m_TextureViewsSupported = true;
		const TemporalHistoryFormatSupport formatSupport = QueryTemporalHistoryFormatSupport(device);
		context.Check(formatSupport.IsSupported() &&
			device.m_TextureViewQueryCount == 6 &&
			device.m_LastTextureViewQueryTextureDesc.m_Format == TemporalHistoryReliabilityFormat &&
			Test(device.m_LastTextureViewQueryTextureDesc.m_Usage, RHITextureUsage::Sampled) &&
			Test(device.m_LastTextureViewQueryTextureDesc.m_Usage, RHITextureUsage::UnorderedAccess) &&
			device.m_LastTextureViewQueryDesc.m_Type == RHITextureViewType::UnorderedAccess,
			"Temporal history capability requires SRV and typed-UAV support for the color, depth and reliability formats");
		device.m_UseControlledFenceCompletion = true;
		PersistentTexturePool texturePool(&device);
		TemporalHistoryManager historyManager(&texturePool);
		const auto emptyHistorySummary = historyManager.GetSummary();
		context.Check(!emptyHistorySummary.m_HasActiveHistory && !emptyHistorySummary.m_HistoryValid &&
			emptyHistorySummary.m_DisplayViewId == RenderViewID::Unknown && device.m_CreateTextureCount == 0,
			"Lightweight history summary observes an empty manager without allocating GPU history");
		TemporalViewHistory viewHistory;
		TemporalObjectHistory objectHistory;
		const TemporalFramePlanResolveInfo activePlanInfo{
			.m_TemporalAA = {
				.m_Settings = { .m_Enabled = true },
				.m_Capabilities = {
					.m_MotionRenderTarget = true,
					.m_MotionShaderResource = true,
					.m_ResolvedColorRenderTarget = true,
					.m_ResolvedColorShaderResource = true,
					.m_ResolvedColorTypedUavStore = true,
					.m_HistoryColorShaderResource = true,
					.m_HistoryColorTypedUavStore = true,
					.m_HistoryDepthShaderResource = true,
					.m_HistoryDepthTypedUavStore = true,
					.m_HistoryReliabilityShaderResource = true,
					.m_HistoryReliabilityTypedUavStore = true,
				},
			},
			.m_DisplayViewId = RenderViewID::Main,
			.m_SceneExtensionParticipation = SceneExtensionTemporalParticipation::TemporalIntegrated,
			.m_ResetIdentity = 7,
			.m_SessionIdentity = 11,
			.m_DisplayViewEligible = true,
			.m_DepthVelocityPathAvailable = true,
		};
		const ResolvedTemporalFramePlan activePlan = ResolveTemporalFramePlan(activePlanInfo);

		auto prepareDisplayView = [&](TemporalFrameTransaction& transaction)
		{
			RenderView view{};
			view.m_ViewId = RenderViewID::Main;
			view.m_Width = 64;
			view.m_Height = 64;
			view.m_IsValid = true;
			view.m_ScenePreExposure = transaction.GetScenePreExposure();
			transaction.PrepareDisplayView(view);
			return view;
		};
		auto buildHistoryGraph = [&]
		(TemporalFrameTransaction& transaction, bool expectPrevious, bool inspectColdStartContract)
		{
			RenderGraph graph({
				.m_Device = &device,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			bool imported = false;
			bool exported = false;
			bool previousMatched = false;
			graph.AddPass<TemporalHistoryContractPassData>("TemporalHistory.Contract",
				[&](RenderGraph::RGBuilder& builder, TemporalHistoryContractPassData& data)
				{
					imported = transaction.ImportHistoryResources(builder, data.m_History);
					previousMatched = data.m_History.m_PreviousValid == expectPrevious;
					if (data.m_History.m_PreviousValid)
					{
						data.m_History.m_PreviousColor = builder.Read(data.m_History.m_PreviousColor, RGTextureAccess::Sample);
						data.m_History.m_PreviousDepth = builder.Read(data.m_History.m_PreviousDepth, RGTextureAccess::Sample);
						data.m_History.m_PreviousReliability = builder.Read(data.m_History.m_PreviousReliability, RGTextureAccess::Sample);
					}
					builder.WriteInPlace(data.m_History.m_NextColor, RGTextureAccess::StorageWrite);
					builder.WriteInPlace(data.m_History.m_NextDepth, RGTextureAccess::StorageWrite);
					builder.WriteInPlace(data.m_History.m_NextReliability, RGTextureAccess::StorageWrite);
					exported = transaction.ExportHistoryResources(builder, data.m_History);
				});
			const bool compiled = graph.Compile();
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(graph, snapshot);
			const bool previousExportsCommon = !expectPrevious ||
				std::ranges::count_if(snapshot.m_Resources,
					[](const RGSnapshotResourceInfo& resource) noexcept
					{
						return resource.m_Name.starts_with("TAA.History.Previous") &&
							resource.m_HasFinalBarrierState &&
							resource.m_FinalBarrierState == CommonRHIResourceState();
					}) == 3;
			if (!inspectColdStartContract)
			{
				return imported && exported && previousMatched && compiled &&
					previousExportsCommon;
			}

			const bool importedSix = snapshot.m_Resources.size() == 6 &&
				std::ranges::all_of(snapshot.m_Resources,
					[](const RGSnapshotResourceInfo& resource) noexcept
					{
						return resource.m_Imported;
					});
			const bool nextExportsCommon = std::ranges::count_if(snapshot.m_Resources,
				[](const RGSnapshotResourceInfo& resource) noexcept
				{
					return resource.m_Name.starts_with("TAA.History.Next") &&
						resource.m_InitialBarrierState == UndefinedRHITextureState() &&
						resource.m_HasFinalBarrierState &&
						resource.m_FinalBarrierState == CommonRHIResourceState();
				}) == 3;
			return imported && exported && previousMatched && compiled &&
				previousExportsCommon && importedSix && nextExportsCommon;
		};
		auto buildNoWriteHistoryGraph = [&](TemporalFrameTransaction& transaction)
		{
			RenderGraph graph({
				.m_Device = &device,
				.m_TransientResourcePool =
					reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }),
			});
			bool imported = false;
			bool exported = true;
			graph.AddPass<TemporalHistoryContractPassData>("TemporalHistory.NoWrite",
				[&](RenderGraph::RGBuilder& builder, TemporalHistoryContractPassData& data)
				{
					imported = transaction.ImportHistoryResources(builder, data.m_History);
					exported = transaction.ExportHistoryResources(builder, data.m_History);
				});
			return imported && !exported && !transaction.ParticipatedInResolve() &&
				graph.Compile();
		};

		TemporalFrameTransaction noWriteTransaction;
		noWriteTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		GGLAB_UNUSED(prepareDisplayView(noWriteTransaction));
		const bool noWriteGraphValid = buildNoWriteHistoryGraph(noWriteTransaction);
		noWriteTransaction.CommitCompleted(RHIFencePoint{ RHIFenceHandle{ 1, 1 }, 5 });
		const TemporalHistoryManagerDiagnostics afterNoWrite = historyManager.GetDiagnostics();
		context.Check(noWriteGraphValid &&
			noWriteTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
			!afterNoWrite.m_HistoryValid && afterNoWrite.m_ReadIndex == 0 &&
			!viewHistory.m_Valid && viewHistory.m_NextJitterIndex == 0,
			"Export without full current-frame history writes cannot participate or commit");

		TemporalFrameTransaction firstTransaction;
		firstTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager, 0.25f);
		const RenderView firstView = prepareDisplayView(firstTransaction);
		const bool coldStartGraphValid = buildHistoryGraph(firstTransaction, false, true);
		const RHIFencePoint firstFence{ RHIFenceHandle{ 1, 1 }, 10 };
		firstTransaction.CommitCompleted(firstFence);
		const TemporalHistoryManagerDiagnostics firstCommitted = historyManager.GetDiagnostics();
		const uint64_t firstGeneration = firstCommitted.m_AllocationGeneration;
		const auto committedHistorySummary = historyManager.GetSummary();
		context.Check(committedHistorySummary.m_HasActiveHistory && committedHistorySummary.m_HistoryValid &&
			committedHistorySummary.m_DisplayViewId == activePlan.m_DisplayViewId &&
			committedHistorySummary.m_SessionIdentity == activePlan.m_SessionIdentity && device.m_CreateTextureCount == 6,
			"Lightweight history summary identifies valid committed history without allocating or reading back");
		context.Check(coldStartGraphValid && !firstView.m_HasPreviousTemporalState &&
			firstTransaction.GetState() == TemporalFrameTransactionState::Committed &&
			firstCommitted.m_HasActiveHistory && firstCommitted.m_HistoryValid &&
			firstCommitted.m_ReadIndex == 1 && firstCommitted.m_ActiveBytes > 0 &&
			firstCommitted.m_LastCommitted.m_JitterIndex == 0 &&
			firstCommitted.m_Compatibility.m_ColorAbi == ActiveTemporalColorAbi &&
			firstCommitted.m_LastCommitted.m_PreExposure == 0.25f &&
			firstCommitted.m_LastCommitted.m_GraphicsFence == firstFence &&
			device.m_CreateTextureCount == 6,
			"Temporal history cold start imports six persistent textures and commits one write set");

		TemporalViewHistory incompatibleViewHistory = viewHistory;
		incompatibleViewHistory.Invalidate();
		TemporalObjectHistory incompatibleObjectHistory;
		TemporalFrameTransaction incompatibleViewTransaction;
		incompatibleViewTransaction.Begin(incompatibleViewHistory,
			incompatibleObjectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		const RenderView incompatibleView = prepareDisplayView(incompatibleViewTransaction);
		const bool managerHistoryStillValid = buildHistoryGraph(incompatibleViewTransaction, true, false);
		const bool finalHistoryCompatibility = incompatibleViewTransaction.HasCompatiblePreviousHistory();
		incompatibleViewTransaction.Abort();
		context.Check(managerHistoryStillValid && !finalHistoryCompatibility &&
			!incompatibleView.m_HasPreviousTemporalState,
			"Temporal resolve gates imported history with the transaction's final view-and-history compatibility");

		TemporalFrameTransaction abortedTransaction;
		abortedTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager, 8.0f);
		const RenderView abortedView = prepareDisplayView(abortedTransaction);
		const bool abortGraphValid = buildHistoryGraph(abortedTransaction, true, false);
		const RHIFencePoint vulkanAbortFence{ RHIFenceHandle{ 1, 1 }, 20 };
		abortedTransaction.Abort(vulkanAbortFence);
		const TemporalHistoryManagerDiagnostics afterAbort = historyManager.GetDiagnostics();
		context.Check(abortGraphValid && abortedView.m_HasPreviousTemporalState &&
			abortedView.m_PreviousScenePreExposure == 0.25f &&
			abortedTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
			afterAbort.m_HistoryValid && afterAbort.m_ReadIndex == 1 &&
			afterAbort.m_AllocationGeneration == firstGeneration &&
			afterAbort.m_LastCommitted.m_PreExposure == firstCommitted.m_LastCommitted.m_PreExposure &&
			afterAbort.m_LastCommitted.m_GraphicsFence == firstFence &&
			viewHistory.m_Valid && viewHistory.m_NextJitterIndex == 1,
			"Vulkan abort fences gate lifetime without advancing logical temporal history");

		TemporalFrameTransaction noResolveTransaction;
		noResolveTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		const RenderView noResolveView = prepareDisplayView(noResolveTransaction);
		const RHIFencePoint noResolveFence{ RHIFenceHandle{ 1, 1 }, 30 };
		noResolveTransaction.CommitCompleted(noResolveFence);
		const TemporalHistoryManagerDiagnostics afterNoResolve = historyManager.GetDiagnostics();
		context.Check(noResolveView.m_HasPreviousTemporalState &&
			noResolveTransaction.GetJitterIndex() == 1 &&
			noResolveTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
			afterNoResolve.m_ReadIndex == 1 &&
			afterNoResolve.m_LastCommitted.m_GraphicsFence == firstFence &&
			viewHistory.m_NextJitterIndex == 1,
			"A completed frame without temporal resolve advances no temporal participant");
		context.Check(afterAbort.m_ReadIndex == firstCommitted.m_ReadIndex &&
			afterNoResolve.m_ReadIndex == firstCommitted.m_ReadIndex,
			"History age remains on the committed color/depth ping-pong read index across abort and non-resolve paths");

		TemporalFrameTransaction invalidFenceTransaction;
		invalidFenceTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		GGLAB_UNUSED(prepareDisplayView(invalidFenceTransaction));
		const bool invalidFenceGraphValid = buildHistoryGraph(invalidFenceTransaction, true, false);
		invalidFenceTransaction.CommitCompleted();
		const TemporalHistoryManagerDiagnostics afterInvalidFence = historyManager.GetDiagnostics();
		context.Check(invalidFenceGraphValid &&
			invalidFenceTransaction.GetState() == TemporalFrameTransactionState::Aborted &&
			afterInvalidFence.m_HistoryValid && afterInvalidFence.m_ReadIndex == 1 &&
			afterInvalidFence.m_LastCommitted.m_GraphicsFence == firstFence &&
			viewHistory.m_NextJitterIndex == 1,
			"A completed transaction without a valid submitted fence cannot commit history");

		TemporalFrameTransaction fatalTransaction;
		fatalTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		GGLAB_UNUSED(prepareDisplayView(fatalTransaction));
		const bool fatalGraphValid = buildHistoryGraph(fatalTransaction, true, false);
		const RHIFencePoint fatalFence{ RHIFenceHandle{ 1, 1 }, 40 };
		fatalTransaction.InvalidateAfterFatal(fatalFence);
		const TemporalHistoryManagerDiagnostics afterFatal = historyManager.GetDiagnostics();
		const bool fatalFencesAreRetirementOnly =
			!afterFatal.m_PendingRetirementFences.empty() &&
			std::ranges::all_of(afterFatal.m_PendingRetirementFences,
				[&](const RHIFencePoint& fence) noexcept { return fence == fatalFence; });
		device.m_CompletedFenceValue = 39;
		texturePool.Tick();
		const uint32_t destroyedBeforeFatalFence = device.m_DestroyTextureCount;
		device.m_CompletedFenceValue = 40;
		texturePool.Tick();
		context.Check(fatalGraphValid &&
			fatalTransaction.GetState() == TemporalFrameTransactionState::Invalidated &&
			!viewHistory.m_Valid && !afterFatal.m_HasActiveHistory &&
			afterFatal.m_LastResetReason == TemporalHistoryResetReason::FatalSubmission &&
			afterFatal.m_PendingRetirementBytes > 0 && fatalFencesAreRetirementOnly &&
			destroyedBeforeFatalFence == 0 && device.m_DestroyTextureCount == 6,
			"Fatal-after-submit invalidates history and uses its fence only for retirement");

		TemporalFrameTransaction reenabledTransaction;
		reenabledTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		const TemporalHistoryManagerDiagnostics reenabled = historyManager.GetDiagnostics();
		reenabledTransaction.Abort();
		context.Check(
			reenabled.m_LastResetReason == TemporalHistoryResetReason::FatalSubmission,
			"Fresh history allocation preserves its explicit reset cause instead of ColdStart");
		TemporalFramePlanResolveInfo disabledPlanInfo = activePlanInfo;
		disabledPlanInfo.m_TemporalAA.m_Settings.m_Enabled = false;
		const ResolvedTemporalFramePlan disabledPlan = ResolveTemporalFramePlan(disabledPlanInfo);
		TemporalFrameTransaction disabledTransaction;
		disabledTransaction.Begin(viewHistory, objectHistory, disabledPlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		disabledTransaction.Abort();
		const TemporalHistoryManagerDiagnostics disabled = historyManager.GetDiagnostics();
		context.Check(reenabled.m_HasActiveHistory && !reenabled.m_HistoryValid &&
			reenabled.m_AllocationGeneration > firstGeneration &&
			device.m_CreateTextureCount == 12 && !disabled.m_HasActiveHistory &&
			disabled.m_LastResetReason == TemporalHistoryResetReason::Disabled &&
			device.m_DestroyTextureCount == 12,
			"Disabled history releases immediately and re-enable never revives retired allocations");

		TemporalFrameTransaction preResizeTransaction;
		preResizeTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &historyManager);
		GGLAB_UNUSED(prepareDisplayView(preResizeTransaction));
		const bool preResizeGraphValid = buildHistoryGraph(preResizeTransaction, false, false);
		const RHIFencePoint preResizeFence{ RHIFenceHandle{ 1, 1 }, 50 };
		preResizeTransaction.CommitCompleted(preResizeFence);
		const uint64_t preResizeGeneration = historyManager.GetDiagnostics().m_AllocationGeneration;
		TemporalFrameTransaction resizedTransaction;
		resizedTransaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 128, 72 }), &historyManager);
		const TemporalHistoryManagerDiagnostics resized = historyManager.GetDiagnostics();
		resizedTransaction.Abort();
		context.Check(preResizeGraphValid && resized.m_HasActiveHistory &&
			!resized.m_HistoryValid && resized.m_Compatibility.m_ColorExtent == ViewExtent{ 128, 72 } &&
			resized.m_Compatibility.m_DepthExtent == ViewExtent{ 128, 72 } &&
			resized.m_AllocationGeneration > preResizeGeneration &&
			resized.m_LastResetReason == TemporalHistoryResetReason::ExtentChanged &&
			resized.m_PendingRetirementFences.size() == 6 &&
			device.m_CreateTextureCount == 24,
			"Extent changes retire committed history and allocate a fresh invalid generation");

		TemporalFramePlanResolveInfo unavailablePlanInfo = activePlanInfo;
		unavailablePlanInfo.m_TemporalAA.m_Capabilities = {};
		const ResolvedTemporalFramePlan unavailablePlan = ResolveTemporalFramePlan(unavailablePlanInfo);
		TemporalFrameTransaction unavailableTransaction;
		unavailableTransaction.Begin(viewHistory, objectHistory, unavailablePlan, ResolveNativeViewResolution({ 128, 72 }), &historyManager);
		unavailableTransaction.Abort();
		const TemporalHistoryManagerDiagnostics unavailable = historyManager.GetDiagnostics();
		context.Check(!unavailable.m_HasActiveHistory &&
			unavailable.m_LastResetReason == TemporalHistoryResetReason::AvailabilityChanged,
			"Requested TAA availability loss retires history separately from an explicit toggle off");

		historyManager.Shutdown();
		device.m_CompletedFenceValue = 50;
		texturePool.Tick();
		context.Check(device.m_DestroyTextureCount == 24 &&
			texturePool.GetDiagnostics().m_ActiveTextureCount == 0 &&
			texturePool.GetDiagnostics().m_PendingRetirementTextureCount == 0,
			"Temporal history shutdown leaves no active or pending persistent allocation");

		TemporalHistoryManager exposureManager(&texturePool);
		viewHistory.Invalidate();
		float previousScale = 1.0f;
		uint64_t exposureFence = 100;
		bool sweepValid = true;
		bool expectPrevious = false;
		for (const float scale : { 0.5f, 4.0f, 0.00000001f, 1024.0f, 1.0f })
		{
			TemporalFrameTransaction transaction;
			transaction.Begin(viewHistory, objectHistory, activePlan, ResolveNativeViewResolution({ 64, 64 }), &exposureManager, scale);
			const auto view = prepareDisplayView(transaction);
			sweepValid &= view.m_HasPreviousTemporalState == expectPrevious &&
				view.m_PreviousScenePreExposure == (expectPrevious ? previousScale : scale) &&
				buildHistoryGraph(transaction, expectPrevious, false);
			transaction.CommitCompleted({ RHIFenceHandle{ 1, 1 }, exposureFence++ });
			sweepValid &= transaction.GetState() == TemporalFrameTransactionState::Committed &&
				exposureManager.GetDiagnostics().m_LastCommitted.m_PreExposure == scale;
			previousScale = scale;
			expectPrevious = true;
		}
		context.Check(sweepValid, "Exposure sweeps sample the prior committed scale and commit only the new submitted scale");
		exposureManager.Invalidate(TemporalHistoryResetReason::Disabled);
		auto writeManagerFrame = [&](TemporalHistoryFrameState& frame)
		{
			RenderGraph graph({ .m_Device = &device,
				.m_TransientResourcePool = reinterpret_cast<TransientResourcePool*>(uintptr_t{ 1 }) });
			bool written = false;
			graph.AddPass<TemporalHistoryContractPassData>("TemporalHistory.ExposureMetadata",
				[&](RenderGraph::RGBuilder& builder, TemporalHistoryContractPassData& data)
				{
					if (!exposureManager.ImportRenderGraphResources(frame, builder, data.m_History))
					{
						return;
					}
					builder.WriteInPlace(data.m_History.m_NextColor, RGTextureAccess::StorageWrite);
					builder.WriteInPlace(data.m_History.m_NextDepth, RGTextureAccess::StorageWrite);
					builder.WriteInPlace(data.m_History.m_NextReliability, RGTextureAccess::StorageWrite);
					written = exposureManager.ExportRenderGraphResources(frame, builder, data.m_History);
				});
			return graph.Compile() && written;
		};
		auto legacyFrame = exposureManager.BeginFrame(activePlan, ResolveNativeViewResolution({ 64, 64 }),
			TemporalColorAbi::LinearRec709SceneReferredV1);
		const bool legacyWritten = writeManagerFrame(legacyFrame);
		const bool legacyCommitted = exposureManager.CommitFrame(legacyFrame, {
			.m_Compatibility = exposureManager.GetDiagnostics().m_Compatibility,
			.m_PreExposure = 1.0f }, { RHIFenceHandle{ 1, 1 }, exposureFence++ });
		auto migratedFrame = exposureManager.BeginFrame(activePlan, ResolveNativeViewResolution({ 64, 64 }));
		context.Check(legacyWritten && legacyCommitted && !migratedFrame.m_PreviousValid &&
			exposureManager.GetDiagnostics().m_LastResetReason == TemporalHistoryResetReason::ColorAbiChanged,
			"V1 history is retired rather than sampled after migration to active V2");
		exposureManager.AbortFrame(migratedFrame, {});
		bool invalidScalesRejected = true;
		for (const float invalidScale : { 0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() })
		{
			auto frame = exposureManager.BeginFrame(activePlan, ResolveNativeViewResolution({ 64, 64 }));
			invalidScalesRejected &= writeManagerFrame(frame);
			invalidScalesRejected &= !exposureManager.CommitFrame(frame, {
				.m_Compatibility = exposureManager.GetDiagnostics().m_Compatibility,
				.m_PreExposure = invalidScale }, { RHIFenceHandle{ 1, 1 }, exposureFence++ });
			const auto diagnostics = exposureManager.GetDiagnostics();
			invalidScalesRejected &= !diagnostics.m_HistoryValid && !diagnostics.m_HasActiveHistory &&
				diagnostics.m_LastResetReason == TemporalHistoryResetReason::InvalidExposureMetadata;
		}
		context.Check(invalidScalesRejected, "Zero, negative, infinite and NaN scales cannot publish history and retire submitted resources");
		// Color history stores display pixels and depth history render-domain samples, so
		// either extent changing invalidates the pair.
		TemporalHistoryManager domainManager(&texturePool);
		const ViewResolution upscaled{ .m_Render = { 64, 40 }, .m_Display = { 96, 60 } };
		auto upscaledFrame = domainManager.BeginFrame(activePlan, upscaled);
		const TemporalHistoryManagerDiagnostics upscaledHistory = domainManager.GetDiagnostics();
		domainManager.AbortFrame(upscaledFrame, {});
		ViewResolution renderChanged = upscaled;
		renderChanged.m_Render = { 48, 30 };
		auto renderChangedFrame = domainManager.BeginFrame(activePlan, renderChanged);
		const TemporalHistoryManagerDiagnostics renderChangedHistory = domainManager.GetDiagnostics();
		domainManager.AbortFrame(renderChangedFrame, {});
		context.Check(upscaledHistory.m_HasActiveHistory &&
			upscaledHistory.m_Compatibility.m_ColorExtent == ViewExtent{ 96, 60 } &&
			upscaledHistory.m_Compatibility.m_DepthExtent == ViewExtent{ 64, 40 } &&
			renderChangedHistory.m_LastResetReason == TemporalHistoryResetReason::ExtentChanged &&
			renderChangedHistory.m_Compatibility.m_ColorExtent == ViewExtent{ 96, 60 } &&
			renderChangedHistory.m_Compatibility.m_DepthExtent == ViewExtent{ 48, 30 },
			"Temporal color history follows the display extent and depth history the render "
			"extent; a render extent change alone resets the pair");
		// The stored alpha of one accumulation model is not the other's.
		ResolvedTemporalFramePlan agePlan = activePlan;
		agePlan.m_TemporalAA.m_HistoryAccumulation = TemporalAAHistoryAccumulation::CompatibilityAge;
		auto ageFrame = domainManager.BeginFrame(agePlan, renderChanged);
		const TemporalHistoryManagerDiagnostics ageHistory = domainManager.GetDiagnostics();
		domainManager.AbortFrame(ageFrame, {});
		context.Check(renderChangedHistory.m_Compatibility.m_Accumulation == TemporalAAHistoryAccumulation::EffectiveSamples &&
			!ageFrame.m_PreviousValid &&
			ageHistory.m_LastResetReason == TemporalHistoryResetReason::AccumulationChanged &&
			ageHistory.m_Compatibility.m_Accumulation == TemporalAAHistoryAccumulation::CompatibilityAge,
			"Switching the history accumulation model resets the color and depth history");
		domainManager.Shutdown();
		exposureManager.Shutdown();
		device.m_CompletedFenceValue = exposureFence;
		texturePool.Tick();
		context.Check(texturePool.GetDiagnostics().m_ActiveTextureCount == 0 &&
			texturePool.GetDiagnostics().m_PendingRetirementTextureCount == 0,
			"Exposure migration and invalid-metadata paths retain no allocations after their fences complete");
	}
}

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingTemporalHistorySelfTests(SelfTestContext& context) noexcept
	{
		RunTemporalReferenceContractTests(context);
		RunGTAOTemporalHistoryContractTests(context);
		RunTemporalHistoryTransactionContractTests(context);
		RunTemporalCaptureContinuityContractTests(context);
	}
}
