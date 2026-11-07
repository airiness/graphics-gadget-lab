#include "Capture/FrameCaptureMetadata.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "Capture/RequiredFrameState.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"
#include "GGLabFoundation/Hash/Sha256.h"
#include "GGLabRuntime/Graphics/Pipeline/GIFieldProfile.h"

#include <cmath>
#include <array>
#include <cstdint>
#include <format>
#include <iterator>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

namespace gglab
{
	namespace
	{
		// Minimal pretty-printing JSON emitter for the fixed capture schema.
		class JsonWriter final
		{
		public:
			void BeginObject(std::string_view key = {}) { Open(key, '{'); }
			void EndObject() { Close('}'); }
			void BeginArray(std::string_view key = {}) { Open(key, '['); }
			void EndArray() { Close(']'); }

			void String(std::string_view key, std::string_view value)
			{
				BeginValue(key);
				AppendString(value);
			}
			void Bool(std::string_view key, bool value)
			{
				BeginValue(key);
				m_Text += value ? "true" : "false";
			}
			void Unsigned(std::string_view key, uint64_t value)
			{
				BeginValue(key);
				std::format_to(std::back_inserter(m_Text), "{}", value);
			}
			// Non-finite values have no JSON representation and are written as null.
			void Number(std::string_view key, double value)
			{
				BeginValue(key);
				if (std::isfinite(value))
				{
					std::format_to(std::back_inserter(m_Text), "{}", value);
				}
				else
				{
					m_Text += "null";
				}
			}
			void Null(std::string_view key)
			{
				BeginValue(key);
				m_Text += "null";
			}
			void Json(std::string_view key, std::string_view value)
			{
				BeginValue(key);
				m_Text += value;
			}
			void Vector(std::string_view key, const std::array<float, 3>& value)
			{
				BeginArray(key);
				for (const float component : value)
				{
					Number({}, component);
				}
				EndArray();
			}

			[[nodiscard]] std::string Finish()
			{
				m_Text += "\r\n";
				return std::move(m_Text);
			}

		private:
			void Open(std::string_view key, char bracket)
			{
				BeginValue(key);
				m_Text += bracket;
				++m_Depth;
				m_FirstInScope = true;
			}

			void Close(char bracket)
			{
				--m_Depth;
				if (!m_FirstInScope)
				{
					NewLine();
				}
				m_Text += bracket;
				m_FirstInScope = false;
			}

			void BeginValue(std::string_view key)
			{
				if (m_Depth > 0)
				{
					if (!m_FirstInScope)
					{
						m_Text += ',';
					}
					NewLine();
				}
				m_FirstInScope = false;
				if (!key.empty())
				{
					AppendString(key);
					m_Text += ": ";
				}
			}

			void NewLine()
			{
				m_Text += "\r\n";
				m_Text.append(static_cast<size_t>(m_Depth) * 2, ' ');
			}

			void AppendString(std::string_view value)
			{
				m_Text += '"';
				for (const char character : value)
				{
					const auto byte = static_cast<unsigned char>(character);
					switch (character)
					{
					case '"':
						m_Text += "\\\"";
						break;
					case '\\':
						m_Text += "\\\\";
						break;
					case '\n':
						m_Text += "\\n";
						break;
					case '\r':
						m_Text += "\\r";
						break;
					case '\t':
						m_Text += "\\t";
						break;
					default:
						if (byte < 0x20)
						{
							std::format_to(std::back_inserter(m_Text), "\\u{:04x}",
								static_cast<uint32_t>(byte));
						}
						else
						{
							m_Text += character;
						}
						break;
					}
				}
				m_Text += '"';
			}

			std::string m_Text;
			int32_t m_Depth = 0;
			bool m_FirstInScope = true;
		};
	}

	namespace
	{
		void WriteSceneInputs(JsonWriter& writer, const FrameCaptureSceneInputs& inputs)
		{
			const auto vector =
				[&](std::string_view name, const Vector3& value)
				{
					writer.Vector(name, { value.m_X, value.m_Y, value.m_Z });
				};
			writer.BeginObject("sceneInputs");
			writer.Unsigned("schemaVersion", 1);
			writer.BeginObject("exposure");
			writer.Number("manualEV100", inputs.m_Exposure.m_ManualEV100);
			writer.Number("compensationEV", inputs.m_Exposure.m_CompensationEV);
			writer.Number("effectiveEV100", inputs.m_Exposure.m_EffectiveEV100);
			writer.Number("scale", inputs.m_Exposure.m_ExposureScale);
			writer.Number("preExposure", inputs.m_Exposure.m_PreExposure);
			writer.EndObject();
			writer.BeginObject("postProcess");
			writer.Unsigned("toneMapping", static_cast<uint32_t>(inputs.m_PostProcess.m_ToneMapping.m_Operator));
			const auto& bloom = inputs.m_PostProcess.m_Bloom;
			writer.Bool("bloomEnabled", bloom.m_Enabled);
			writer.Number("bloomThreshold", bloom.m_Threshold);
			writer.Number("bloomSoftKnee", bloom.m_SoftKnee);
			writer.Number("bloomIntensity", bloom.m_Intensity);
			writer.Number("bloomScatter", bloom.m_Scatter);
			writer.Unsigned("bloomMaxLevels", bloom.m_MaxLevels);
			writer.EndObject();
			writer.BeginObject("environment");
			const auto& environment = inputs.m_Environment;
			writer.Number("intensity", environment.m_Intensity);
			writer.Number("rotationRadians", environment.m_RotationRadians);
			writer.Bool("skybox", environment.m_EnableSkybox);
			writer.Unsigned("backgroundMode", static_cast<uint32_t>(environment.m_BackgroundMode));
			writer.String("iblQuality", GetIBLQualityPresetName(environment.m_QualityPreset));
			writer.Unsigned("activeIBLGeneration", inputs.m_ActiveIBLGeneration);
			const auto& config = environment.m_BakeConfig;
			writer.BeginObject("iblConfig");
			writer.Unsigned("environmentSize", config.m_EnvironmentCubemapSize);
			writer.Unsigned("environmentFormat", static_cast<uint32_t>(config.m_EnvironmentCubemapFormat));
			writer.Unsigned("irradianceSize", config.m_IrradianceCubemapSize);
			writer.Unsigned("irradianceFormat", static_cast<uint32_t>(config.m_IrradianceCubemapFormat));
			writer.Unsigned("irradianceSamples", config.m_IrradianceSampleCount);
			writer.Unsigned("specularSize", config.m_PrefilteredSpecularCubemapSize);
			writer.Unsigned("specularMipLevels", config.m_PrefilteredSpecularMipLevels);
			writer.Unsigned("specularFormat", static_cast<uint32_t>(config.m_PrefilteredSpecularCubemapFormat));
			writer.Unsigned("specularSamples", config.m_PrefilteredSpecularSampleCount);
			writer.Number("specularMaxSampleLuminance", config.m_PrefilteredSpecularMaxSampleLuminance);
			writer.Unsigned("brdfLutSize", config.m_BrdfLutSize);
			writer.Unsigned("brdfLutFormat", static_cast<uint32_t>(config.m_BrdfLutFormat));
			writer.EndObject();
			writer.EndObject();
			writer.BeginObject("shadows");
			const auto& shadows = inputs.m_Shadows;
			writer.Bool("enabled", shadows.m_Enable);
			writer.Bool("pcf", shadows.m_EnablePCF);
			writer.Unsigned("mapSize", shadows.m_ShadowMapSize);
			writer.Unsigned("cascadeCount", shadows.m_CascadeCount);
			writer.Number("splitLambda", shadows.m_SplitLambda);
			writer.Number("cascadeBlendFraction", shadows.m_CascadeBlendFraction);
			writer.Number("distanceFadeFraction", shadows.m_DistanceFadeFraction);
			writer.Unsigned("fitMode", static_cast<uint32_t>(shadows.m_FitMode));
			writer.Bool("texelSnapping", shadows.m_EnableTexelSnapping);
			writer.Number("maxDistance", shadows.m_MaxShadowDistance);
			writer.Number("casterExtrusion", shadows.m_CasterExtrusionDistance);
			writer.Number("orthoPadding", shadows.m_OrthoPadding);
			writer.Number("depthPadding", shadows.m_DepthPadding);
			writer.Number("receiverBiasTexels", shadows.m_ReceiverBiasTexels);
			writer.Number("receiverSlopeBiasTexels", shadows.m_ReceiverSlopeBiasTexels);
			writer.Number("receiverMaxSlope", shadows.m_ReceiverMaxSlope);
			writer.EndObject();
			if (inputs.m_Sun)
			{
				const auto& sun = *inputs.m_Sun;
				writer.BeginObject("sun");
				vector("photonDirection", sun.m_Direction);
				vector("chromaticity", sun.m_Chromaticity);
				vector("directIlluminance", sun.m_DirectIlluminance);
				vector("diskRadiance", sun.m_DiskRadiance);
				writer.Number("toaIlluminanceLux", sun.m_PerpendicularIlluminanceLux);
				writer.Number("angularRadiusRadians", sun.m_AngularRadiusRadians);
				writer.Number("projectedSolidAngle", sun.m_ProjectedSolidAngle);
				writer.EndObject();
			}
			else
			{
				writer.Null("sun");
			}
			if (inputs.m_Atmosphere)
			{
				const auto& atmosphere = *inputs.m_Atmosphere;
				writer.BeginObject("atmosphere");
				writer.Number("planetRadiusMeters", atmosphere.m_PlanetRadiusMeters);
				writer.Number("heightMeters", atmosphere.m_HeightMeters);
				vector("planetCenterMeters", atmosphere.m_PlanetCenterMeters);
				writer.Number("worldUnitsToMeters", atmosphere.m_WorldUnitsToMeters);
				vector("rayleighScattering", atmosphere.m_RayleighScattering);
				writer.Number("rayleighScaleHeightMeters", atmosphere.m_RayleighScaleHeightMeters);
				writer.Number("mieScattering", atmosphere.m_MieScattering);
				writer.Number("mieExtinction", atmosphere.m_MieExtinction);
				writer.Number("mieScaleHeightMeters", atmosphere.m_MieScaleHeightMeters);
				writer.Number("mieAnisotropy", atmosphere.m_MieAnisotropy);
				vector("absorption", atmosphere.m_Absorption);
				writer.Number("absorptionCenterMeters", atmosphere.m_AbsorptionCenterMeters);
				writer.Number("absorptionHalfWidthMeters", atmosphere.m_AbsorptionHalfWidthMeters);
				vector("groundAlbedo", atmosphere.m_GroundAlbedo);
				writer.EndObject();
			}
			else
			{
				writer.Null("atmosphere");
			}
			writer.Unsigned("localLightCount", inputs.m_LocalLightCount);
			writer.Bool("aerialPerspective", inputs.m_AerialPerspective);
			writer.EndObject();
		}
	}

	std::string GetFrameCapturePathText(const std::filesystem::path& path) noexcept
	{
		const std::u8string text = path.u8string();
		return std::string(text.begin(), text.end());
	}

	std::string SerializeNonFiniteWatchCoverage(const NonFiniteWatchCoverage& coverage) noexcept
	{
		const auto claimName = [](NonFiniteClaimStatus status)
			{
				switch (status)
				{
				case NonFiniteClaimStatus::Passed:
					return "Passed";
				case NonFiniteClaimStatus::Failed:
					return "Failed";
				case NonFiniteClaimStatus::NotApplicable:
					return "NotApplicable";
				case NonFiniteClaimStatus::Unavailable:
					return "Unavailable";
				default:
					return "Incomplete";
				}
			};
		JsonWriter writer;
		writer.BeginObject();
		writer.String("schema", "gglab.nonfinite-watch");
		writer.Unsigned("schemaVersion", 2);
		writer.Unsigned("configurationVersion", coverage.m_ConfigurationVersion);
		writer.String("mode", coverage.m_Mode == NonFiniteWatchMode::Fail ? "fail" : "report");
		writer.String("coverage", "submitted-range");
		writer.String("instrumentation", coverage.m_InstrumentationAvailable ? "available" : "unavailable");
		writer.BeginArray("claims");
		if (coverage.m_BoundaryRequested)
		{
			writer.String({}, "boundary");
		}
		if (coverage.m_ProductionRequested)
		{
			writer.String({}, "production");
		}
		writer.EndArray();
		writer.Unsigned("firstFrameSerial", coverage.m_FirstFrameSerial);
		writer.Unsigned("lastFrameSerial", coverage.m_LastFrameSerial);
		writer.Unsigned("frames", coverage.m_FrameCount);
		writer.Unsigned("completedFrames", coverage.m_CompletedFrames);
		writer.Bool("complete", coverage.m_Complete);
		writer.String("boundary", claimName(coverage.m_Boundary.m_Status));
		writer.String("production", claimName(coverage.m_Production.m_Status));
		writer.String("failure", coverage.m_Failure);
		if (coverage.m_FirstAnomaly)
		{
			writer.Unsigned("firstAnomalyFrameSerial", coverage.m_FirstAnomaly->m_FrameSerial);
			writer.String("reason", GetNonFiniteWatchFailureName(coverage.m_FirstAnomaly->m_Failure));
		}
		else
		{
			writer.Null("firstAnomalyFrameSerial");
			writer.String("reason", coverage.m_Failure.empty() ? "None" : "MissingCoverage");
		}
		// Debug verification stamp: anomalies of this range were deliberately injected.
		if (const auto& injection = coverage.m_Injection; injection && injection->m_PointIndex < nonfinite::InjectionPoints.size())
		{
			writer.BeginObject("injection");
			writer.String("point", nonfinite::InjectionPoints[injection->m_PointIndex].m_Id);
			writer.String("value", GetNonFiniteInjectionValueName(injection->m_Value));
			writer.Unsigned("x", injection->m_X);
			writer.Unsigned("y", injection->m_Y);
			writer.Unsigned("component", injection->m_Component);
			writer.Unsigned("injectedFrames", coverage.m_InjectedFrames);
			writer.Unsigned("appliedFrames", coverage.m_AppliedFrames);
			writer.EndObject();
		}
		else
		{
			writer.Null("injection");
		}
		writer.EndObject();
		return writer.Finish();
	}

	namespace
	{
		using OrderedJson = nlohmann::ordered_json;
		constexpr std::array ParameterTypeNames{ "bool", "int", "uint", "float", "enum", "vector3", "color" };

		[[nodiscard]] OrderedJson SerializeLabValue(const LabValue& input)
		{
			return std::visit([](const auto& item) -> OrderedJson
				{
					using T = std::decay_t<decltype(item)>;
					if constexpr (std::is_same_v<T, Vector3>)
					{
						return OrderedJson::array({ item.m_X, item.m_Y, item.m_Z });
					}
					else if constexpr (std::is_same_v<T, Color>)
					{
						return OrderedJson::array({ item.m_R, item.m_G, item.m_B, item.m_A });
					}
					else
					{
						return item;
					}
				}, input);
		}

		[[nodiscard]] OrderedJson SerializeHostViewStamp(const HostViewControlStamp& stamp)
		{
			return {
				{ "runIdentity", stamp.m_RunIdentity.empty() ? OrderedJson() : OrderedJson(stamp.m_RunIdentity) },
				{ "contentIdentity", stamp.m_ContentIdentity },
				{ "hostViewRevision", stamp.m_HostViewRevision } };
		}

		// The predicate keeps its requested form; unset optional fields are null.
		[[nodiscard]] OrderedJson SerializeRequiredFrameState(const RequiredFrameState& state)
		{
			OrderedJson values = OrderedJson::array();
			for (const RequiredFrameValue& value : state.m_Values)
			{
				values.push_back({
					{ "id", value.m_Id.GetName() },
					{ "type", ParameterTypeNames[static_cast<size_t>(value.m_Type)] },
					{ "value", SerializeLabValue(value.m_Value) },
					{ "source", value.m_Source ?
						OrderedJson(GetHostViewValueSourceName(*value.m_Source)) :
						OrderedJson() } });
			}
			OrderedJson consumers = OrderedJson::array();
			for (const RequiredTemporalConsumer& consumer : state.m_TemporalConsumers)
			{
				consumers.push_back({
					{ "consumer", GetTemporalConsumerName(consumer.m_Consumer) },
					{ "status", GetTemporalConsumerStatusName(consumer.m_Status) },
					{ "disableReason", consumer.m_DisableReason ?
						OrderedJson(GetTemporalConsumerDisableReasonName(*consumer.m_DisableReason)) :
						OrderedJson() } });
			}
			return {
				{ "runIdentity", state.m_RunIdentity },
				{ "contentIdentity", state.m_ContentIdentity },
				{ "minHostViewRevision", state.m_MinHostViewRevision },
				{ "values", std::move(values) },
				{ "temporalConsumers", std::move(consumers) },
				{ "referenceSampling", state.m_ReferenceSampling ?
					OrderedJson(*state.m_ReferenceSampling) :
					OrderedJson() } };
		}

		[[nodiscard]] std::string SerializeControlState(const FrameCaptureControlState& control)
		{
			OrderedJson hostView = OrderedJson::array();
			for (const HostViewProjectionEntry& entry : control.m_HostView)
			{
				hostView.push_back({
					{ "id", entry.m_Id.GetName() },
					{ "type", ParameterTypeNames[static_cast<size_t>(entry.m_Type)] },
					{ "value", SerializeLabValue(entry.m_Value) },
					{ "source", GetHostViewValueSourceName(entry.m_Source) },
					{ "inheritedSource", GetHostViewValueSourceName(entry.m_InheritedSource) },
					{ "hostOverride", entry.m_Override ?
						OrderedJson{ { "present", true }, { "value", SerializeLabValue(*entry.m_Override) } } :
						OrderedJson{ { "present", false } } } });
			}
			return OrderedJson{
				{ "schemaVersion", FrameCaptureControlSchemaVersion },
				{ "hostViewControl", SerializeHostViewStamp(control.m_HostViewControl) },
				{ "hostView", std::move(hostView) },
				{ "requiredFrameState", control.m_RequiredFrameState ?
					SerializeRequiredFrameState(*control.m_RequiredFrameState) :
					OrderedJson() } }.dump(-1, ' ', false, OrderedJson::error_handler_t::replace);
		}
	}

	std::string SerializeParameterFrameState(const ParameterFrameState& frame) noexcept
	{
		using Json = nlohmann::ordered_json;
		const auto value = [](const LabValue& input) { return SerializeLabValue(input); };
		const auto lighting = [](const ParameterFrameLighting& state)
			{
				return Json{
					{ "generation", state.m_Generation },
					{ "profileId", state.m_Profile ? Json(state.m_Profile->m_Id) : Json() },
					{ "profileHash", state.m_Profile ? Json(Sha256DigestToHex(state.m_Profile->m_Hash)) : Json() },
					{ "profileHashVersion", state.m_Profile ? Json(LightingProfileHashVersion) : Json() } };
			};
		static constexpr std::array classNames{ "Immediate", "Deferred", "Rebuild" };
		static constexpr std::array stateNames{ "Requested", "Applied", "Published", "Consumed",
			"Superseded", "NoChange", "EffectNotReached", "Failed" };
		Json values = Json::array();
		for (const auto& item : frame.m_Values)
		{
			values.push_back({
				{ "id", item.m_Id },
				{ "source", item.m_Source },
				{ "type", ParameterTypeNames[static_cast<size_t>(item.m_Type)] },
				{ "value", value(item.m_Value) },
				{ "default", value(item.m_Default) } });
		}
		Json receipts = Json::array();
		for (const auto& receipt : frame.m_Receipts)
		{
			const auto& item = receipt.m_Item;
			receipts.push_back({
				{ "batchId", receipt.m_BatchId },
				{ "id", item.m_Parameter.GetName() },
				{ "declaredClass", classNames[static_cast<size_t>(item.m_DeclaredClass)] },
				{ "state", stateNames[static_cast<size_t>(item.m_State)] },
				{ "identity", {
					{ "revision", item.m_Identity.m_Revision },
					{ "domain", item.m_Identity.m_Domain },
					{ "generation", item.m_Identity.m_Generation } } },
				{ "requested", item.m_Requested },
				{ "applied", item.m_Applied ? Json(*item.m_Applied) : Json() },
				{ "published", item.m_Published ? Json(*item.m_Published) : Json() },
				{ "firstConsumed", item.m_FirstConsumed ? Json(*item.m_FirstConsumed) : Json() },
				{ "failure", item.m_Failure } });
		}
		return Json{
			{ "schemaVersion", 1 },
			{ "frameSerial", frame.m_FrameSerial },
			{ "revision", frame.m_Revision },
			{ "contentId", frame.m_ContentId },
			{ "available", frame.m_Available },
			{ "values", std::move(values) },
			{ "lighting", { { "requested", lighting(frame.m_RequestedLighting) },
				{ "active", lighting(frame.m_ActiveLighting) } } },
			{ "receipts", std::move(receipts) } }.dump(-1, ' ', false, Json::error_handler_t::replace);
	}

	namespace
	{
		nlohmann::json GIIdentityJson(const GIFieldIdentity& field)
		{
			return {
				{ "generation", field.m_Generation },
				{ "sceneRevision", field.m_SceneRevision },
				{ "lightingGeneration", field.m_LightingGeneration },
				{ "environmentGeneration", field.m_EnvironmentGeneration },
				{ "refreshEpoch", field.m_RefreshEpoch },
				{ "epochOriginUpdate", field.m_EpochOriginUpdate } };
		}
		nlohmann::json GIStampJson(const GIObservationStamp& stamp)
		{
			return {
				{ "field", GIIdentityJson(stamp.m_Field) },
				{ "kind", stamp.m_Kind == GIObservationKind::ReceiverContribution ? "receiver-contribution" :
					stamp.m_Kind == GIObservationKind::ProbeAtlas ? "probe-atlas" :
					stamp.m_Kind == GIObservationKind::ProbePicking ? "probe-picking" :
					stamp.m_Kind == GIObservationKind::ProbeVisualization ? "probe-visualization" : "completed-readiness" },
				{ "producingFrame", stamp.m_ProducingFrame },
				{ "fieldUpdate", stamp.m_FieldUpdate },
				{ "readinessUpdate", stamp.m_ReadinessUpdate } };
		}
		nlohmann::json GIReceiverViewJson(const GIReceiverViewSnapshot& view)
		{
			return {
				{ "revision", view.m_Revision },
				{ "viewId", view.m_ViewId },
				{ "renderExtent", view.m_RenderExtent },
				{ "displayExtent", view.m_DisplayExtent },
				{ "cameraPosition", view.m_CameraPosition },
				{ "view", view.m_View },
				{ "projection", view.m_Projection },
				{ "viewProjection", view.m_ViewProjection },
				{ "matrixConvention", "row-major-row-vector" },
				{ "clipDepthRange", { 0, 1 } },
				{ "pixelConvention", "top-left-integer-center" },
				{ "jitterPixels", view.m_JitterPixels },
				{ "nearFar", view.m_NearFar },
				{ "reversedDepth", view.m_ReversedDepth },
				{ "receiversAvailable", view.m_ReceiversAvailable },
				{ "temporalAA", view.m_TemporalAA },
				{ "supported", view.m_Supported },
				{ "receiverAttachmentLogicalBytes", view.m_ReceiverAttachmentBytes },
				{ "lookupDiagnosticLogicalBytes", view.m_LookupDiagnosticBytes } };
		}
	}

	namespace
	{
		nlohmann::json GIProbeSelectionJson(const std::optional<GIProbeSelection>& selection)
		{
			return selection ?
				nlohmann::json{
					{ "coordinate", selection->m_Coordinate },
					{ "expectedGeneration", selection->m_ExpectedGeneration } } :
				nlohmann::json(nullptr);
		}

		nlohmann::json GIProbeVisualizationSettingsJson(const GIProbeVisualizationSettings& settings)
		{
			return {
				{ "enabled", settings.m_Enabled },
				{ "bounds", settings.m_Bounds },
				{ "radiusInSpacings", settings.m_Radius },
				{ "budget", settings.m_Budget },
				{ "sliceAxis", settings.m_SliceAxis },
				{ "layerMinimum", settings.m_LayerMinimum },
				{ "layerMaximum", settings.m_LayerMaximum },
				{ "classificationMask", settings.m_ClassificationMask },
				{ "maximumDistance", settings.m_MaximumDistance },
				{ "mode", GetGIProbeVisualizationModeName(settings.m_Mode) },
				{ "previewEV", settings.m_PreviewExposureEV },
				{ "changeNormalized", settings.m_ChangeNormalized },
				{ "changeRange", settings.m_ChangeRange },
				{ "changeBlocks", settings.m_ChangeBlocks },
				{ "depthMode", GetGIProbeDepthModeName(settings.m_DepthMode) },
				{ "selection", GIProbeSelectionJson(settings.m_Selection) } };
		}
		nlohmann::json GIProbeVisualizationViewJson(const GIProbeVisualizationViewSnapshot& view)
		{
			const uint32_t phases = view.m_TemporalAA ? GetTemporalAAJitterSequenceLength(view.m_ResolutionPreset) : 1u;
			return {
				{ "revision", view.m_Revision },
				{ "viewId", view.m_ViewId },
				{ "renderExtent", view.m_RenderExtent },
				{ "displayExtent", view.m_DisplayExtent },
				{ "cameraPosition", view.m_CameraPosition },
				{ "view", view.m_View },
				{ "projection", view.m_Projection },
				{ "viewProjection", view.m_ViewProjection },
				{ "unjitteredProjection", view.m_UnjitteredProjection },
				{ "unjitteredViewProjection", view.m_UnjitteredViewProjection },
				{ "matrixConvention", "row-major-row-vector" },
				{ "clipDepthRange", { 0, 1 } },
				{ "pixelConvention", "top-left-integer-center" },
				{ "jitterPixels", view.m_JitterPixels },
				{ "nearFar", view.m_NearFar },
				{ "reversedDepth", view.m_ReversedDepth },
				{ "temporalAA", view.m_TemporalAA },
				{ "resolutionPreset", GetTemporalAAResolutionPresetName(view.m_ResolutionPreset) },
				{ "postTemporalProjection", view.m_PostTemporalJittered ? "jittered" : "unjittered" },
				{ "temporalFrameIndex", view.m_TemporalFrameIndex },
				{ "jitterPhase", view.m_TemporalFrameIndex % phases },
				{ "jitterSequenceLength", phases },
				{ "depthAlignment", GetGIProbeVisualizationDepthAlignmentName(view) },
				{ "supported", IsGIProbeVisualizationViewSupported(view) } };
		}
		nlohmann::json GIChangeObservationJson(const std::shared_ptr<const GIFieldChangeObservation>& observation)
		{
			using Json = nlohmann::json;
			if (!observation)
			{
				return nullptr;
			}
			const auto& data = *observation->m_Measurement;
			const auto& readiness = data.m_Readiness;
			Json blocks = Json::array();
			for (const auto& block : data.m_Blocks)
			{
				blocks.push_back({
					{ "activeProbes", block.m_ActiveProbes },
					{ "excluded", block.m_ActiveProbes == 0 },
					{ "p95", block.m_Percentile95 ? Json(*block.m_Percentile95) : Json(nullptr) },
					{ "maximum", block.m_Maximum ? Json(*block.m_Maximum) : Json(nullptr) },
					{ "quietUpdates", block.m_QuietUpdates },
					{ "blockers", block.m_Blockers },
					{ "warmingMet", block.m_WarmingMet },
					{ "currentQuiet", block.m_CurrentQuiet } });
			}
			return {
				{ "schemaVersion", 1 },
				{ "observation", GIStampJson(observation->m_Observation) },
				{ "counts", data.m_Counts },
				{ "probeCount", data.m_Probes.size() },
				{ "blockWidth", data.m_BlockWidth },
				{ "blockOrder", "x + Bx * (z + Bz * y)" },
				{ "state", GetGIFieldReadinessName(readiness.m_State) },
				{ "phase", data.m_RefreshMode ? "refresh" : "steady" },
				{ "includesDistance", data.m_IncludesDistance },
				{ "hysteresis", readiness.m_Hysteresis },
				{ "noiseSigma", readiness.m_NoiseSigma },
				{ "irradianceFloor", readiness.m_IrradianceFloor },
				{ "threshold", readiness.m_Threshold },
				{ "maximumThreshold", 3 * readiness.m_Threshold },
				{ "warmingUpdates", data.m_WarmingUpdates },
				{ "requiredQuietUpdates", data.m_RequiredQuietUpdates },
				{ "blockerBits", { { "warming", 1 }, { "p95", 2 }, { "maximum", 4 }, { "quietHold", 8 } } },
				{ "blocks", std::move(blocks) } };
		}

		nlohmann::json GIProbeVisualizationJson(const GIProbeVisualizationSnapshot& snapshot)
		{
			using Json = nlohmann::json;
			const auto& counts = snapshot.m_CompletedCounts;
			return {
				{ "schemaVersion", 7 },
				{ "settingsRevision", snapshot.m_SettingsRevision },
				{ "pickingAvailable", snapshot.m_PickingAvailable },
				{ "changeObservation", GIChangeObservationJson(snapshot.m_ChangeObservation) },
				{ "changeResource", "GI.Probes.Change" },
				{ "changeUploadLogicalBytes", snapshot.m_ChangeUploadBytes },
				{ "changeBlockOutlineCount", snapshot.m_Available && snapshot.m_HighlightAvailable &&
					snapshot.m_Settings.m_Mode == GIProbeVisualizationMode::Change && snapshot.m_Settings.m_ChangeBlocks &&
					snapshot.m_ChangeObservation ? snapshot.m_ChangeObservation->m_Measurement->m_Blocks.size() : 0 },
				{ "changeMeaning", "fence-completed-estimator; raw-relative-change or change/tau; independent of live atlas" },
				{ "changePalette", "saturate((2t,2-2*abs(2t-1),2-2t)); t=clamp(value/range,0,1); over-range saturated" },
				{ "mode", GetGIProbeVisualizationModeName(snapshot.m_Settings.m_Mode) },
				{ "irradianceQuantity", "white-Lambertian-E-over-pi" },
				{ "irradianceMapping", "atlas-M=E/pi; exp2(previewEV); ACES-fitted; sRGB" },
				{ "irradianceOutline", "neutral-gray-valid-probe-silhouette" },
				{ "depthMode", GetGIProbeDepthModeName(snapshot.m_Settings.m_DepthMode) },
				{ "probeDepth", "private-strict-D32; nearest-then-lowest-stable-ID" },
				{ "depthSubnormalConversion", "positive-subnormal-to-zero" },
				{ "sceneDepthSource", snapshot.m_Available && snapshot.m_Settings.m_DepthMode == GIProbeDepthMode::SceneDepth ?
					Json(GetGIProbeVisualizationDepthSourceName(snapshot.m_View)) : Json(nullptr) },
				{ "sceneDepthSampling", "display-integer-pixel-load; equal-visible; no-bias; invalid-rejected" },
				{ "sceneDepthAdditionalLogicalBytes", 0 },
				{ "view", GIProbeVisualizationViewJson(snapshot.m_View) },
				{ "filterOrderVersion", GIProbeVisualizationFilterVersion },
				{ "filterOrder", { "stable-id", "slice-inclusive", "classification", "center-distance-inclusive", "sphere-frustum", "first-N" } },
				{ "settings", GIProbeVisualizationSettingsJson(snapshot.m_Settings) },
				{ "available", snapshot.m_Available },
				{ "viewSupported", snapshot.m_ViewSupported },
				{ "readbackFailed", snapshot.m_ReadbackFailed },
				{ "privateDepthLogicalBytes", snapshot.m_PrivateDepthLogicalBytes },
				{ "selectionLogicalBytes", snapshot.m_SelectionLogicalBytes },
				{ "readbackBufferBytes", snapshot.m_ReadbackBufferBytes },
				{ "readbackAllocatedBytes", snapshot.m_ReadbackAllocatedBytes ?
					Json(*snapshot.m_ReadbackAllocatedBytes) : Json(nullptr) },
				{ "observation", snapshot.m_Observation ? GIStampJson(*snapshot.m_Observation) : Json(nullptr) },
				{ "countsResource", "GI.Probes.Counts" },
				{ "orderedIDsResource", "GI.Probes.RenderedIDs" },
				{ "idLayout", "R32Uint[2,budget]: row0=stable-id, row1=classification; unused=(UINT32_MAX,0)" },
				{ "highlightCount", snapshot.m_HighlightCount },
				{ "highlight", {
					{ "state", GetGIProbeHighlightStateName(snapshot.m_HighlightState) },
					{ "pipelineAvailable", snapshot.m_HighlightAvailable },
					{ "maximumCount", GIProbeVisualizationMaximumHighlights },
					{ "countMeaning", "submitted-diagnostic-probes; not visible-pixels" },
					{ "appearance", "yellow-great-circles; radius-scale=1.12" },
					{ "depth", "scene-depth-mode applies; ordinary private depth neither tested nor written" } } },
				{ "completed", snapshot.m_CompletedObservation ? Json{
					{ "observation", GIStampJson(*snapshot.m_CompletedObservation) },
					{ "view", GIProbeVisualizationViewJson(snapshot.m_CompletedView) },
					{ "settings", GIProbeVisualizationSettingsJson(snapshot.m_CompletedSettings) },
					{ "counts", {
						{ "enumerated", counts.m_Enumerated },
						{ "afterSlice", counts.m_AfterSlice },
						{ "afterClassification", counts.m_AfterClassification },
						{ "afterDistance", counts.m_AfterDistance },
						{ "afterFrustum", counts.m_AfterFrustum },
						{ "rendered", counts.m_Rendered },
						{ "budgetDropped", counts.m_BudgetDropped },
						{ "highlights", counts.m_Highlights } } } } : Json(nullptr) } };
		}
	}

	std::string SerializeGIProbePickingSource(const GIFieldSummary& summary, float dpiScale,
		std::string_view contentId) noexcept
	{
		const auto& display = summary.m_Visualization;
		return nlohmann::json{
			{ "schemaVersion", 1 },
			{ "sourceFrame", display.m_Observation ? display.m_Observation->m_ProducingFrame : 0 },
			{ "contentId", contentId },
			{ "field", GIIdentityJson(summary.m_Field) },
			{ "view", GIProbeVisualizationViewJson(display.m_View) },
			{ "settings", GIProbeVisualizationSettingsJson(GIProbePickSettings(display.m_Settings)) },
			{ "settingsRevision", display.m_SettingsRevision },
			{ "mapping", {
				{ "guiOrigin", { 0, 0 } },
				{ "guiExtent", display.m_View.m_DisplayExtent },
				{ "framebufferScale", { 1, 1 } },
				{ "dpiScale", dpiScale } } } }.dump();
	}

	std::string SerializeGIProbePicking(const GIProbePickingResult& result) noexcept
	{
		using Json = nlohmann::json;
		const auto& request = result.m_Request;
		return Json{
			{ "schemaVersion", 1 },
			{ "token", request.m_Token },
			{ "pixel", request.m_Pixel },
			{ "sourceFrame", request.m_SourceFrame },
			{ "sourceField", GIIdentityJson(request.m_Field) },
			{ "sourceView", GIProbeVisualizationViewJson(request.m_View) },
			{ "settingsRevision", request.m_SettingsRevision },
			{ "settings", GIProbeVisualizationSettingsJson(request.m_Settings) },
			{ "mapping", {
				{ "guiOrigin", request.m_Mapping.m_GuiOrigin },
				{ "guiExtent", request.m_Mapping.m_GuiExtent },
				{ "framebufferScale", request.m_Mapping.m_FramebufferScale },
				{ "dpiScale", request.m_Mapping.m_DpiScale } } },
			{ "observation", GIStampJson(result.m_Observation) },
			{ "executionView", GIProbeVisualizationViewJson(result.m_ExecutionView) },
			{ "validity", result.m_Validity == GIProbePickingValidity::Hit ? "Hit" :
				result.m_Validity == GIProbePickingValidity::NoHit ? "NoHit" : "MalformedPayload" },
			{ "probeId", result.m_ProbeId ? Json(*result.m_ProbeId) : Json(nullptr) },
			{ "coordinate", result.m_Coordinate ? Json(*result.m_Coordinate) : Json(nullptr) },
			{ "depth", result.m_Depth ? Json(*result.m_Depth) : Json(nullptr) },
			{ "idEncoding", "0=NoHit; stable-id+1=Hit" },
			{ "attachmentExtent", { 1, 1 } },
			{ "attachmentLogicalBytes", 8 },
			{ "maximumSubmittedVertices", uint64_t(request.m_Settings.m_Budget) * 768 } }.dump();
	}

	std::string SerializeGIFieldSummary(const GIFieldSummary& summary) noexcept
	{
		nlohmann::json profile = nullptr;
		if (summary.m_Profile)
		{
			const auto& value = *summary.m_Profile;
			profile = {
				{ "schemaVersion", GIFieldProfileReceipt::SchemaVersion },
				{ "id", value.m_Volume.m_Profile.m_Id },
				{ "revision", value.m_Volume.m_Profile.m_Revision },
				{ "counts", value.m_Volume.m_Counts },
				{ "minimum", value.m_Volume.m_Minimum },
				{ "spacing", value.m_Volume.m_Spacing },
				{ "irradianceInteriorWidth", value.m_IrradianceInteriorWidth },
				{ "raysPerProbe", value.m_RaysPerProbe },
				{ "seed", value.m_Seed },
				{ "maximumDistance", value.m_MaximumDistance },
				{ "hysteresis", value.m_Hysteresis },
				{ "viewBias", value.m_ViewBias },
				{ "receiverSampling", value.m_EnableReceiverSampling },
				{ "feedback", value.m_EnableFeedback },
				{ "progressiveSampling", value.m_ProgressiveSampling },
				{ "constantEnvironment", value.m_ConstantEnvironment },
				{ "constantRadiance", value.m_ConstantRadiance },
				{ "readiness", {
					{ "enabled", value.m_Readiness.m_Enabled },
					{ "noiseSigma", value.m_Readiness.m_NoiseSigma },
					{ "steadyNoiseSigma", value.m_Readiness.m_SteadyNoiseSigma },
					{ "refreshHysteresis", value.m_Readiness.m_RefreshHysteresis },
					{ "progressiveRefresh", value.m_Readiness.m_ProgressiveRefresh } } } };
		}
		return nlohmann::json{
			{ "schemaVersion", 1 },
			{ "field", GIIdentityJson(summary.m_Field) },
			{ "profile", std::move(profile) },
			{ "counts", summary.m_Counts },
			{ "minimum", summary.m_Minimum },
			{ "spacing", summary.m_Spacing },
			{ "positionUnits", "world-units" },
			{ "irradianceInteriorWidth", summary.m_IrradianceInteriorWidth },
			{ "submittedUpdates", summary.m_SubmittedUpdates },
			{ "initialized", summary.m_Initialized },
			{ "invalid", summary.m_Invalid },
			{ "inspectionAvailable", summary.m_InspectionAvailable },
			{ "receiverInspectionAvailable", summary.m_ReceiverInspectionAvailable },
			{ "receiverMarkersAvailable", summary.m_ReceiverMarkersAvailable },
			{ "classificationCounts", nullptr },
			{ "changeObservation", GIChangeObservationJson(summary.m_ChangeObservation) },
			{ "changeState", summary.m_Invalid ? "failed-or-unknown-completion" :
				!summary.m_Readiness.m_Enabled ? "disabled" : summary.m_ChangeObservation ? "completed" : "pending" },
			{ "probeVisualization", GIProbeVisualizationJson(summary.m_Visualization) },
			{ "receiverView", GIReceiverViewJson(summary.m_ReceiverView) },
			{ "completedMeasurement", summary.m_CompletedMeasurement ?
				GIStampJson(*summary.m_CompletedMeasurement) : nlohmann::json(nullptr) } }.dump();
	}

	std::string SerializeGIProbeInspection(const GIProbeInspectionResult& result) noexcept
	{
		auto value = nlohmann::json{
			{ "schemaVersion", 1 },
			{ "request", {
				{ "coordinate", result.m_Request.m_Coordinate },
				{ "expectedGeneration", result.m_Request.m_ExpectedGeneration },
				{ "expectedRefreshEpoch", result.m_Request.m_ExpectedRefreshEpoch ?
					nlohmann::json(*result.m_Request.m_ExpectedRefreshEpoch) : nlohmann::json(nullptr) } } },
			{ "observation", GIStampJson(result.m_Observation) },
			{ "worldPosition", result.m_WorldPosition },
			{ "positionUnits", "world-units" },
			{ "classification", result.m_Classification },
			{ "validity", GetGIProbeInspectionValidityName(result.m_Validity) },
			{ "tileLayout", "row-major-bordered-octahedral" },
			{ "irradianceUnits", "scene-referred-irradiance-E" },
			{ "distanceUnits", { "world-units", "world-units-squared" } },
			{ "irradianceTileWidth", result.m_IrradianceTileWidth },
			{ "distanceTileWidth", result.m_DistanceTileWidth },
			{ "irradiance", result.m_Irradiance },
			{ "distance", result.m_Distance } };
		if (result.m_Request.m_IntegrationReplayUpdate)
		{
			value["schemaVersion"] = 2;
			value["request"]["integrationReplayUpdate"] = *result.m_Request.m_IntegrationReplayUpdate;
		}
		return value.dump();
	}

	std::string SerializeGIReceiverInspection(const GIReceiverInspectionResult& result) noexcept
	{
		using Json = nlohmann::json;
		Json corners = Json::array();
		if (result.m_CageEvaluated)
		{
			for (const auto& corner : result.m_Corners)
			{
				const bool sampled = corner.m_SamplingEvaluated == 1;
				corners.push_back({
					{ "coordinate", corner.m_Coordinate },
					{ "classification", corner.m_Classification },
					{ "worldPosition", corner.m_WorldPosition },
					{ "reason", GetGIReceiverCornerReasonName(corner.m_Reason) },
					{ "samplingEvaluated", sampled },
					{ "trilinearFactors", corner.m_TrilinearFactors },
					{ "trilinearWeight", corner.m_TrilinearWeight },
					{ "backfaceFactor", sampled ? Json(corner.m_BackfaceFactor) : Json(nullptr) },
					{ "atlasM", sampled ? Json(corner.m_AtlasM) : Json(nullptr) },
					{ "meanDistance", sampled ? Json(corner.m_MeanDistance) : Json(nullptr) },
					{ "variance", sampled ? Json(corner.m_Variance) : Json(nullptr) },
					{ "usedVariance", sampled ? Json(corner.m_UsedVariance) : Json(nullptr) },
					{ "receiverDistance", sampled ? Json(corner.m_ReceiverDistance) : Json(nullptr) },
					{ "visibility", sampled ? Json(corner.m_Visibility) : Json(nullptr) },
					{ "preVisibilityWeight", sampled ? Json(corner.m_PreVisibilityWeight) : Json(nullptr) },
					{ "preCrushWeight", sampled ? Json(corner.m_PreCrushWeight) : Json(nullptr) },
					{ "rawWeight", corner.m_RawWeight },
					{ "normalizedWeight", corner.m_NormalizationAvailable == 1 ? Json(corner.m_NormalizedWeight) : Json(nullptr) },
					{ "contributionE", corner.m_NormalizationAvailable == 1 ? Json(corner.m_ContributionE) : Json(nullptr) } });
			}
		}
		const bool receiver = result.m_Validity != GIReceiverInspectionValidity::Background &&
			result.m_Validity != GIReceiverInspectionValidity::MalformedPayload;
		return Json{
			{ "schemaVersion", 1 },
			{ "request", {
				{ "pixel", result.m_Request.m_Pixel },
				{ "expectedGeneration", result.m_Request.m_ExpectedGeneration },
				{ "expectedViewRevision", result.m_Request.m_ExpectedViewRevision },
				{ "expectedViewId", result.m_Request.m_ExpectedViewId },
				{ "expectedRefreshEpoch", result.m_Request.m_ExpectedRefreshEpoch ?
					Json(*result.m_Request.m_ExpectedRefreshEpoch) : Json(nullptr) },
				{ "markers", result.m_Request.m_Markers } } },
			{ "observation", GIStampJson(result.m_Observation) },
			{ "view", GIReceiverViewJson(result.m_View) },
			{ "validity", GetGIReceiverInspectionValidityName(result.m_Validity) },
			{ "cageEvaluated", result.m_CageEvaluated },
			{ "support", result.m_Support },
			{ "compatible", result.m_Compatible },
			{ "position", receiver ? Json(result.m_Position) : Json(nullptr) },
			{ "biasedPosition", result.m_CageEvaluated ? Json(result.m_BiasedPosition) : Json(nullptr) },
			{ "vertexNormal", receiver ? Json(result.m_VertexNormal) : Json(nullptr) },
			{ "shadingNormal", receiver ? Json(result.m_ShadingNormal) : Json(nullptr) },
			{ "viewDirection", receiver ? Json(result.m_ViewDirection) : Json(nullptr) },
			{ "coverage", receiver ? Json(result.m_Coverage) : Json(nullptr) },
			{ "totalWeight", receiver ? Json(result.m_TotalWeight) : Json(nullptr) },
			{ "normalizationReason", result.m_TotalWeight > 0 ? Json(nullptr) : Json("NoPositiveTotalWeight") },
			{ "irradianceE", receiver ? Json(result.m_IrradianceE) : Json(nullptr) },
			{ "ordinaryIrradianceE", receiver ? Json(result.m_OrdinaryIrradianceE) : Json(nullptr) },
			{ "ordinaryWeight", receiver ? Json(result.m_OrdinaryWeight) : Json(nullptr) },
			{ "fieldProfile", {
				{ "counts", result.m_Counts },
				{ "spacing", result.m_Spacing },
				{ "irradianceInteriorWidth", result.m_IrradianceInteriorWidth },
				{ "normalBias", 0.1f },
				{ "viewBias", result.m_ViewBias } } },
			{ "units", {
				{ "position", "world-units" },
				{ "distance", "world-units" },
				{ "variance", "world-units-squared" },
				{ "irradiance", "scene-referred-irradiance-E" },
				{ "weights", "dimensionless" } } },
			{ "payloadBytes", GIReceiverInspectionBytes },
			{ "markerMode", result.m_Request.m_Markers ? "gpu-xray" : "off" },
			{ "corners", std::move(corners) } }.dump();
	}

	std::string SerializeFrameCaptureMetadata(const FrameCaptureMetadata& metadata) noexcept
	{
		JsonWriter writer;
		writer.BeginObject();
		writer.Unsigned("schemaVersion", FrameCaptureMetadataSchemaVersion);
		if (metadata.m_GIProbePickingRequest && metadata.m_GIProbePickingObservation && metadata.m_GIProbePickingView)
		{
			GIProbePickingResult observation{ .m_Request = *metadata.m_GIProbePickingRequest,
				.m_Observation = *metadata.m_GIProbePickingObservation,
				.m_ExecutionView = *metadata.m_GIProbePickingView };
			auto document = nlohmann::json::parse(SerializeGIProbePicking(observation));
			document.erase("validity");
			document.erase("probeId");
			document.erase("coordinate");
			document.erase("depth");
			document["executionDpiScale"] = metadata.m_GIProbePickingDpiScale;
			writer.Json("giProbePickingObservation", document.dump());
		}
		if (metadata.m_GIProbePicking)
		{
			writer.Json("giProbePicking", SerializeGIProbePicking(*metadata.m_GIProbePicking));
		}
		if (metadata.m_GIReceiverObservation && metadata.m_GIReceiverView)
		{
			writer.Json("giReceiverObservation", nlohmann::json{
				{ "stamp", GIStampJson(*metadata.m_GIReceiverObservation) },
				{ "view", GIReceiverViewJson(*metadata.m_GIReceiverView) } }.dump());
		}
		if (metadata.m_GIReceiverInspection)
		{
			writer.Json("giReceiverInspection", SerializeGIReceiverInspection(*metadata.m_GIReceiverInspection));
		}
		if (metadata.m_GIProbeObservation)
		{
			writer.Json("giProbeObservation", GIStampJson(*metadata.m_GIProbeObservation).dump());
		}
		if (metadata.m_GIProbeInspection)
		{
			writer.Json("giProbeInspection", SerializeGIProbeInspection(*metadata.m_GIProbeInspection));
		}
		writer.Unsigned("requestId", metadata.m_RequestId);
		if (metadata.m_Parameters)
		{
			writer.Json("parameters", SerializeParameterFrameState(*metadata.m_Parameters));
		}
		else
		{
			writer.Null("parameters");
		}
		writer.String("label", metadata.m_Label);
		writer.String("note", metadata.m_Note);
		writer.String("source", GetFrameCaptureSourceName(metadata.m_Source));
		writer.BeginObject("observation");
		writer.Bool("graph", metadata.m_GraphExportActive);
		writer.Bool("resourceExport", metadata.m_ResourceExportActive);
		writer.Bool("nonFiniteWatch", metadata.m_NonFiniteScanActive);
		writer.EndObject();
		if (metadata.m_WatchCoverage)
		{
			writer.Json("watch", SerializeNonFiniteWatchCoverage(*metadata.m_WatchCoverage));
		}
		if (metadata.m_DiagnosticTap.empty())
		{
			writer.Null("diagnosticTap");
		}
		else
		{
			writer.String("diagnosticTap", metadata.m_DiagnosticTap);
		}

		writer.BeginObject("timing");
		writer.String("mode", GetFrameCaptureTimingName(metadata.m_Timing));
		writer.Unsigned("settleFrames", metadata.m_SettleFrames);
		writer.Unsigned("settledFrames", metadata.m_SettledFrames);
		writer.EndObject();

		writer.String("backend", metadata.m_Backend);
		writer.BeginObject("content");
		writer.String("demoId", metadata.m_DemoId);
		writer.String("labId", metadata.m_LabId);
		if (metadata.m_AssetBundle)
		{
			writer.BeginObject("assetBundle");
			writer.String("id", metadata.m_AssetBundle->m_Id);
			writer.String("revision", metadata.m_AssetBundle->m_Revision);
			writer.EndObject();
		}
		else
		{
			writer.Null("assetBundle");
		}
		writer.EndObject();
		if (metadata.m_SceneInputs)
		{
			WriteSceneInputs(writer, *metadata.m_SceneInputs);
		}
		else
		{
			writer.Null("sceneInputs");
		}

		writer.BeginObject("frame");
		writer.Unsigned("serial", metadata.m_FrameSerial);
		writer.Unsigned("index", metadata.m_FrameIndex);
		writer.EndObject();

		writer.BeginObject("image");
		writer.String("file", metadata.m_ImageFile);
		writer.Unsigned("width", metadata.m_Width);
		writer.Unsigned("height", metadata.m_Height);
		writer.String("displayFormat", metadata.m_DisplayFormat);
		writer.String("encoding", "png-srgb-rgb8");
		writer.EndObject();

		writer.BeginObject("camera");
		writer.String("name", metadata.m_Camera.m_Name);
		writer.String("referenceView", metadata.m_Camera.m_ReferenceViewId);
		writer.Vector("position", metadata.m_Camera.m_Position);
		writer.Vector("forward", metadata.m_Camera.m_Forward);
		writer.Vector("up", metadata.m_Camera.m_Up);
		writer.Number("verticalFovDegrees", metadata.m_Camera.m_VerticalFovDegrees);
		writer.Number("nearPlane", metadata.m_Camera.m_NearPlane);
		writer.Number("farPlane", metadata.m_Camera.m_FarPlane);
		writer.EndObject();

		writer.BeginObject("time");
		if (metadata.m_FixedDeltaTime)
		{
			writer.Number("fixedDeltaTime", *metadata.m_FixedDeltaTime);
		}
		else
		{
			writer.Null("fixedDeltaTime");
		}
		writer.Number("totalTime", metadata.m_TotalTime);
		writer.EndObject();

		writer.Bool("developmentTools", metadata.m_DevelopmentTools);

		writer.BeginObject("readiness");
		writer.Bool("ready", metadata.m_Readiness.IsReady());
		writer.BeginArray("gates");
		for (const FrameCaptureGate& gate : metadata.m_Readiness.m_Gates)
		{
			writer.BeginObject();
			writer.String("name", gate.m_Name);
			writer.String("state", GetFrameCaptureGateStateName(gate.m_State));
			writer.String("detail", gate.m_Detail);
			writer.EndObject();
		}
		writer.EndArray();

		if (metadata.m_Readiness.m_GIField)
		{
			const auto& field = *metadata.m_Readiness.m_GIField;
			const auto& readiness = field.m_Readiness;
			writer.BeginObject("giField");
			writer.Unsigned("schemaVersion", 1);
			writer.Unsigned("generation", field.m_Generation);
			writer.Unsigned("sceneRevision", field.m_SceneRevision);
			writer.Unsigned("lightingGeneration", field.m_LightingGeneration);
			writer.Unsigned("environmentGeneration", field.m_EnvironmentGeneration);
			if (field.m_Summary)
			{
				writer.Json("summary", SerializeGIFieldSummary(*field.m_Summary));
			}
			writer.Bool("enabled", readiness.m_Enabled);
			writer.String("state", GetGIFieldReadinessName(readiness.m_State));
			writer.Bool("refreshing", readiness.m_Refreshing);
			writer.Bool("failed", readiness.m_Failed);
			writer.Unsigned("completedUpdates", readiness.m_CompletedUpdates);
			writer.Unsigned("readyUpdate", readiness.m_ReadyUpdate);
			writer.Unsigned("activeBlocks", readiness.m_ActiveBlocks);
			writer.Unsigned("excludedBlocks", readiness.m_ExcludedBlocks);
			writer.Unsigned("quietBlocks", readiness.m_QuietBlocks);
			writer.Number("maximumChange", readiness.m_MaximumChange);
			writer.Number("threshold", readiness.m_Threshold);
			writer.Number("noiseSigma", readiness.m_NoiseSigma);
			writer.Number("refreshNoiseEstimate", readiness.m_RefreshNoiseEstimate);
			writer.Number("steadyNoiseEstimate", readiness.m_SteadyNoiseEstimate);
			writer.Unsigned("refreshNoiseSamples", readiness.m_RefreshNoiseSamples);
			writer.Unsigned("steadyNoiseSamples", readiness.m_SteadyNoiseSamples);
			writer.Number("irradianceFloor", readiness.m_IrradianceFloor);
			writer.Number("hysteresis", readiness.m_Hysteresis);
			writer.EndObject();
		}

		writer.EndObject();

		const FrameCaptureTemporalState& temporal = metadata.m_Temporal;
		writer.BeginObject("temporal");
		writer.Bool("requested", temporal.m_Requested);
		writer.String("status", temporal.m_Status);
		writer.String("disableReason", temporal.m_DisableReason);
		writer.BeginArray("consumers");
		for (const FrameCaptureTemporalConsumer& consumer : temporal.m_Consumers)
		{
			writer.BeginObject();
			writer.String("name", consumer.m_Name);
			writer.Bool("requested", consumer.m_Requested);
			writer.String("status", consumer.m_Status);
			writer.String("disableReason", consumer.m_DisableReason);
			writer.BeginArray("services");
			for (const std::string& service : consumer.m_Services)
			{
				writer.String({}, service);
			}
			writer.EndArray();
			writer.EndObject();
		}
		writer.EndArray();
		writer.BeginArray("services");
		for (const std::string& service : temporal.m_Services)
		{
			writer.String({}, service);
		}
		writer.EndArray();
		writer.Unsigned("sessionIdentity", temporal.m_SessionIdentity);
		writer.Unsigned("resetIdentity", temporal.m_ResetIdentity);
		writer.Unsigned("jitterIndex", temporal.m_JitterIndex);
		writer.Unsigned("jitterSequenceLength", temporal.m_JitterSequenceLength);
		writer.BeginArray("jitterPixels");
		writer.Number({}, temporal.m_JitterPixels[0]);
		writer.Number({}, temporal.m_JitterPixels[1]);
		writer.EndArray();
		writer.BeginObject("settings");
		writer.Number("maxHistoryFeedback", temporal.m_MaxHistoryFeedback);
		writer.Number("depthAbsoluteThreshold", temporal.m_DepthAbsoluteThreshold);
		writer.Number("depthRelativeThreshold", temporal.m_DepthRelativeThreshold);
		writer.Number("velocityWeightScale", temporal.m_VelocityWeightScale);
		writer.Number("luminanceWeightScale", temporal.m_LuminanceWeightScale);
		writer.Number("neighborhoodClampExpansion", temporal.m_NeighborhoodClampExpansion);
		writer.Number("historyRelaxation", temporal.m_HistoryRelaxation);
		writer.String("historyAccumulation", temporal.m_HistoryAccumulation);
		writer.String("historyRectification", temporal.m_HistoryRectification);
		writer.Number("varianceClipGamma", temporal.m_VarianceClipGamma);
		writer.String("historyFilter", temporal.m_HistoryFilter);
		writer.String("currentFilter", temporal.m_CurrentFilter);
		writer.String("motionSelection", temporal.m_MotionSelection);
		writer.String("postTemporalView", temporal.m_PostTemporalView);
		writer.String("resolutionPreset", temporal.m_ResolutionPreset);
		writer.Number("textureLodBiasOffset", temporal.m_TextureLodBiasOffset);
		writer.EndObject();
		writer.BeginObject("gtao");
		writer.Bool("enabled", temporal.m_GTAO.m_Enabled);
		writer.Number("radius", temporal.m_GTAO.m_Radius);
		writer.Number("falloffStart", temporal.m_GTAO.m_FalloffStart);
		writer.Number("falloffEnd", temporal.m_GTAO.m_FalloffEnd);
		writer.Unsigned("directionCount", temporal.m_GTAO.m_DirectionCount);
		writer.Unsigned("stepCount", temporal.m_GTAO.m_StepCount);
		writer.Unsigned("denoiseRadius", temporal.m_GTAO.m_DenoiseRadius);
		writer.Bool("temporalAccumulation", temporal.m_GTAO.m_TemporalAccumulation);
		writer.Unsigned("temporalMaxSamples", temporal.m_GTAO.m_TemporalMaxSamples);
		writer.EndObject();
		writer.Number("textureLodBias", temporal.m_TextureLodBias);
		writer.BeginArray("renderExtent");
		writer.Unsigned({}, temporal.m_RenderExtent[0]);
		writer.Unsigned({}, temporal.m_RenderExtent[1]);
		writer.EndArray();
		writer.BeginArray("displayExtent");
		writer.Unsigned({}, temporal.m_DisplayExtent[0]);
		writer.Unsigned({}, temporal.m_DisplayExtent[1]);
		writer.EndArray();
		writer.Number("renderScale", temporal.m_RenderScale);
		writer.EndObject();

		if (metadata.m_Control)
		{
			writer.Json("control", SerializeControlState(*metadata.m_Control));
		}
		else
		{
			writer.Null("control");
		}

		if (metadata.m_Sequence)
		{
			writer.BeginObject("sequence");
			writer.Unsigned("id", metadata.m_Sequence->m_SequenceId);
			writer.String("cameraPath", metadata.m_Sequence->m_CameraPathId);
			writer.Unsigned("cameraPathVersion", metadata.m_Sequence->m_CameraPathVersion);
			writer.Unsigned("frame", metadata.m_Sequence->m_Frame);
			writer.Unsigned("frameCount", metadata.m_Sequence->m_FrameCount);
			writer.Unsigned("referenceSamples", metadata.m_Sequence->m_ReferenceSamples);
			writer.Number("referenceTextureLodBias",
				metadata.m_Sequence->m_ReferenceTextureLodBias);
			writer.EndObject();
		}
		else
		{
			writer.Null("sequence");
		}

		writer.String("capturedAtUtc", metadata.m_CapturedAtUtc);
		writer.EndObject();
		return writer.Finish();
	}
}
