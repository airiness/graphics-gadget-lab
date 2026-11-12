#include "GGLabTestCore/SelfTest.h"
#include "RGObjectArenaSelfTests.h"
#include "RenderServicesViewSelfTests.h"
#include "RenderingContractSelfTests.h"
#include "RenderingContractTestCases.h"

namespace gglab
{
	using namespace rendering_test;

	void RunRenderingContractSelfTests(SelfTestContext& context) noexcept
	{
		RunRenderServicesViewSelfTests(context);
		RunRGObjectArenaSelfTests(context);
		RunGraphObjectLifetimeTests(context);
		RunMaterialDiagnosticPrewarmTests(context);
		RunMaterialBaselineContractTests(context);
		RunMaterialFrameCacheContractTests(context);
		RunSceneTableUploadContractTests(context);
		RunMaterialDiagnosticDemandTests(context);
		RunGGXEnergyCompensationContractTests(context);
		RunNativeContextCompositionTests(context);
		RunIBLPreviewContractTests(context);
		RunPostProcessPreviewContractTests(context);
		RunShadowPreviewContractTests(context);
		RunEnvironmentLightingSettingsTests(context);
		RunEnvironmentLightingOverrideTests(context);
		RunLightingProfileContractTests(context);
		RunOpaqueSceneExtensionContractTests(context);
		RunOverlayExtensionContractTests(context);
		RunRuntimePathConfigurationContractTests(context);
		RunRenderHostContractTests(context);
		RunNapaVoxelRenderGraphContractTests(context);
		RunPassParameterContractTests(context);
		RunUploadWriterContractTests(context);
		RunUploadStorageContractTests(context);
		RunSubmissionRetentionContractTests(context);
		RunUploadBudgetTests(context);
		RunUploadTeardownContractTests(context);
		RunUploadOwnerThreadContractTests(context);
		RunCheckedCompletionContractTests(context);
		RunDX12TransferCompletionTests(context);
		RunFrameSubmissionContractTests(context);
		RunRenderFrameOutcomeContractTests(context);
		RunDeferredCommitContractTests(context);
		RunPassProfileIdentityContractTests(context);
		RunAccelerationStructureGraphTests(context);
		RunPreviewSubmissionContractTests(context);
		RunPreviewPassContractTests(context);
		RunFrameConstantUploadTests(context);
		RunScopedConstantRecordingTests(context);
		RunRHIFrameAndGraphicsScopeContractTests(context);
		RunDX12GraphicsContractLoweringTests(context);
		RunScreenSpaceAndDepthContractTests(context);
		RunDirectionalShadowCascadeFrameTests(context);
		RunDirectionalShadowSplitTests(context);
		RunDirectionalShadowPlanningTests(context);
		RunPartialTextureProducerTests(context);
		RunDirectionalShadowStabilityTests(context);
		RunDirectionalShadowBiasTests(context);
		RunDirectionalShadowGraphTests(context);
		RunTextureFormatCapabilityTests(context);
		RunPersistentTexturePoolContractTests(context);
		RunTemporalReferenceContractTests(context);
		RunGTAOTemporalHistoryContractTests(context);
		RunTemporalHistoryTransactionContractTests(context);
		RunTemporalCaptureContinuityContractTests(context);
		RunAtmosphereContractTests(context);
		RunWorldSunContractTests(context);
		RunViewSettingsPatchTests(context);
		RunExposureContractTests(context);
		RunResourceStateAndPortabilityContractTests(context);
		RunGTAORenderGraphDataflowTests(context);
		RunCompiledResourceStatusTests(context);
		RunRenderGraphAccessAndBarrierContractTests(context);
		RunTemporalCompatibilityAndHistoryContractTests(context);
	}
}
