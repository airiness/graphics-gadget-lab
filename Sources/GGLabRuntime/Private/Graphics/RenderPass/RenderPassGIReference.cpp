#include "Graphics/RenderPass/RenderPassGIReference.h"
#include "Graphics/Pipeline/GIReferenceObservation.h"
#include "GGLabRuntime/Graphics/Buffer/PersistentStructuredBuffer.h"
#include "GGLabRuntime/Graphics/Pipeline/PipelineTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneBindingLayouts.h"
#include "GGLabRuntime/Graphics/Pipeline/SceneConstantBindings.h"
#include "GGLabRuntime/Graphics/Pipeline/ScopedConstantBindings.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderPass/IBLGraphResources.h"
#include "GGLabRuntime/Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/RenderPass/AtmosphereGraphResources.h"
#include "Graphics/RenderPass/GIReceiverGraphResources.h"
#include "ShaderArtifactRuntime/GGLabShaderPrograms.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace gglab
{
	namespace
	{
		const RenderPassInfo PassInfo{ .m_TypeName = "GI.Reference.PathTrace",
			.m_DisplayName = "Fixed-depth irradiance reference", .m_CategoryName = "Global Illumination",
			.m_Category = RenderPassCategory::Debug, .m_Type = RenderPassType::Compute };
		constexpr std::array OutputNames{ "GI.Reference.Irradiance", "GI.Reference.Diagnostics",
			"GI.Reference.Position", "GI.Reference.Identity" };
		struct Parameters
		{
			uint32_t m_Depth = 0;
			uint32_t m_Face = 0;
			uint32_t m_Shading = 0;
			std::array<uint32_t, 4> m_Outputs{};
			uint32_t m_View = 0;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			uint32_t m_InstanceCount = 0;
			uint32_t m_GeometryCount = 0;
			uint32_t m_MaterialCount = 0;
			uint32_t m_SampleCount = 0;
			uint32_t m_Seed = 0;
			uint32_t m_ConstantEnvironment = 0;
			std::array<float, 3> m_ConstantRadiance{};
			float m_MaximumDistance = 0;
			uint32_t m_Transmittance = UINT32_MAX;
			uint32_t m_AtmosphereSampler = 0;
			uint32_t m_ReceiverAlbedo = 0;
			uint32_t m_LambertRadiance = 0;
			uint32_t m_SunIrradiance = 0;
			uint32_t m_EnvironmentIrradiance = 0;
			uint32_t m_EnvironmentFilter = 0;
			uint32_t m_RowOffset = 0;
			std::array<uint32_t, 4> m_Previous{};
			std::array<uint32_t, 4> m_Next{};
			uint32_t m_SampleBegin = 0;
			uint32_t m_ObservationEnabled = 0;
			uint32_t m_MaximumDepth = 1;
			uint32_t m_PathDiagnostics = 0;
			std::array<uint32_t, 2> m_PreviousCompensation{};
			std::array<uint32_t, 2> m_NextCompensation{};
		};
		static_assert(sizeof(Parameters) == 176 && offsetof(Parameters, m_ConstantRadiance) == 64 &&
			offsetof(Parameters, m_Transmittance) == 80 &&
			offsetof(Parameters, m_SunIrradiance) == 96 && offsetof(Parameters, m_RowOffset) == 108 &&
			offsetof(Parameters, m_Previous) == 112 && offsetof(Parameters, m_SampleBegin) == 144 &&
			offsetof(Parameters, m_MaximumDepth) == 152 && offsetof(Parameters, m_PathDiagnostics) == 156 &&
			offsetof(Parameters, m_PreviousCompensation) == 160 && offsetof(Parameters, m_NextCompensation) == 168);
	}

	RenderFrameValidationResult ValidateGIReferenceInputs(
		const GIReferenceCreateInfo& info, const RaySceneDiagnostics& scene) noexcept
	{
		if ((info.m_EnvironmentFilter != GIReferenceEnvironmentFilter::Face32 &&
			info.m_EnvironmentFilter != GIReferenceEnvironmentFilter::MipZero) ||
			info.m_SampleCount == 0 || info.m_SampleCount > 1'048'576 ||
			info.m_SamplesPerBatch == 0 || info.m_SamplesPerBatch > 4096 ||
			info.m_SampleWorkPerSubmission == 0 || info.m_SampleWorkPerSubmission > 8'388'608 ||
			info.m_MaximumDepth == 0 || info.m_MaximumDepth > 256 ||
			info.m_QueryWorkPerSubmission == 0 || info.m_QueryWorkPerSubmission > 16'777'216 ||
			!std::isfinite(info.m_MaximumDistance) || info.m_MaximumDistance <= 0.0f ||
			!std::ranges::all_of(info.m_ConstantRadiance,
				[](float value) { return std::isfinite(value) && value >= 0.0f; }))
		{
			return RenderFrameValidationResult::ContractFailure("Invalid GI reference sampling inputs");
		}
		if (scene.m_Status != RaySceneStatus::Prepared || !scene.m_HasCompleteBounds ||
			!std::isfinite(scene.m_BoundsDiagonal) || scene.m_BoundsDiagonal <= 0.0f ||
			info.m_MaximumDistance < scene.m_BoundsDiagonal)
		{
			return RenderFrameValidationResult::ContractFailure("IncompleteScene",
				"Environment misses require a complete all-World snapshot and t_max >= its bounds diagonal.");
		}
		return RenderFrameValidationResult::Ready();
	}

	struct RenderPassGIReference::State
	{
		ResolvedRenderLayout m_Layout;
		ResolvedBindingRef<SceneGPU> m_Scene;
		ResolvedBindingRef<AtmosphereGPU> m_Atmosphere;
		ComputePipelineRecipe m_Recipe;
		ComputePipelineSlot m_Slot;
		ResolvedRenderPipeline m_Pipeline;
		std::array<uint32_t, 6> m_Parameters{};
	};

	RenderPassGIReference::RenderPassGIReference() noexcept : m_State(std::make_unique<State>()) {}
	RenderPassGIReference::~RenderPassGIReference() = default;

	RenderFrameValidationResult RenderPassGIReference::Prepare(const RenderFrameContext& frame,
		const RenderServices& services, const GIReferenceCreateInfo& info, ShaderID coverageVertex) noexcept
	{
		if (!info.m_RayScene)
		{
			return RenderFrameValidationResult::ContractFailure("GI reference requires an owned ray scene");
		}
		const auto validation = ValidateGIReferenceInputs(info, info.m_RayScene->GetDiagnostics());
		if (!validation.IsReady())
		{
			return validation;
		}
		if (!services.m_GIReferenceObservation)
		{
			return RenderFrameValidationResult::ContractFailure("GI reference observation service is unavailable");
		}
		auto& state = *m_State;
		if (!state.m_Layout.IsValid())
		{
			const RHITextureDesc desc{ .m_Format = RHIFormat::R32G32B32A32Float,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess | RHITextureUsage::CopySource };
			if (!services.m_Presentation->GetDevice()->QueryTextureSupport(desc).IsSupported())
			{
				return RenderFrameValidationResult::ContractFailure("GI reference requires RGBA32F storage textures");
			}
			const RHITextureDesc randomDesc{ .m_Format = RHIFormat::R32Uint,
				.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess };
			if (!services.m_Presentation->GetDevice()->QueryTextureSupport(randomDesc).IsSupported())
			{
				return RenderFrameValidationResult::ContractFailure("GI reference requires R32Uint RNG storage");
			}
			const auto* common = services.m_BindingLayout->GetCommonLayoutContract();
			const auto atmosphere = common ? scene_bindings::ComposeAtmosphereLayout(*common) : LayoutCompositionResult{};
			if (!atmosphere.IsSuccess())
			{
				return RenderFrameValidationResult::ContractFailure("GI reference common layout is unavailable");
			}
			std::vector<LayoutBindingRequirement> bindings(
				atmosphere.m_Layout->GetBindings().begin(), atmosphere.m_Layout->GetBindings().end());
			for (uint32_t index = 0; index < state.m_Parameters.size(); ++index)
			{
				bindings.push_back({ .m_Key = LayoutBindingKey{ 0x700u + index },
					.m_Slot = { .m_Type = index == 0 ? RHIBindingType::AccelerationStructure : RHIBindingType::ReadOnlyStorageBuffer,
						.m_Visibility = RHIShaderStage::Compute, .m_Binding = 5 + index, .m_Count = 1 },
					.m_ParameterOrder = 14 + index });
			}
			const std::array fragments{ LayoutFragment{ "GIReference", bindings } };
			const auto composed = ComposeLayoutFragments(fragments);
			if (!composed.IsSuccess())
			{
				return RenderFrameValidationResult::ContractFailure("GI reference layout composition failed");
			}
			state.m_Layout = ResolvedRenderLayout::Create(services.m_Presentation->GetRHIContext()->GetPipelineSystem(),
				*composed.m_Layout, "GI.Reference");
			for (uint32_t index = 0; index < state.m_Parameters.size(); ++index)
			{
				const auto parameter = composed.m_Layout->FindParameterIndex(LayoutBindingKey{ 0x700u + index });
				if (!parameter)
				{
					return RenderFrameValidationResult::ContractFailure("GI reference ray binding is unavailable");
				}
				state.m_Parameters[index] = *parameter;
			}
			state.m_Scene = ResolvedBindingRef<SceneGPU>::Resolve(state.m_Layout, scene_bindings::SceneConstants);
			state.m_Atmosphere = ResolvedBindingRef<AtmosphereGPU>::Resolve(state.m_Layout, scene_bindings::AtmosphereConstants);
			state.m_Recipe.m_BindingLayout = state.m_Layout.GetNativeLayout();
			state.m_Recipe.m_CSId = services.m_ShaderPrograms->LoadProgram(shader_programs::GIReferenceCompute);
		}
		state.m_Pipeline = state.m_Layout.Resolve(*services.m_PipelineResolver, state.m_Slot, state.m_Recipe, PassInfo);
		const auto& view = frame.GetDisplayRenderView();
		const auto& environment = services.m_Environment->GetEnvironmentLightingSettings();
		GIReferenceObservationKey key{ .m_Scene = info.m_RayScene,
			.m_SceneRevision = info.m_RayScene->GetDiagnostics().m_Revision,
			.m_LightingGeneration = frame.m_RenderScene.m_WorldLightingGeneration,
			.m_EnvironmentGeneration = services.m_Environment->GetBakeRequestGeneration(),
			.m_ShaderGeneration = services.m_ShaderPrograms->GetGeneration(state.m_Recipe.m_CSId),
			.m_ReceiverShaderGeneration =
				services.m_ShaderPrograms->GetGeneration(services.m_ShaderPrograms->LoadProgram(shader_programs::GIReceiverPixel)),
			.m_CoverageShaderGeneration = services.m_ShaderPrograms->GetGeneration(coverageVertex),
			.m_CameraPosition = { view.m_CameraPosition.m_X, view.m_CameraPosition.m_Y, view.m_CameraPosition.m_Z },
			.m_ConstantRadiance = info.m_ConstantRadiance, .m_MaximumDistance = info.m_MaximumDistance,
			.m_EnvironmentIntensity = environment.m_Intensity, .m_EnvironmentRotation = environment.m_RotationRadians,
			.m_Width = view.m_Width, .m_Height = view.m_Height, .m_Samples = info.m_SampleCount,
			.m_Seed = info.m_Seed, .m_MaximumDepth = info.m_MaximumDepth,
			.m_Filter = info.m_EnvironmentFilter,
			.m_ConstantEnvironment = info.m_ConstantEnvironment };
		static_assert(sizeof(key.m_ViewProjection) == sizeof(view.m_UnjitteredViewProj));
		std::memcpy(key.m_ViewProjection.data(), &view.m_UnjitteredViewProj, sizeof(key.m_ViewProjection));
		if (!services.m_GIReferenceObservation->Prepare(key,
			info.m_SamplesPerBatch, info.m_SampleWorkPerSubmission, info.m_QueryWorkPerSubmission))
		{
			return RenderFrameValidationResult::ContractFailure("GI reference observation preparation failed");
		}
		return state.m_Pipeline.IsValid() && state.m_Scene.IsValid() && state.m_Atmosphere.IsValid() ?
			RenderFrameValidationResult::Ready() :
			RenderFrameValidationResult::ContractFailure("GI reference compute pipeline is unavailable");
	}

	void RenderPassGIReference::AddPass(RenderGraph& graph, const RenderFrameContext& frame,
		const RenderServices& services, const RaySceneGraphResources& scene,
		const GIReferenceCreateInfo& info) noexcept
	{
		if (!info.m_ConstantEnvironment)
		{
			const auto& bake = services.m_Environment->GetBakingStatus();
			if (!bake.m_HasActiveBake || bake.m_ActiveGeneration !=
				services.m_Environment->GetBakeRequestGeneration())
			{
				// Keep recording the frame's bake work. Skipping the whole frame here
				// would prevent publication; after-ready observations wait on IBL.
				return;
			}
		}
		struct PassData
		{
			RGTextureViewId m_Depth;
			RGTextureViewId m_Face;
			RGTextureViewId m_Shading;
			RGTextureViewId m_Transmittance;
			RGTextureViewId m_ReceiverAlbedo;
			RGTextureViewId m_LambertRadiance;
			RGTextureViewId m_SunIrradiance;
			RGTextureViewId m_EnvironmentIrradiance;
			RGTextureViewId m_PathDiagnostics;
			std::array<RGTextureViewId, OutputNames.size()> m_Outputs{};
			TypedConstantSnapshot<AtmosphereGPU> m_Atmosphere;
			Parameters m_Parameters;
			std::array<RGTextureViewId, GIReferenceObservation::SurfaceCount> m_Previous{};
			std::array<RGTextureViewId, GIReferenceObservation::SurfaceCount> m_Next{};
		};
		graph.AddPass<PassData>(PassInfo.m_TypeName.c_str(), RGPassEncoderType::Compute,
			[&frame, &services, scene, info](RenderGraph::RGBuilder& builder, PassData& data)
			{
				const auto& receivers = builder.GetBlackboard().Get<RGGIReceiverResources>(GIReceiverResourcesName);
				const auto readTexture = [&builder](RGTextureId texture)
					{
						const auto read = builder.Read(texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
						return builder.CreateView<RHITextureViewType::ShaderResource>(read);
					};
				data.m_Face = readTexture(receivers.m_Textures[0]);
				data.m_Shading = readTexture(receivers.m_Textures[2]);
				data.m_ReceiverAlbedo = readTexture(receivers.m_Textures[7]);
				const auto& depth = builder.GetBlackboard().Get<RGSceneDepthResources>(SceneDepthResourcesName);
				const auto extent = builder.GetTextureDesc(depth.m_Texture).m_Extent;
				const auto read = builder.Read(depth.m_Texture, RGTextureAccess::Sample, RHIStage::ComputeShader);
				data.m_Depth = builder.CreateView<RHITextureViewType::ShaderResource>(read, depth.m_SrvDesc);
				if (!info.m_ConstantEnvironment)
				{
					const auto& ibl = builder.GetBlackboard().Get<RGIBLResources>(IBLResourcesName);
					GGLAB_UNUSED(builder.Read(ibl.m_EnvironmentCubemap, RGTextureAccess::Sample, RHIStage::ComputeShader));
				}
				data.m_Atmosphere = builder.GetBlackboard().Get<RGForwardAtmosphereConstants>().m_Snapshot;
				if (const auto* atmosphere = builder.GetBlackboard().TryGet<RGAtmosphereResources>(AtmosphereResourcesName);
					atmosphere && frame.m_RenderScene.m_WorldSun)
				{
					data.m_Transmittance = readTexture(atmosphere->m_Luts[0]);
				}
				for (size_t index = 0; index < data.m_Outputs.size(); ++index)
				{
					auto output = builder.CreateTexture(OutputNames[index], { .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent });
					builder.WriteInPlace(output, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
					data.m_Outputs[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(output);
				}
				auto radiance = builder.CreateTexture("GI.Reference.LambertRadiance",
					{ .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent });
				builder.WriteInPlace(radiance, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_LambertRadiance = builder.CreateView<RHITextureViewType::UnorderedAccess>(radiance);
				auto sun = builder.CreateTexture("GI.Reference.SunIrradiance",
					{ .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent });
				builder.WriteInPlace(sun, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_SunIrradiance = builder.CreateView<RHITextureViewType::UnorderedAccess>(sun);
				auto environment = builder.CreateTexture("GI.Reference.EnvironmentIrradiance",
					{ .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent });
				builder.WriteInPlace(environment, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_EnvironmentIrradiance = builder.CreateView<RHITextureViewType::UnorderedAccess>(environment);
				auto pathDiagnostics = builder.CreateTexture("GI.Reference.PathDiagnostics",
					{ .m_Format = RHIFormat::R32G32B32A32Float, .m_Extent = extent });
				builder.WriteInPlace(pathDiagnostics, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
				data.m_PathDiagnostics = builder.CreateView<RHITextureViewType::UnorderedAccess>(pathDiagnostics);
				GGLAB_UNUSED(builder.Read(scene.m_TopLevel, RGAccelerationStructureAccess::QueryRead));
				for (const auto blas : scene.m_BottomLevels)
				{
					GGLAB_UNUSED(builder.Read(blas, RGAccelerationStructureAccess::QueryRead));
				}
				for (const auto buffer : { scene.m_Vertices, scene.m_Indices, scene.m_Geometries, scene.m_Instances, scene.m_Materials })
				{
					GGLAB_UNUSED(builder.Read(buffer, RGBufferAccess::StructuredRead, RHIStage::ComputeShader));
				}
				data.m_Parameters = { .m_View = frame.GetRenderQueue(frame.GetDisplayViewId()).m_CoverageRasterDomain.m_ViewBindingId,
					.m_Width = extent.m_Width, .m_Height = extent.m_Height, .m_InstanceCount = scene.m_InstanceCount,
					.m_GeometryCount = scene.m_GeometryCount, .m_MaterialCount = scene.m_MaterialCount,
					.m_SampleCount = info.m_SampleCount, .m_Seed = info.m_Seed,
					.m_ConstantEnvironment = info.m_ConstantEnvironment ? 1u : 0u,
					.m_ConstantRadiance = info.m_ConstantRadiance, .m_MaximumDistance = info.m_MaximumDistance,
					.m_AtmosphereSampler = services.m_Samplers->GetSamplerIndex(SamplerPreset::LinearClamp) };
				data.m_Parameters.m_EnvironmentFilter = static_cast<uint32_t>(info.m_EnvironmentFilter);
				data.m_Parameters.m_MaximumDepth = info.m_MaximumDepth;
				if (auto* observation = services.m_GIReferenceObservation)
				{
					const auto progress = observation->GetDiagnostics();
					data.m_Parameters.m_ObservationEnabled = 1;
					data.m_Parameters.m_SampleBegin = progress.m_SubmittedSamples;
					data.m_Parameters.m_SampleCount = progress.m_BatchSamples;
					const auto surfaces = observation->Import(builder);
					for (size_t index = 0; index < surfaces.size(); ++index)
					{
						if (progress.m_SubmittedSamples != 0)
						{
							data.m_Previous[index] = readTexture(surfaces[index].m_Previous);
						}
						if (progress.m_BatchSamples != 0)
						{
							auto next = surfaces[index].m_Next;
							builder.WriteInPlace(next, RGTextureAccess::StorageWrite, RHIStage::ComputeShader);
							data.m_Next[index] = builder.CreateView<RHITextureViewType::UnorderedAccess>(next);
						}
					}
					observation->Export(builder, surfaces);
				}
				builder.SideEffect();
			},
			[this, &frame, &services, scene](RGExecuteContext& execute, PassData& data)
			{
				auto* commands = execute.GetDirectComputeCommandContext();
				ScopedConstantBindings<RHIComputeCommandContext> constants(
					*commands, m_State->m_Layout, m_State->m_Scene, m_State->m_Atmosphere);
				if (!commands->RetainForSubmission(scene.m_Lifetime) || !constants.SetPipeline(m_State->m_Pipeline) ||
					!constants.Bind(m_State->m_Scene, frame.m_FrameConstants.m_SceneSnapshot) ||
					!constants.Bind(m_State->m_Atmosphere, data.m_Atmosphere) ||
					!commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::ViewSB), frame.m_FrameConstants.m_Views) ||
					!commands->SetAccelerationStructure(m_State->m_Parameters[0], execute.GetAccelerationStructureHandle(scene.m_TopLevel)))
				{
					return;
				}
				commands->SetReadOnlyBuffer(static_cast<uint32_t>(CommonRSRootParamIndex::LightSB),
					services.m_FrameBuffers->GetLightStructuredBuffer()->GetBufferHandle(frame.m_FrameSlotIndex));
				const std::array buffers{ scene.m_Vertices, scene.m_Indices, scene.m_Geometries, scene.m_Instances, scene.m_Materials };
				for (uint32_t index = 0; index < buffers.size(); ++index)
				{
					commands->SetReadOnlyBuffer(m_State->m_Parameters[index + 1], execute.GetBufferHandle(buffers[index]));
				}
				auto parameters = data.m_Parameters;
				parameters.m_Depth = execute.GetViewDescriptor(data.m_Depth).m_Index;
				parameters.m_Face = execute.GetViewDescriptor(data.m_Face).m_Index;
				parameters.m_Shading = execute.GetViewDescriptor(data.m_Shading).m_Index;
				parameters.m_ReceiverAlbedo = execute.GetViewDescriptor(data.m_ReceiverAlbedo).m_Index;
				parameters.m_LambertRadiance = execute.GetViewDescriptor(data.m_LambertRadiance).m_Index;
				parameters.m_SunIrradiance = execute.GetViewDescriptor(data.m_SunIrradiance).m_Index;
				parameters.m_EnvironmentIrradiance = execute.GetViewDescriptor(data.m_EnvironmentIrradiance).m_Index;
				parameters.m_PathDiagnostics = execute.GetViewDescriptor(data.m_PathDiagnostics).m_Index;
				for (size_t index = 0; index < data.m_Previous.size(); ++index)
				{
					if (parameters.m_SampleBegin != 0)
					{
						const auto descriptor = execute.GetViewDescriptor(data.m_Previous[index]).m_Index;
						if (index < parameters.m_Previous.size())
						{
							parameters.m_Previous[index] = descriptor;
						}
						else
						{
							parameters.m_PreviousCompensation[index - parameters.m_Previous.size()] = descriptor;
						}
					}
					if (parameters.m_ObservationEnabled && parameters.m_SampleCount != 0)
					{
						const auto descriptor = execute.GetViewDescriptor(data.m_Next[index]).m_Index;
						if (index < parameters.m_Next.size())
						{
							parameters.m_Next[index] = descriptor;
						}
						else
						{
							parameters.m_NextCompensation[index - parameters.m_Next.size()] = descriptor;
						}
					}
				}
				if (data.m_Transmittance.IsValid())
				{
					parameters.m_Transmittance = execute.GetViewDescriptor(data.m_Transmittance).m_Index;
				}
				for (size_t index = 0; index < data.m_Outputs.size(); ++index)
				{
					parameters.m_Outputs[index] = execute.GetViewDescriptor(data.m_Outputs[index]).m_Index;
				}
				// The observation bounds the entire submission; row dispatches also bound
				// encoder work. Resolve-only frames do not advance samples or RNG.
				const uint64_t rowWork = static_cast<uint64_t>(parameters.m_Width) *
					std::max(1u, parameters.m_SampleCount) * (2ull * parameters.m_MaximumDepth);
				const uint32_t rows = std::max(8u, static_cast<uint32_t>(8'388'608 / rowWork) / 8 * 8);
				for (uint32_t row = 0; row < parameters.m_Height; row += rows)
				{
					parameters.m_RowOffset = row;
					if (!commands->SetConstantBufferData(
						static_cast<uint32_t>(CommonRSRootParamIndex::PassCB), parameters))
					{
						return;
					}
					const auto count = std::min(rows, parameters.m_Height - row);
					commands->Dispatch((parameters.m_Width + 7) / 8, (count + 7) / 8, 1);
				}
				if (services.m_GIReferenceObservation)
				{
					services.m_GIReferenceObservation->MarkRecorded();
				}
			});
	}
}
