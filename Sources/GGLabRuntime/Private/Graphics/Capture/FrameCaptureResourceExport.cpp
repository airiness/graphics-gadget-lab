#include "Graphics/Capture/FrameCaptureService.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchDefaults.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderGraph/RGExecutionPlan.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderGraphSnapshot.h"

#include <algorithm>
#include <format>
#include <limits>
#include <numeric>
#include <tuple>

namespace gglab
{
	std::string_view GetResourceExportDtype(ResourceExportScalar scalar) noexcept
	{
		switch (scalar)
		{
		case ResourceExportScalar::Uint8:
			return "|u1";
		case ResourceExportScalar::Uint32:
			return "<u4";
		case ResourceExportScalar::Float16:
			return "<f2";
		case ResourceExportScalar::Float32:
			return "<f4";
		}
		return "Unknown";
	}

	namespace
	{
		std::optional<ResourceExportSemantics> Semantics(std::string_view name, ResourceExportFrameContext context)
		{
			if (name == "DisplayView.SceneColor" || name == "Atmosphere.AerialSceneColor" ||
				name == "TAA.ResolvedSceneColor" || name == "TAA.CurrentSceneColor")
			{
				return ResourceExportSemantics{ .m_Role = name == "TAA.ResolvedSceneColor" ? "scene-color.post-temporal" : "scene-color.pre-temporal",
					.m_Units = "scene-linear Rec.709, pre-exposed",
					.m_Space = "display view",
					.m_Description = "Stored radiance is scene-linear radiance multiplied by preExposure.",
					.m_PreExposure = context.m_PreExposure };
			}
			if (name == "DisplayView.DepthBuffer" || name == "DisplayView.DisplayDepth")
			{
				return ResourceExportSemantics{ .m_Role = "depth",
					.m_Units = "device depth [0, 1]",
					.m_Space = "display view",
					.m_Description = "Projection depth; reversedDepth records the selected view convention.",
					.m_ReversedDepth = context.m_ReversedDepth };
			}
			if (name == "Temporal.MotionVectors")
			{
				return ResourceExportSemantics{ .m_Role = "motion-vectors",
					.m_Units = "UV displacement",
					.m_Space = "display view",
					.m_Description = "Current UV minus previous UV; positive X points right and positive Y points down. Previous UV = current UV - motion." };
			}
			if (name == "GTAO.FinalAO" || name == "GTAO.TemporalAO" || name == "GTAO.RawAO" ||
				name == "GTAO.DenoiseX" || name == "GTAO.DenoiseY")
			{
				return ResourceExportSemantics{ .m_Role = "ambient-occlusion",
					.m_Units = "dimensionless [0, 1]",
					.m_Space = "display view",
					.m_Description = "Ambient visibility; one is unoccluded." };
			}
			if (name == "RayScene.SunVisibility")
			{
				return ResourceExportSemantics{ .m_Role = "ray-scene.sun-visibility",
					.m_Units = "R/G visibility [0,1]; B receiver state; A metres",
					.m_Space = "render view, jittered opaque raster samples",
					.m_Description = "R: binary RT sun visibility. G: production CSM filtering/bias/blend/fade. B: 1 matched opaque receiver, 0 background, -1 unmatched raster depth. A: first accepted blocker distance, -1 on miss; not nearest-hit distance. Ray origins use triangle-face ULP offsets; sun rays stop at 1000 m." };
			}
			if (name == "GI.Reference.Irradiance" || name == "GI.Reference.LambertRadiance" ||
				name == "GI.Reference.SunIrradiance" || name == "GI.Reference.EnvironmentIrradiance")
			{
				return ResourceExportSemantics{
					.m_Role = name == "GI.Reference.LambertRadiance" ? "gi.reference.lambert-radiance" : "gi.reference.irradiance",
					.m_Units = "scene-referred linear RGB, unexposed",
					.m_Space = "Native unjittered render view before temporal integration",
					.m_Description = "Stateless one-bounce reference. SunIrradiance RGB and EnvironmentIrradiance RGB sum to Irradiance RGB. Sun alpha: atmosphere attenuation bound (0/1); environment alpha: actual cubemap LOD, -1 for direct constant input (background 0). Irradiance excludes receiver albedo and direct receiver sun. LambertRadiance is reduced diffuse albedo * E / pi, not full Forward material or Final. Irradiance alpha: 0 background, 1 valid, -1 unsupported receiver, -2 unmatched/invalid receiver, -3 invalid secondary sample; never drop invalid pixels from metrics." };
			}
			if (name == "GI.ProbeUpdate.Rays" || name == "GI.Field.Rays")
			{
				return ResourceExportSemantics{ .m_Role = name.starts_with("GI.Field.") ? "gi-field" : "gi-probe-update", .m_Units = "scene-linear radiance / metres / fractions as described",
					.m_Space = "probe grid", .m_Description = "Float4 per probe/ray: reduced outgoing scene-linear radiance RGB and hit distance in metres. Miss distance is t_max; single-sided backface is -0.2*t with zero radiance. Invalid samples remain non-finite." };
			}
			if (name == "GI.ProbeUpdate.Irradiance" || name == "GI.Field.Irradiance")
			{
				return ResourceExportSemantics{ .m_Role = name.starts_with("GI.Field.") ? "gi-field" : "gi-probe-update", .m_Units = "scene-linear radiance / metres / fractions as described",
					.m_Space = "probe grid", .m_Description = "RGB: cosine-weighted radiance ratio M; incident E=pi*M. A: initialized texel marker. FP32 octahedral 6x6 interiors with one-texel seam borders. Experimental workload, not a production field certificate." };
			}
			if (name == "GI.ProbeUpdate.Distance" || name == "GI.Field.Distance")
			{
				return ResourceExportSemantics{ .m_Role = name.starts_with("GI.Field.") ? "gi-field" : "gi-probe-update", .m_Units = "scene-linear radiance / metres / fractions as described",
					.m_Space = "probe grid", .m_Description = "RG: weighted mean and mean-square of min(abs(hit distance), 1.5*length(spacing)). FP32 octahedral 14x14 interiors and one-texel seam borders; cosine-power-50 weights." };
			}
			if (name == "GI.ProbeUpdate.State" || name == "GI.Field.State")
			{
				return ResourceExportSemantics{ .m_Role = name.starts_with("GI.Field.") ? "gi-field" : "gi-probe-update", .m_Units = "scene-linear radiance / metres / fractions as described",
					.m_Space = "probe grid", .m_Description = "RGBA: miss, front/two-sided surface, single-sided backface and invalid fractions over every declared ray. Zero marks an unupdated probe; invalid rays are never dropped from a denominator. The separately labelled traversal-only control reports unclassified committed hits in G and unsigned distance, with zero radiance; it does not evaluate material sidedness." };
			}
			constexpr std::array giRecords
			{
				std::pair{ "GI.SpecularVisibility.Visibility", "RGB: base, coat and Rec709-luminance layer-weighted continuous GGX environment visibility. A: 0 background/pending, 1 valid calculated estimate, 2 all lobes inactive, -1 unsupported/unmatched primary, -2 invalid sampling. GPU completion is not numerical qualification. Not radiance, reflection transport, specular GI or production S1." },
				std::pair{ "GI.SpecularVisibility.Base", "RGBA: base numerator, denominator, below-face count and invalid count, each divided by full submitted samples. Normalize per pixel before ROI area means; do not divide ROI sums." },
				std::pair{ "GI.SpecularVisibility.Coat", "RGBA: coat numerator, denominator, below-face count and invalid count, each divided by full submitted samples. Inactive lobes have zero sums, not a failed denominator." },
				std::pair{ "GI.SpecularVisibility.Weights", "RGB: base directional specular albedo times squared coat transmission. A: coat factor times coat directional albedo. Constant-white-environment scalar composite; excludes material AO and arbitrary environment radiance." },
				std::pair{ "GI.SpecularVisibility.Position", "RGB: committed primary world position in metres. A: snapshot instance index, not raster material index; use receiver coverage and Visibility alpha for validity." },
				std::pair{ "GI.SpecularVisibility.Diagnostics", "R: primary primitive index, or failure -1 unsupported, -2 nonfinite position, -3 invalid ray hit, -4 face mismatch, -5 bounded primary miss, -6 illegal material input. G: active lobe bits (base=1, coat=2). B: complete-sweep sample count. A: invalid sampling flag. Background zero. Partial sweeps retain the preceding complete estimate." },
				std::pair{ "GI.SpecularVisibility.F0", "RGB: full primary F0 reconstructed from IOR, raster base color and metallic. A: effective IOR (zero denotes infinite-IOR F0=1). Never inferred from diffuse energy." },
				std::pair{ "GI.SpecularVisibility.LayerEnergy", "RGBA: coat factor, coat directional albedo, base NoV and coat NoV. Uses the ready published BRDF LUT bound to this observation." },
				std::pair{ "GI.Reference.Position", "RGB: reconstructed committed-triangle world position in metres. A: 1 matched primary receiver, 0 otherwise." },
				std::pair{ "GI.Reference.Identity", "R/G: primary snapshot instance and primitive indices; B: oriented geometric normal Y; A: 1 centre-ray match, 2 bounded subpixel match, 3 paired raster-interpolant local match. Every kind requires matching raster face/plane and native triangles. -1 without receiver." },
				std::pair{ "GI.Reference.Diagnostics", "RGBA: miss, surface hit, single-sided backface hit and invalid secondary fractions, divided by requested sample count. Below-face directions contribute zero and stay in the denominator." },
				std::pair{ "GI.Receiver.FaceNormal", "RGB: world triangle face normal oriented toward vertex normal. A: 0 background, 1 opaque, 2 MASK; BLEND excluded." },
				std::pair{ "GI.Receiver.Position", "RGB: world position interpolated by the paired coverage vertex shader. A: 0 background, 1 opaque, 2 MASK. Supplemental primary matching data; canonical position still uses paired opaque depth." },
				std::pair{ "GI.Receiver.VertexNormal", "RGB: interpolated world vertex normal after double-sided flip. A: raster front face +1, back face -1; background 0." },
				std::pair{ "GI.Receiver.ShadingNormal", "RGB: production normal-mapped world shading normal. A: metallic [0,1]." },
				std::pair{ "GI.Receiver.DiffuseWeight", "RGB: production directional diffuse weight [0,1]. A: clearcoat factor [0,1]." },
				std::pair{ "GI.Receiver.BaseColor", "RGB: scene-linear production base color. A: frame raster material table index; not a ray-snapshot material index." },
				std::pair{ "GI.Receiver.ReflectionRoughness", "RGB: production anisotropic IBL reflection direction in world space. A: effective base perceptual roughness." },
				std::pair{ "GI.Receiver.ClearcoatNormalRoughness", "RGB: production world clearcoat normal. A: effective clearcoat perceptual roughness." },
				std::pair{ "GI.Receiver.HitAlbedo", "RGB: reduced GI diffuse albedo at explicit <=64 texel mip, using raster UVs. A: 1 covered, 0 background." },
				std::pair{ "RayScene.HitAlbedo", "RGB: reduced GI diffuse albedo from committed triangle UVs and snapshot material. A: 0 background, 1 hit, 2 single-sided backface, -1 unmatched, -2 invalid." },
				std::pair{ "RayScene.HitNormal", "RGB: transformed, interpolated world vertex normal, flipped only for double-sided backfaces. A: native front face +1, back face -1; 0 without hit." },
				std::pair{ "RayScene.HitIdentity", "RGBA: snapshot instance, primitive, material and geometry indices; -1 without hit. Exact float integers within current scene limits." },
			};
			for (const auto& [resource, description] : giRecords)
			{
				if (name == resource)
				{
					return ResourceExportSemantics{ .m_Role = "gi.surface-data", .m_Units = "scene-referred, unexposed; channel-specific",
						.m_Space = "Native unjittered render view before temporal integration", .m_Description = description };
				}
			}
			return std::nullopt;
		}

		bool DescribeTextureFormat(RHIFormat format, RHITextureAspect aspect, bool rawTypeless,
			ResourceExportScalar& scalar, uint32_t& channels) noexcept
		{
			channels = 1;
			if (aspect == RHITextureAspect::Stencil)
			{
				scalar = ResourceExportScalar::Uint8;
				return true;
			}
			if (rawTypeless)
			{
				const auto bytes = GetRHIFormatInfo(format).m_BytesPerBlock;
				if (bytes == 0 || bytes % 4 != 0)
				{
					return false;
				}
				channels = bytes / 4;
				scalar = ResourceExportScalar::Uint32;
				return true;
			}
			switch (format)
			{
			case RHIFormat::R8Unorm:
				scalar = ResourceExportScalar::Uint8;
				break;
			case RHIFormat::R8G8B8A8Unorm:
			case RHIFormat::R8G8B8A8UnormSrgb:
			case RHIFormat::B8G8R8A8Unorm:
			case RHIFormat::B8G8R8A8UnormSrgb:
				scalar = ResourceExportScalar::Uint8;
				channels = 4;
				break;
			case RHIFormat::R16Float:
				scalar = ResourceExportScalar::Float16;
				break;
			case RHIFormat::R16G16Float:
				scalar = ResourceExportScalar::Float16;
				channels = 2;
				break;
			case RHIFormat::R16G16B16A16Float:
				scalar = ResourceExportScalar::Float16;
				channels = 4;
				break;
			case RHIFormat::R32Float:
			case RHIFormat::D32Float:
			case RHIFormat::D24UnormS8Uint:
				scalar = ResourceExportScalar::Float32;
				break;
			case RHIFormat::R32G32Float:
				scalar = ResourceExportScalar::Float32;
				channels = 2;
				break;
			case RHIFormat::R32G32B32Float:
				scalar = ResourceExportScalar::Float32;
				channels = 3;
				break;
			case RHIFormat::R32G32B32A32Float:
				scalar = ResourceExportScalar::Float32;
				channels = 4;
				break;
			case RHIFormat::R32Uint:
				scalar = ResourceExportScalar::Uint32;
				break;
			default:
				return false;
			}
			return true;
		}

		bool DescribeArray(const RGObservationTarget& target, ResourceExportArray& array,
			ResourceExportFrameContext context) noexcept
		{
			array.m_Address = target.m_Address;
			array.m_Type = target.m_Type;
			array.m_ResourceNodeIndex = target.m_Node.Value();
			array.m_Version = target.m_Resource->GetVersion();
			array.m_WriterPassIndex = target.m_Writer.IsValid() ? static_cast<int32_t>(target.m_Writer.Value()) : -1;
			array.m_WriterPass = target.m_WriterName;
			array.m_WriterOccurrence = target.m_WriterOccurrence;
			array.m_OriginalLastUse = target.m_OriginalLastUse;
			array.m_Semantics = Semantics(target.m_Address.m_Resource, context);
			if (target.m_Type == RGResourceType::RGBuffer)
			{
				const auto& desc = target.m_BufferDesc;
				const uint32_t stride = desc.m_StrideInBytes == 0 ? 4 : desc.m_StrideInBytes;
				if (GetResourceExportSupport(desc, false) != RGObservationFailure::None)
				{
					return false;
				}
				array.m_Stride = stride;
				array.m_ElementCount = desc.m_SizeInBytes / stride;
				array.m_Shape = { array.m_ElementCount, stride / 4 };
				GpuBufferLayout layout;
				if (target.m_Address.m_Resource == "ForwardPlus.TileLightHeaders")
				{
					layout = ForwardPlusTileHeaderLayout;
				}
				if (target.m_Address.m_Resource == "ForwardPlus.TileDepthRanges")
				{
					layout = ForwardPlusTileDepthRangeLayout;
				}
				if (target.m_Address.m_Resource == "GI.ProbeUpdate.Rays" ||
					target.m_Address.m_Resource == "GI.Field.Rays")
				{
					static constexpr std::array fields{ GpuBufferFieldLayout{ "radianceDistance", 0, GpuBufferScalar::Float32, 4 } };
					layout = { 16, fields };
				}
				if (layout.m_Stride == stride && !layout.m_Fields.empty())
				{
					for (const auto& field : layout.m_Fields)
					{
						array.m_Fields.push_back({ field.m_Name, field.m_Offset, field.m_Scalar, field.m_Count });
					}
					array.m_Shape = { array.m_ElementCount };
				}
				array.m_Encoding = "raw";
				return true;
			}
			array.m_PhysicalFormat = target.m_TextureDesc.m_Format;
			array.m_ViewFormat = target.m_ViewFormat;
			array.m_RawTypeless = target.m_RawTypeless;
			const auto aspect = target.m_Subresources.m_Aspects;
			uint32_t channels = 1;
			const auto format = target.m_ViewFormat;
			if (!DescribeTextureFormat(target.m_RawTypeless ? target.m_TextureDesc.m_Format : format,
				aspect, target.m_RawTypeless, array.m_Scalar, channels))
			{
				return false;
			}
			array.m_Address.m_Mip = target.m_Subresources.m_BaseMip;
			array.m_Address.m_FirstSlice = target.m_Subresources.m_BaseArraySlice;
			array.m_Address.m_SliceCount = target.m_Subresources.m_ArraySliceCount;
			array.m_Address.m_Aspect = aspect;
			array.m_Shape = { target.m_Subresources.m_ArraySliceCount,
				std::max(1u, target.m_TextureDesc.m_Extent.m_Height >> target.m_Subresources.m_BaseMip),
				std::max(1u, target.m_TextureDesc.m_Extent.m_Width >> target.m_Subresources.m_BaseMip), channels };
			array.m_ChannelOrder = channels == 4 ?
				((format == RHIFormat::B8G8R8A8Unorm || format == RHIFormat::B8G8R8A8UnormSrgb) ? "BGRA" : "RGBA") :
				channels == 3 ? "RGB" : channels == 2 ? "RG" : "R";
			array.m_Encoding = format == RHIFormat::D24UnormS8Uint && aspect == RHITextureAspect::Depth ?
				"normalized-depth" :
				(format == RHIFormat::R8G8B8A8UnormSrgb || format == RHIFormat::B8G8R8A8UnormSrgb) ?
					"srgb" :
					array.m_Scalar == ResourceExportScalar::Uint8 && aspect == RHITextureAspect::Color ? "unorm" : "raw";
			return true;
		}
	}

	RGObservationFailure GetResourceExportSupport(const RHITextureDesc& desc, bool imported) noexcept
	{
		if (desc.m_Dimension != RHITextureDimension::Texture2D || desc.m_SampleCount != 1 ||
			desc.m_Extent.m_Width == 0 || desc.m_Extent.m_Height == 0 ||
			(imported && !Test(desc.m_Usage, RHITextureUsage::CopySource)))
		{
			return RGObservationFailure::UnsupportedResource;
		}
		const auto& info = GetRHIFormatInfo(desc.m_Format);
		if (info.m_BlockWidth != 1 || info.m_BlockHeight != 1 || info.m_BytesPerBlock == 0)
		{
			return RGObservationFailure::UnsupportedFormat;
		}
		const auto aspects = GetRHITextureAspects(desc);
		const auto aspect = Test(aspects, RHITextureAspect::Depth) ? RHITextureAspect::Depth : RHITextureAspect::Color;
		ResourceExportScalar scalar;
		uint32_t channels;
		return DescribeTextureFormat(desc.m_Format, aspect, info.m_IsTypeless, scalar, channels) ?
			RGObservationFailure::None :
			RGObservationFailure::UnsupportedFormat;
	}

	RGObservationFailure GetResourceExportSupport(const RHIBufferDesc& desc, bool imported) noexcept
	{
		const uint32_t stride = desc.m_StrideInBytes == 0 ? 4 : desc.m_StrideInBytes;
		if (desc.m_SizeInBytes == 0 || stride % 4 != 0 || desc.m_SizeInBytes % stride != 0 ||
			(imported && !Test(desc.m_Usage, RHIBufferUsage::CopySource)))
		{
			return RGObservationFailure::UnsupportedResource;
		}
		return RGObservationFailure::None;
	}

	std::pair<uint64_t, uint32_t> FrameCaptureService::GetPayloadUsage(bool includeProduction) const noexcept
	{
		auto [bytes, slots] = m_PayloadBudget.GetUsage();
		if (includeProduction)
		{
			const auto [productionBytes, productionSlots] = m_ProductionBudget.GetUsage();
			bytes += productionBytes;
			slots += productionSlots;
		}
		return { bytes, slots };
	}

	uint64_t FrameCaptureService::RequestResourceExport(std::vector<RGResourceAddress> targets,
		uint64_t frameSerial, bool deferFrameOnBusy, bool includeGraph, uint64_t groupId) noexcept
	{
		if (GetRequestAvailability() == FrameCaptureAvailability::Busy)
		{
			return 0;
		}
		const uint64_t id = m_NextRequestId++;
		if (m_IsShutdown || targets.empty() || targets.size() > m_Limits.m_MaxPayloads)
		{
			m_Results.push_back({ .m_RequestId = id, .m_Source = FrameCaptureSource::ResourceExport,
				.m_ResourceFailure = targets.size() > m_Limits.m_MaxPayloads ? RGObservationFailure::TooLarge : RGObservationFailure::UnsupportedResource,
				.m_Failure = m_IsShutdown ? "The render host has been finalized." : "The resource export group is empty or exceeds the payload slot limit." });
			return id;
		}
		m_Queued.push_back({ .m_Id = id, .m_Source = FrameCaptureSource::ResourceExport, .m_FrameSerial = frameSerial,
			.m_GroupId = groupId, .m_ResourceTargets = std::move(targets), .m_DeferFrameOnBusy = deferFrameOnBusy, .m_IncludeGraph = includeGraph });
		DeferUnrelatedImages(frameSerial);
		return id;
	}

	FrameCaptureAvailability FrameCaptureService::GetResourceExportFrameAvailability() const noexcept
	{
		if (m_IsShutdown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		// Continuous instrumentation never becomes idle. Only user payloads gate scheduling;
		// the full shared budget is still reserved atomically against the compiled group.
		if (GetRequestAvailability() != FrameCaptureAvailability::Available || GetPayloadUsage(false).second != 0)
		{
			return FrameCaptureAvailability::Busy;
		}
		return FrameCaptureAvailability::Available;
	}

	void FrameCaptureService::DeferFrameCaptures(uint64_t frameSerial) noexcept
	{
		const auto defer = [&](const QueuedRequest& request)
			{
				if (request.m_FrameSerial == 0)
				{
					m_Queued.push_back(request);
					return;
				}
				m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = request.m_Source,
					.m_Status = FrameCaptureStatus::Busy, .m_FrameSerial = frameSerial,
					.m_ResourceFailure = RGObservationFailure::Busy });
			};
		std::erase_if(m_Taps, [&](const Tap& tap)
			{
				if (tap.m_FrameSerial != frameSerial || tap.m_Readback.IsSubmitted())
				{
					return false;
				}
				for (const auto& request : tap.m_Requests)
				{
					defer(request);
				}
				return true;
			});
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if (request.m_FrameSerial != frameSerial)
				{
					return false;
				}
				m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = request.m_Source,
					.m_Status = FrameCaptureStatus::Busy, .m_FrameSerial = frameSerial,
					.m_ResourceFailure = RGObservationFailure::Busy });
				return true;
			});
		std::ranges::sort(m_Queued, {}, &QueuedRequest::m_Id);
	}

	FrameCaptureAvailability FrameCaptureService::PrepareResourceExports(uint64_t frameSerial,
		RenderGraph& graph, ResourceExportFrameContext context) noexcept
	{
		if (m_IsShutdown)
		{
			return FrameCaptureAvailability::Finalized;
		}
		if (!HasPendingRequests(FrameCaptureSource::ResourceExport) && !HasPendingRequests(FrameCaptureSource::NonFiniteScan) && !m_ProductionEnabled)
		{
			return FrameCaptureAvailability::Available;
		}
		const uint64_t productionMinimumBytes = m_ProductionEnabled ?
			ProductionCaptureProducer::MinimumBytes : 0u;
		const uint32_t productionMinimumSlots = m_ProductionEnabled ? 2u : 0u;
		std::vector<Tap> prepared;
		auto [usedBytes, usedSlots] = GetPayloadUsage();
		Tap production;
		RGProductionCounters counters;
		if (m_ProductionEnabled)
		{
			CollectCompleted();
			std::tie(usedBytes, usedSlots) = GetPayloadUsage();
			if (usedSlots >= m_Limits.m_MaxPayloads || usedBytes > m_Limits.m_MaxPayloadBytes ||
				ProductionCaptureProducer::PayloadBytes > m_Limits.m_MaxPayloadBytes - usedBytes ||
				!PrepareProductionCounters(frameSerial, production, counters))
			{
				DeferFrameCaptures(frameSerial);
				return FrameCaptureAvailability::Busy;
			}
			production.m_PayloadLease = m_ProductionBudget.Reserve(ProductionCaptureProducer::PayloadBytes, 1);
			usedBytes += ProductionCaptureProducer::PayloadBytes;
			++usedSlots;
		}
		bool deferFrame = false;
		// Unbound images record after graph observation admission. Reserve their slots while
		// admitting a strict scan, otherwise an admitted scan can crowd out its own
		// frame's image. Multiple requests for one source share the same image tap.
		std::vector<QueuedRequest> frameImages;
		size_t boundImageSlots = 0;
		uint64_t boundImageBytes = 0;
		for (const auto& image : m_Taps)
		{
			if (image.m_FrameSerial == frameSerial && !image.m_Readback.IsSubmitted() &&
				(image.m_Source == FrameCaptureSource::Scene || image.m_Source == FrameCaptureSource::Composited ||
					image.m_Source == FrameCaptureSource::Diagnostic) && !image.m_ProductionCounters)
			{
				++boundImageSlots;
				boundImageBytes += image.m_PayloadLease.GetBytes();
			}
		}
		for (const auto& request : m_Queued)
		{
			if ((request.m_FrameSerial == 0 || request.m_FrameSerial == frameSerial) &&
				(request.m_Source == FrameCaptureSource::Scene || request.m_Source == FrameCaptureSource::Composited ||
					request.m_Source == FrameCaptureSource::Diagnostic) &&
				!std::ranges::any_of(frameImages, [&](const auto& image) { return IsBoundBy(image, request.m_Source, request.m_DiagnosticTap); }))
			{
				frameImages.push_back(request);
			}
		}
		// Exact sequence groups claim this frame before flexible concurrent exports.
		std::stable_sort(m_Queued.begin(), m_Queued.end(), [](const auto& a, const auto& b)
			{
				return a.m_DeferFrameOnBusy && !b.m_DeferFrameOnBusy;
			});
		std::erase_if(m_Queued, [&](const QueuedRequest& request)
			{
				if ((request.m_Source != FrameCaptureSource::ResourceExport && request.m_Source != FrameCaptureSource::NonFiniteScan) ||
					(request.m_FrameSerial != 0 && request.m_FrameSerial != frameSerial))
				{
					return false;
				}
				Tap tap{ .m_FrameSerial = frameSerial, .m_Source = request.m_Source };
				tap.m_Requests.push_back(request);
				uint64_t pairedBytes = 0;
				uint32_t pairedSlots = 0;
				for (const auto& imageTap : m_Taps)
				{
					if (request.m_GroupId != 0 && imageTap.m_FrameSerial == frameSerial &&
						imageTap.m_Source != FrameCaptureSource::RenderGraph && imageTap.m_Source != FrameCaptureSource::ResourceExport &&
						imageTap.m_Source != FrameCaptureSource::NonFiniteScan &&
						std::ranges::any_of(imageTap.m_Requests, [&](const auto& imageRequest) { return imageRequest.m_GroupId == request.m_GroupId; }))
					{
						pairedBytes += imageTap.m_PayloadLease.GetBytes();
						++pairedSlots;
					}
				}
				uint64_t bytes = 0;
				RGObservationFailure failure = RGObservationFailure::None;
				for (const auto& address : request.m_ResourceTargets)
				{
					ResourceCaptureReadback readback;
					readback.m_Copy.m_Target = graph.ResolveObservation(address);
					failure = readback.m_Copy.m_Target.m_Failure;
					if (failure != RGObservationFailure::None)
					{
						break;
					}
					const auto& target = readback.m_Copy.m_Target;
					if (!DescribeArray(target, readback.m_Array, context))
					{
						failure = RGObservationFailure::UnsupportedFormat;
						break;
					}
					if (readback.m_Array.m_Semantics)
					{
						readback.m_Array.m_Semantics->m_WatchTier = GetNonFiniteDefaultWatchTier(graph, target);
					}
					if (target.m_Type == RGResourceType::RGTexture)
					{
						readback.m_Copy.m_Footprint = ComputeRHIObservationCopyFootprint(target.m_TextureDesc,
							target.m_Subresources.m_BaseMip, target.m_Subresources.m_Aspects);
						const auto& footprint = readback.m_Copy.m_Footprint;
						if (!footprint.IsValid())
						{
							failure = RGObservationFailure::UnsupportedFormat;
							break;
						}
						if (footprint.m_SizeInBytes > m_Limits.m_MaxPayloadBytes)
						{
							failure = RGObservationFailure::TooLarge;
							break;
						}
						// Every slice starts at both a placed-footprint and texel-block boundary.
						// In particular, RGB32F needs lcm(512, 12), not just 512 bytes.
						const uint64_t sliceAlignment = std::lcm<uint64_t>(RHITextureCopyPlacementAlignment, footprint.m_BytesPerTexel);
						const uint64_t slicePadding = (sliceAlignment - footprint.m_SizeInBytes % sliceAlignment) % sliceAlignment;
						if (slicePadding > m_Limits.m_MaxPayloadBytes - footprint.m_SizeInBytes)
						{
							failure = RGObservationFailure::TooLarge;
							break;
						}
						readback.m_Copy.m_SlicePitch = footprint.m_SizeInBytes + slicePadding;
						if (readback.m_Copy.m_SlicePitch > m_Limits.m_MaxPayloadBytes / target.m_Subresources.m_ArraySliceCount)
						{
							failure = RGObservationFailure::TooLarge;
							break;
						}
						readback.m_Copy.m_ReadbackBytes = readback.m_Copy.m_SlicePitch * target.m_Subresources.m_ArraySliceCount;
						readback.m_CpuBytes = footprint.m_RowSizeInBytes * footprint.m_Height * target.m_Subresources.m_ArraySliceCount;
					}
					else
					{
						readback.m_Copy.m_ReadbackBytes = readback.m_CpuBytes = target.m_BufferDesc.m_SizeInBytes;
					}
					if (readback.m_Copy.m_ReadbackBytes > m_Limits.m_MaxPayloadBytes ||
						readback.m_CpuBytes > m_Limits.m_MaxPayloadBytes - readback.m_Copy.m_ReadbackBytes ||
						bytes > m_Limits.m_MaxPayloadBytes - readback.m_Copy.m_ReadbackBytes - readback.m_CpuBytes)
					{
						failure = RGObservationFailure::TooLarge;
						break;
					}
					bytes += readback.m_Copy.m_ReadbackBytes + readback.m_CpuBytes;
					tap.m_Resources.m_Readbacks.push_back(std::move(readback));
				}
				for (const auto& scanTarget : request.m_ScanTargets)
				{
					ScanCaptureReadback readback;
					failure = PrepareScanTarget(graph, scanTarget, readback);
					if (failure != RGObservationFailure::None)
					{
						break;
					}
					const uint64_t scanBytes = readback.GetPayloadBytes();
					if (scanBytes > m_Limits.m_MaxPayloadBytes || bytes > m_Limits.m_MaxPayloadBytes - scanBytes)
					{
						failure = RGObservationFailure::TooLarge;
						break;
					}
					bytes += scanBytes;
					tap.m_Scans.m_Readbacks.push_back(std::move(readback));
				}
				// Exports take a slot per array; a scan group shares one slot for all records.
				const auto slots = tap.m_Resources.GetPayloadCost().second + tap.m_Scans.GetPayloadCost().second;
				const bool strictScan = request.m_Source == FrameCaptureSource::NonFiniteScan && request.m_DeferFrameOnBusy;
				const size_t imageSlots = strictScan ? frameImages.size() : 0;
				if (failure == RGObservationFailure::None && strictScan)
				{
					size_t required = slots + imageSlots + boundImageSlots + productionMinimumSlots;
					uint64_t requiredBytes = bytes + boundImageBytes + productionMinimumBytes;
					for (const auto& observation : prepared)
					{
						const auto [exportBytes, exportSlots] = observation.m_Resources.GetPayloadCost();
						const auto [scanBytes, scanSlots] = observation.m_Scans.GetPayloadCost();
						required += exportSlots + scanSlots;
						requiredBytes += exportBytes + scanBytes;
					}
					if (required > m_Limits.m_MaxPayloads || requiredBytes > m_Limits.m_MaxPayloadBytes)
					{
						failure = RGObservationFailure::TooLarge;
					}
				}
				// A flexible export can follow an already admitted fail-mode scan. That
				// scan is required on every retry, so include it in the idle-frame minimum
				// instead of reporting permanent incompatibility as temporary pressure.
				size_t requiredScanSlots = 0;
				uint64_t requiredScanBytes = 0;
				if (request.m_Source == FrameCaptureSource::ResourceExport)
				{
					for (const auto& observation : prepared)
					{
						if (observation.m_Source == FrameCaptureSource::NonFiniteScan &&
							std::ranges::any_of(observation.m_Requests, [](const auto& scan) { return scan.m_DeferFrameOnBusy; }))
						{
							const auto [scanBytes, scanSlots] = observation.m_Scans.GetPayloadCost();
							requiredScanSlots += scanSlots;
							requiredScanBytes += scanBytes;
						}
					}
				}
				if (failure == RGObservationFailure::None &&
					(slots + pairedSlots + requiredScanSlots + productionMinimumSlots > m_Limits.m_MaxPayloads ||
						bytes + pairedBytes + requiredScanBytes + productionMinimumBytes > m_Limits.m_MaxPayloadBytes))
				{
					failure = RGObservationFailure::TooLarge;
				}
				if (failure == RGObservationFailure::None && request.m_DeferFrameOnBusy &&
					(pairedSlots + slots > m_Limits.m_MaxPayloads || bytes > m_Limits.m_MaxPayloadBytes - pairedBytes))
				{
					failure = RGObservationFailure::TooLarge;
				}
				if (failure == RGObservationFailure::None && (usedSlots + slots + imageSlots > m_Limits.m_MaxPayloads ||
					usedBytes > m_Limits.m_MaxPayloadBytes ||
					bytes > m_Limits.m_MaxPayloadBytes - usedBytes))
				{
					failure = RGObservationFailure::Busy;
				}
				if (failure != RGObservationFailure::None)
				{
					if (failure == RGObservationFailure::Busy && request.m_DeferFrameOnBusy)
					{
						deferFrame = true;
						return false;
					}
					m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = request.m_Source,
						.m_Status = failure == RGObservationFailure::Busy ? FrameCaptureStatus::Busy : FrameCaptureStatus::Failed,
						.m_FrameSerial = frameSerial, .m_ResourceFailure = failure,
						.m_Failure = std::format("{}: observation group could not bind the selected frame.", GetRGObservationFailureName(failure)) });
					return true;
				}
				tap.m_PayloadLease = m_PayloadBudget.Reserve(bytes, static_cast<uint32_t>(slots));
				usedBytes += bytes;
				usedSlots += static_cast<uint32_t>(slots);
				tap.m_AdapterCompatibilityIdentity = m_Device->GetAdapterCompatibilityIdentity();
				prepared.push_back(std::move(tap));
				return true;
			});
		if (deferFrame)
		{
			for (auto& tap : prepared)
			{
				for (auto& request : tap.m_Requests)
				{
					m_Queued.push_back(std::move(request));
				}
			}
			DeferFrameCaptures(frameSerial);
			return FrameCaptureAvailability::Busy;
		}
		std::vector<RGObservationCopy> copies;
		std::vector<RGObservationScan> scans;
		for (auto& tap : prepared)
		{
			bool allocated = tap.m_Resources.Allocate(*m_Device, tap.m_Readback);
			for (auto& readback : tap.m_Scans.m_Readbacks)
			{
				if (!allocated || !AllocateScanReadback(readback, tap.m_Readback))
				{
					allocated = false;
					break;
				}
			}
			if (!allocated)
			{
				FailTap(tap, frameSerial, "Observation readback allocation or scan program resolution failed.");
				tap.m_Resources.m_Readbacks.clear();
				tap.m_Scans.m_Readbacks.clear();
				continue;
			}
			for (const auto& readback : tap.m_Resources.m_Readbacks)
			{
				copies.push_back(readback.m_Copy);
			}
			for (const auto& readback : tap.m_Scans.m_Readbacks)
			{
				scans.push_back(readback.m_Scan);
			}
		}
		if (!graph.AttachObservations(copies, scans, m_ProductionEnabled ? &counters : nullptr))
		{
			for (auto& tap : prepared)
			{
				if (!tap.m_Requests.empty())
				{
					FailTap(tap, frameSerial, "Observation graph extension failed.");
				}
			}
			return FrameCaptureAvailability::Available;
		}
		if (m_ProductionEnabled)
		{
			m_Taps.push_back(std::move(production));
		}
		if (!copies.empty() || !scans.empty() || m_ProductionEnabled)
		{
			for (auto& consumer : m_Taps)
			{
				if (consumer.m_FrameSerial == frameSerial)
				{
					consumer.m_ResourceExportActive = !copies.empty();
					consumer.m_NonFiniteScanActive = !scans.empty() || m_ProductionEnabled;
				}
			}
		}
		for (auto& tap : prepared)
		{
			if (tap.m_Requests.empty())
			{
				continue;
			}
			tap.m_ResourceExportActive = !copies.empty();
			tap.m_NonFiniteScanActive = !scans.empty() || m_ProductionEnabled;
			for (auto& readback : tap.m_Resources.m_Readbacks)
			{
				readback.m_Array.m_ObservedLastUse = graph.GetExecutionPlan()->GetResources()[
					readback.m_Copy.m_Target.m_VirtualResource.Value()].m_LastUser.Value();
			}
			m_Taps.push_back(std::move(tap));
		}
		return FrameCaptureAvailability::Available;
	}

	void FrameCaptureService::PublishResourceTap(Tap& tap) noexcept
	{
		if (tap.m_Graph && !tap.m_Graph->IsRecorded())
		{
			FailTap(tap, tap.m_FrameSerial, "The resource export's paired graph did not execute.");
			return;
		}
		const auto payload = tap.m_Resources.ReadProduct(*m_Device, tap.m_Readback, tap.m_PayloadLease);
		if (!payload)
		{
			FailTap(tap, tap.m_FrameSerial, "Resource export readback mapping failed.");
			return;
		}
		for (const auto& request : tap.m_Requests)
		{
			m_Results.push_back({ .m_RequestId = request.m_Id, .m_Source = FrameCaptureSource::ResourceExport,
				.m_Status = FrameCaptureStatus::Completed, .m_FrameSerial = tap.m_FrameSerial, .m_Graph = tap.m_Graph ? tap.m_Graph->GetProduct() : nullptr, .m_Resources = payload,
				.m_GraphExportActive = tap.m_GraphExportActive, .m_ResourceExportActive = true,
				.m_NonFiniteScanActive = tap.m_NonFiniteScanActive,
				.m_SubmissionFenceIndex = tap.m_Readback.GetFence().m_Fence.Index(),
				.m_SubmissionFenceGeneration = tap.m_Readback.GetFence().m_Fence.Generation(),
				.m_SubmissionFenceValue = tap.m_Readback.GetFence().m_Value,
				.m_AdapterCompatibilityIdentity = tap.m_AdapterCompatibilityIdentity });
		}
		tap.m_Requests.clear();
	}
}
