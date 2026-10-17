#include "Capture/FrameCaptureMetadata.h"
#include "Capture/NonFiniteWatchCoordinator.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <initializer_list>
#include <string>
#include <string_view>

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
			const auto vector = [&](std::string_view name, const Vector3& value)
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

	std::string SerializeFrameCaptureMetadata(const FrameCaptureMetadata& metadata) noexcept
	{
		JsonWriter writer;
		writer.BeginObject();
		writer.Unsigned("schemaVersion", FrameCaptureMetadataSchemaVersion);
		writer.Unsigned("requestId", metadata.m_RequestId);
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
