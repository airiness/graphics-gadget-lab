#pragma once

#include "GGLabTestCore/SelfTest.h"

namespace gglab::rendering_test
{
	void RunEnvironmentLightingOverrideTests(SelfTestContext& context) noexcept;
	void RunEnvironmentLightingSettingsTests(SelfTestContext& context) noexcept;
	void RunLightingProfileContractTests(SelfTestContext& context) noexcept;
	void RunIBLPreviewContractTests(SelfTestContext& context) noexcept;
	void RunShadowPreviewContractTests(SelfTestContext& context) noexcept;
	void RunPostProcessPreviewContractTests(SelfTestContext& context) noexcept;
	void RunOpaqueSceneExtensionContractTests(SelfTestContext& context) noexcept;
	void RunOverlayExtensionContractTests(SelfTestContext& context) noexcept;
	void RunRuntimePathConfigurationContractTests(SelfTestContext& context) noexcept;
	void RunNapaVoxelRenderGraphContractTests(SelfTestContext& context) noexcept;
	void RunPassParameterContractTests(SelfTestContext& context) noexcept;
	void RunSubmissionRetentionContractTests(SelfTestContext& context) noexcept;
	void RunUploadStorageContractTests(SelfTestContext& context) noexcept;
	void RunUploadBudgetTests(SelfTestContext& context) noexcept;
	void RunUploadTeardownContractTests(SelfTestContext& context) noexcept;
	void RunUploadOwnerThreadContractTests(SelfTestContext& context) noexcept;
	void RunUploadWriterContractTests(SelfTestContext& context) noexcept;
	void RunDX12TransferCompletionTests(SelfTestContext& context) noexcept;
	void RunCheckedCompletionContractTests(SelfTestContext& context) noexcept;
	void RunDependentFrameSubmissionTests(SelfTestContext& context) noexcept;
	void RunAccelerationStructureGraphTests(SelfTestContext& context) noexcept;
	void RunPassProfileIdentityContractTests(SelfTestContext& context) noexcept;
	void RunDeferredCommitContractTests(SelfTestContext& context) noexcept;
	void RunPreviewSubmissionContractTests(SelfTestContext& context) noexcept;
	void RunPreviewPassContractTests(SelfTestContext& context) noexcept;
	void RunRenderFrameOutcomeContractTests(SelfTestContext& context) noexcept;
	void RunFrameSubmissionContractTests(SelfTestContext& context) noexcept;
	void RunFrameConstantUploadTests(SelfTestContext& context) noexcept;
	void RunRHIFrameAndGraphicsScopeContractTests(SelfTestContext& context) noexcept;
	void RunRenderHostContractTests(SelfTestContext& context) noexcept;
	void RunDX12GraphicsContractLoweringTests(SelfTestContext& context) noexcept;
	void RunProjectionConventionTests(SelfTestContext& context) noexcept;
	void RunPositionReconstructionTests(SelfTestContext& context) noexcept;
	void RunScreenCoordinateTests(SelfTestContext& context) noexcept;
	void RunRenderViewConventionTests(SelfTestContext& context) noexcept;
	void RunDirectionalShadowCascadeFrameTests(SelfTestContext& context) noexcept;
	void RunDirectionalShadowGraphTests(SelfTestContext& context) noexcept;
	void RunDirectionalShadowSplitTests(SelfTestContext& context) noexcept;
	void RunDirectionalShadowPlanningTests(SelfTestContext& context) noexcept;
	void RunPartialTextureProducerTests(SelfTestContext& context) noexcept;
	void RunDirectionalShadowStabilityTests(SelfTestContext& context) noexcept;
	void RunDirectionalShadowBiasTests(SelfTestContext& context) noexcept;
	void RunSampleableDepthFormatTests(SelfTestContext& context) noexcept;
	void RunMaterialDiagnosticPrewarmTests(SelfTestContext& context) noexcept;
	void RunForwardPlusReadbackFenceTests(SelfTestContext& context) noexcept;
	void RunForwardPlusContractTests(SelfTestContext& context) noexcept;
	void RunDepthCoverageContractTests(SelfTestContext& context) noexcept;
	void RunScreenSpaceAndDepthContractTests(SelfTestContext& context) noexcept;
	void RunTextureFormatCapabilityTests(SelfTestContext& context) noexcept;
	void RunGTAORenderGraphDataflowTests(SelfTestContext& context) noexcept;
	void RunCompiledResourceStatusTests(SelfTestContext& context) noexcept;
	void RunRenderGraphAccessAndBarrierContractTests(SelfTestContext& context) noexcept;
	void RunPersistentTexturePoolContractTests(SelfTestContext& context) noexcept;
	void RunTemporalReferenceContractTests(SelfTestContext& context) noexcept;
	void RunGTAOTemporalHistoryContractTests(SelfTestContext& context) noexcept;
	void RunTemporalCaptureContinuityContractTests(SelfTestContext& context) noexcept;
	void RunTemporalHistoryTransactionContractTests(SelfTestContext& context) noexcept;
	void RunResourceStateAndPortabilityContractTests(SelfTestContext& context) noexcept;
	void RunAtmosphereContractTests(SelfTestContext& context) noexcept;
	void RunWorldSunContractTests(SelfTestContext& context) noexcept;
	void RunViewSettingsPatchTests(SelfTestContext& context) noexcept;
	void RunExposureContractTests(SelfTestContext& context) noexcept;
	void RunTemporalCompatibilityAndHistoryContractTests(SelfTestContext& context) noexcept;
	void RunMaterialDiagnosticDemandTests(SelfTestContext& context) noexcept;
	void RunMaterialFrameCacheContractTests(SelfTestContext& context) noexcept;
	void RunSceneTableUploadContractTests(SelfTestContext& context) noexcept;
	void RunMaterialBaselineContractTests(SelfTestContext& context) noexcept;
	void RunGGXEnergyCompensationContractTests(SelfTestContext& context) noexcept;
	void RunScopedConstantRecordingTests(SelfTestContext& context) noexcept;
	void RunGraphObjectLifetimeTests(SelfTestContext& context) noexcept;
	void RunNativeContextCompositionTests(SelfTestContext& context) noexcept;
}
