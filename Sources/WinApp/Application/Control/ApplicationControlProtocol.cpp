#include "Application/Control/ApplicationControlProtocol.h"
#include "Capture/FrameCaptureMetadata.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"
#include "ShaderArtifactRuntime/NonFiniteCounterABI.h"
#include "GGLabRuntime/Graphics/Pipeline/GTAOTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/Pipeline/TemporalReference.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr uint32_t MaxSettleFrames = 10000;
		constexpr size_t MaxSequenceCaptureFrames = 10000;
		constexpr double MaxReferenceTextureLodBias = 8.0;

		[[nodiscard]] std::string ToUtf8(const std::filesystem::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		[[nodiscard]] std::string Dump(const Json& document) noexcept
		{
			// Invalid UTF-8 in captured text is replaced instead of throwing.
			return document.dump(-1, ' ', false, Json::error_handler_t::replace);
		}

		[[nodiscard]] Json MakeResponse(uint64_t id, bool ok)
		{
			return Json{
				{ "protocol", ApplicationControlProtocolVersion },
				{ "id", id },
				{ "ok", ok },
			};
		}

		[[nodiscard]] std::optional<ApplicationControlCommand> ParseCommand(
			std::string_view command) noexcept
		{
			if (command == "status")
			{
				return ApplicationControlCommand::Status;
			}
			if (command == "capture")
			{
				return ApplicationControlCommand::Capture;
			}
			if (command == "graph")
			{
				return ApplicationControlCommand::Graph;
			}
			if (command == "export")
			{
				return ApplicationControlCommand::Export;
			}
			if (command == "watch")
			{
				return ApplicationControlCommand::Watch;
			}
			if (command == "result")
			{
				return ApplicationControlCommand::Result;
			}
			if (command == "stop")
			{
				return ApplicationControlCommand::Stop;
			}
			if (command == "sequence")
			{
				return ApplicationControlCommand::Sequence;
			}
			if (command == "sequence-cancel")
			{
				return ApplicationControlCommand::SequenceCancel;
			}
			return std::nullopt;
		}

		// Parses a diagnostic tap name; returns an error text.
		[[nodiscard]] std::string ParseDiagnosticTap(
			const Json& value, std::optional<PostProcessDebugTap>& outTap)
		{
			outTap = value.is_string()
				? FindFrameCaptureDiagnosticTap(value.get<std::string>())
				: std::nullopt;
			if (!outTap)
			{
				return "Field 'diagnosticTap' must name a diagnostic tap such as "
					"'temporal-history-weight'.";
			}
			return {};
		}

		[[nodiscard]] std::string ValidateDiagnosticTap(
			FrameCaptureSource source, const std::optional<PostProcessDebugTap>& tap)
		{
			if ((source == FrameCaptureSource::Diagnostic) != tap.has_value())
			{
				return "Field 'diagnosticTap' is required for, and only valid with, source "
					"'diagnostic'.";
			}
			return {};
		}

		// Parses an absolute output directory; returns an error text.
		[[nodiscard]] std::string ParseOutputDirectory(
			const Json& value, std::filesystem::path& outDirectory)
		{
			if (!value.is_string())
			{
				return "Field 'outputDirectory' must be a string.";
			}
			const std::string text = value.get<std::string>();
			outDirectory = std::filesystem::path(std::u8string(text.begin(), text.end()));
			if (!outDirectory.is_absolute())
			{
				return "Field 'outputDirectory' must be an absolute directory.";
			}
			return {};
		}

		// Parses the GTAO overrides of a sequence; returns an error text. Values outside a
		// setting's range are rejected instead of clamped.
		[[nodiscard]] std::string ParseGTAOOverrides(
			const Json& value, FrameSequenceGTAOOverrides& outOverrides)
		{
			if (!value.is_object())
			{
				return "Field 'gtao' must be an object.";
			}
			for (const auto& [key, fieldValue] : value.items())
			{
				if (key == "temporal")
				{
					if (!fieldValue.is_boolean())
					{
						return "GTAO override 'temporal' must be a boolean.";
					}
					outOverrides.m_TemporalAccumulation = fieldValue.get<bool>();
					continue;
				}
				struct CountField
				{
					std::string_view m_Name;
					std::optional<uint32_t> FrameSequenceGTAOOverrides::* m_Member;
					uint32_t m_Max;
				};
				constexpr std::array<CountField, 3> countFields{ {
					{ "temporalMaxSamples", &FrameSequenceGTAOOverrides::m_TemporalMaxSamples,
						GTAOMaxTemporalSamples },
					{ "directionCount", &FrameSequenceGTAOOverrides::m_DirectionCount,
						GTAOMaxDirectionCount },
					{ "stepCount", &FrameSequenceGTAOOverrides::m_StepCount, GTAOMaxStepCount },
				} };
				const auto countField = std::ranges::find(countFields, key, &CountField::m_Name);
				if (countField != countFields.end())
				{
					const double count = fieldValue.is_number() ? fieldValue.get<double>() : 0.0;
					if (!fieldValue.is_number() || count != std::floor(count) || count < 1.0 ||
						count > static_cast<double>(countField->m_Max))
					{
						return std::format("GTAO override '{}' must be an integer in [1, {}].",
							countField->m_Name, countField->m_Max);
					}
					outOverrides.*(countField->m_Member) = static_cast<uint32_t>(count);
					continue;
				}
				return std::format("Unknown GTAO override '{}'.", key);
			}
			return {};
		}

		// Parses the Temporal AA overrides of a sequence; returns an error text. Values
		// outside a setting's range are rejected instead of clamped, so a run records
		// exactly the configuration it was asked to evaluate.
		[[nodiscard]] std::string ParseTemporalAAOverrides(
			const Json& value, FrameSequenceTemporalAAOverrides& outOverrides)
		{
			if (!value.is_object())
			{
				return "Field 'temporalAA' must be an object.";
			}
			struct OverrideField
			{
				std::string_view m_Name;
				std::optional<float> FrameSequenceTemporalAAOverrides::* m_Member;
				float m_Min;
				float m_Max;
			};
			constexpr std::array<OverrideField, 9> fields{ {
				{ "maxHistoryFeedback", &FrameSequenceTemporalAAOverrides::m_MaxHistoryFeedback,
					0.0f, TemporalAAMaxHistoryFeedbackCeiling },
				{ "depthAbsoluteThreshold",
					&FrameSequenceTemporalAAOverrides::m_DepthAbsoluteThreshold,
					0.0f, TemporalAAMaxDepthThreshold },
				{ "depthRelativeThreshold",
					&FrameSequenceTemporalAAOverrides::m_DepthRelativeThreshold,
					0.0f, TemporalAAMaxDepthThreshold },
				{ "velocityWeightScale", &FrameSequenceTemporalAAOverrides::m_VelocityWeightScale,
					0.0f, TemporalAAMaxVelocityWeightScale },
				{ "luminanceWeightScale",
					&FrameSequenceTemporalAAOverrides::m_LuminanceWeightScale,
					0.0f, TemporalAAMaxLuminanceWeightScale },
				{ "neighborhoodClampExpansion",
					&FrameSequenceTemporalAAOverrides::m_NeighborhoodClampExpansion,
					0.0f, TemporalAAMaxNeighborhoodClampExpansion },
				{ "textureLodBiasOffset",
					&FrameSequenceTemporalAAOverrides::m_TextureLodBiasOffset,
					TemporalAAMinTextureLodBiasOffset, TemporalAAMaxTextureLodBiasOffset },
				{ "varianceClipGamma", &FrameSequenceTemporalAAOverrides::m_VarianceClipGamma,
					TemporalAAMinVarianceClipGamma, TemporalAAMaxVarianceClipGamma },
				{ "historyRelaxation", &FrameSequenceTemporalAAOverrides::m_HistoryRelaxation,
					0.0f, TemporalAAMaxHistoryRelaxation },
			} };
			for (const auto& [key, fieldValue] : value.items())
			{
				if (key == "enabled")
				{
					if (!fieldValue.is_boolean())
					{
						return "Temporal AA override 'enabled' must be a boolean.";
					}
					outOverrides.m_Enabled = fieldValue.get<bool>();
					continue;
				}
				if (key == "historyFilter")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array filters{ TemporalAAHistoryFilter::Bilinear,
						TemporalAAHistoryFilter::CatmullRomClamped };
					const auto filter = std::ranges::find(
						filters, name, &GetTemporalAAHistoryFilterName);
					if (filter == filters.end())
					{
						return "Temporal AA override 'historyFilter' must be 'bilinear' or "
							"'catmull-rom-clamped'.";
					}
					outOverrides.m_HistoryFilter = *filter;
					continue;
				}
				if (key == "currentFilter")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array filters{ TemporalAACurrentFilter::Point,
						TemporalAACurrentFilter::Gaussian };
					const auto filter = std::ranges::find(
						filters, name, &GetTemporalAACurrentFilterName);
					if (filter == filters.end())
					{
						return "Temporal AA override 'currentFilter' must be 'point' or "
							"'gaussian'.";
					}
					outOverrides.m_CurrentFilter = *filter;
					continue;
				}
				if (key == "motionSelection")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array selections{ TemporalAAMotionSelection::Center,
						TemporalAAMotionSelection::ClosestDepth };
					const auto selection = std::ranges::find(
						selections, name, &GetTemporalAAMotionSelectionName);
					if (selection == selections.end())
					{
						return "Temporal AA override 'motionSelection' must be 'center' or "
							"'closest-depth'.";
					}
					outOverrides.m_MotionSelection = *selection;
					continue;
				}
				if (key == "postTemporalView")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array views{ TemporalAAPostTemporalView::Jittered,
						TemporalAAPostTemporalView::Unjittered };
					const auto view =
						std::ranges::find(views, name, &GetTemporalAAPostTemporalViewName);
					if (view == views.end())
					{
						return "Temporal AA override 'postTemporalView' must be 'jittered' or "
							"'unjittered'.";
					}
					outOverrides.m_PostTemporalView = *view;
					continue;
				}
				if (key == "historyAccumulation")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array accumulations{
						TemporalAAHistoryAccumulation::CompatibilityAge,
						TemporalAAHistoryAccumulation::EffectiveSamples };
					const auto accumulation = std::ranges::find(
						accumulations, name, &GetTemporalAAHistoryAccumulationName);
					if (accumulation == accumulations.end())
					{
						return "Temporal AA override 'historyAccumulation' must be "
							"'compatibility-age' or 'effective-samples'.";
					}
					outOverrides.m_HistoryAccumulation = *accumulation;
					continue;
				}
				if (key == "historyRectification")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array rectifications{
						TemporalAAHistoryRectification::MinMaxClamp,
						TemporalAAHistoryRectification::VarianceClip,
						TemporalAAHistoryRectification::BoundedVarianceClip };
					const auto rectification = std::ranges::find(
						rectifications, name, &GetTemporalAAHistoryRectificationName);
					if (rectification == rectifications.end())
					{
						return "Temporal AA override 'historyRectification' must be "
							"'minmax-clamp', 'variance-clip' or 'bounded-variance-clip'.";
					}
					outOverrides.m_HistoryRectification = *rectification;
					continue;
				}
				if (key == "resolutionPreset")
				{
					const std::string name =
						fieldValue.is_string() ? fieldValue.get<std::string>() : "";
					constexpr std::array presets{ TemporalAAResolutionPreset::Native,
						TemporalAAResolutionPreset::Quality };
					const auto preset =
						std::ranges::find(presets, name, &GetTemporalAAResolutionPresetName);
					if (preset == presets.end())
					{
						return "Temporal AA override 'resolutionPreset' must be 'native' or "
							"'quality'.";
					}
					outOverrides.m_ResolutionPreset = *preset;
					continue;
				}
				const auto field = std::ranges::find(fields, key, &OverrideField::m_Name);
				if (field == fields.end())
				{
					return std::format("Unknown Temporal AA override '{}'.", key);
				}
				const double number = fieldValue.is_number() ? fieldValue.get<double>() : 0.0;
				if (!fieldValue.is_number() || !std::isfinite(number) ||
					number < static_cast<double>(field->m_Min) ||
					number > static_cast<double>(field->m_Max))
				{
					return std::format("Temporal AA override '{}' must be a number from {} to {}.",
						key, field->m_Min, field->m_Max);
				}
				outOverrides.*(field->m_Member) = static_cast<float>(number);
			}
			return {};
		}

		[[nodiscard]] std::string ParseResourceAddresses(const Json& value, std::vector<RGResourceAddress>& targets)
		{
			if (!value.is_array() || value.empty() || value.size() > 64)
			{
				return "Field 'targets' must be an array of one to 64 resource addresses.";
			}
			for (const auto& item : value)
			{
				if (!item.is_object())
				{
					return "Each resource address must be an object.";
				}
				RGResourceAddress address;
				for (const auto& [key, field] : item.items())
				{
					if (key == "resource")
					{
						if (!field.is_string() || field.get_ref<const std::string&>().empty())
						{
							return "A resource address requires a non-empty 'resource'.";
						}
						address.m_Resource = field.get<std::string>();
					}
					else if (key == "at")
					{
						if (field == "final")
						{
							address.m_Position = RGObservationPosition::Final;
						}
						else if (field.is_object() && field.size() == 1 && (field.contains("before") || field.contains("after")))
						{
							const bool before = field.contains("before");
							const auto& pass = field[before ? "before" : "after"];
							if (!pass.is_string() || pass.get_ref<const std::string&>().empty())
							{
								return "A pass address requires a non-empty pass name.";
							}
							address.m_Position = before ? RGObservationPosition::Before : RGObservationPosition::After;
							address.m_Pass = pass.get<std::string>();
						}
						else
						{
							return "Field 'at' must be 'final', {'before': name} or {'after': name}.";
						}
					}
					else if (key == "mip" || key == "occurrence")
					{
						if (!field.is_number_unsigned() || field.get<uint64_t>() > UINT32_MAX || (key == "occurrence" && field == 0))
						{
							return "Mip must be a uint32; occurrence must be a positive uint32.";
						}
						if (key == "mip")
						{
							address.m_Mip = field.get<uint32_t>();
						}
						else
						{
							address.m_Occurrence = field.get<uint32_t>();
						}
					}
					else if (key == "slices")
					{
						if (!field.is_array() || field.size() != 2 || !field[0].is_number_unsigned() || !field[1].is_number_unsigned() ||
							field[0].get<uint64_t>() > UINT32_MAX || field[1].get<uint64_t>() > UINT32_MAX || field[1] == 0)
						{
							return "Field 'slices' must be [first, count], with a positive count and uint32 values.";
						}
						address.m_FirstSlice = field[0].get<uint32_t>(); address.m_SliceCount = field[1].get<uint32_t>();
					}
					else if (key == "aspect")
					{
						if (field == "color")
						{
							address.m_Aspect = RHITextureAspect::Color;
						}
						else if (field == "depth")
						{
							address.m_Aspect = RHITextureAspect::Depth;
						}
						else if (field == "stencil")
						{
							address.m_Aspect = RHITextureAspect::Stencil;
						}
						else
						{
							return "Field 'aspect' must be 'color', 'depth' or 'stencil'.";
						}
					}
					else
					{
						return std::format("Unknown resource address field '{}'.", key);
					}
				}
				if (address.m_Resource.empty())
				{
					return "A resource address requires 'resource'.";
				}
				if (address.m_Position == RGObservationPosition::Final && address.m_Occurrence)
				{
					return "A final resource address has no pass occurrence.";
				}
				targets.push_back(std::move(address));
			}
			return {};
		}

		[[nodiscard]] bool IsUint32(const Json& value)
		{
			return value.is_number_unsigned() && value.get<uint64_t>() <= UINT32_MAX;
		}

#if GGLAB_DEVELOPMENT_BUILD
		// {"point": "<registered id>", "value": "<NonFiniteInjectionValue>", "x": n, "y": n, "component": c}
		[[nodiscard]] std::string ParseWatchInjection(const Json& value, std::optional<NonFiniteInjection>& injection)
		{
			NonFiniteInjection parsed;
			bool point = false;
			bool injected = false;
			bool x = false;
			bool y = false;
			for (const auto& [key, field] : value.items())
			{
				if (key == "point" && field.is_string())
				{
					const auto found = std::ranges::find(nonfinite::InjectionPoints, field.get<std::string>(),
						&nonfinite::InjectionPoint::m_Id);
					if (found == nonfinite::InjectionPoints.end())
					{
						return std::format("Unknown non-finite injection point '{}'.", field.get<std::string>());
					}
					parsed.m_PointIndex = static_cast<uint32_t>(std::distance(nonfinite::InjectionPoints.begin(), found));
					point = true;
				}
				else if (key == "value" && field.is_string())
				{
					const auto parsedValue = ParseNonFiniteInjectionValue(field.get<std::string>());
					if (!parsedValue)
					{
						return "Injection value must be NaN, PositiveInfinity, NegativeInfinity, Overflow or Negative.";
					}
					parsed.m_Value = *parsedValue;
					injected = true;
				}
				else if ((key == "x" || key == "y" || key == "component") && IsUint32(field))
				{
					(key == "x" ? parsed.m_X : key == "y" ? parsed.m_Y : parsed.m_Component) = field.get<uint32_t>();
					x |= key == "x";
					y |= key == "y";
				}
				else
				{
					return std::format("Unknown or invalid injection field '{}'.", key);
				}
			}
			if (!point || !injected || !x || !y || parsed.m_Component > 3)
			{
				return "Injection requires 'point', 'value', 'x' and 'y', and a component from 0 to 3.";
			}
			injection = parsed;
			return {};
		}
#endif

		[[nodiscard]] std::string ParseWatchFields(const Json& document, NonFiniteWatchConfiguration& configuration)
		{
			for (const auto& [key, value] : document.items())
			{
				if (key == "protocol" || key == "id" || key == "command")
				{
					continue;
				}
				if (key == "enabled" && value.is_boolean())
				{
					configuration.m_Enabled = value.get<bool>();
				}
				else if (key == "mode" && (value == "report" || value == "fail"))
				{
					configuration.m_Mode = value == "fail" ? NonFiniteWatchMode::Fail : NonFiniteWatchMode::Report;
				}
				else if (key == "claims" && value.is_array() && !value.empty() && value.size() <= 2)
				{
					configuration.m_Boundary = false;
					configuration.m_Production = false;
					for (const auto& claim : value)
					{
						if (claim == "boundary" && !configuration.m_Boundary)
						{
							configuration.m_Boundary = true;
						}
						else if (claim == "production" && !configuration.m_Production)
						{
							configuration.m_Production = true;
						}
						else
						{
							return "Watch claims must contain unique 'boundary' and/or 'production' names.";
						}
					}
				}
				else if (key == "targets" && value.is_array() && value.size() <= 64)
				{
					for (const auto& item : value)
					{
						if (!item.is_object())
						{
							return "Watch targets must be resource address objects.";
						}
						Json address = item;
						address.erase("buffer");
						std::vector<RGResourceAddress> addresses;
						if (auto error = ParseResourceAddresses(Json::array({address}), addresses); !error.empty())
						{
							return error;
						}
						NonFiniteScanTarget target{ .m_Address = std::move(addresses.front()) };
						if (item.contains("buffer"))
						{
							const auto& buffer = item["buffer"];
							if (!buffer.is_object() || buffer.size() != 3 || !buffer.contains("stride") || !buffer.contains("validElementCount") || !buffer.contains("fields") ||
								!IsUint32(buffer["stride"]) || !IsUint32(buffer["validElementCount"]) || !buffer["fields"].is_array() || buffer["fields"].empty() || buffer["fields"].size() > 256)
							{
								return "A buffer interpretation requires uint32 stride/validElementCount and one to 256 fields.";
							}
							std::vector<GpuBufferFieldLayout> fields;
							for (const auto& field : buffer["fields"])
							{
								if (!field.is_object() || !field.contains("offset") || !IsUint32(field["offset"]) || !field.contains("type"))
								{
									return "Each buffer field requires an offset and scalar type.";
								}
								for (const auto& [name, ignored] : field.items())
								{
									if (name != "offset" && name != "type" && name != "count")
									{
										return "Unknown buffer field property.";
									}
								}
								const auto& type = field["type"];
								if ((type != "float16" && type != "float32" && type != "uint8" && type != "uint32") ||
									(field.contains("count") && (!IsUint32(field["count"]) || (field["count"] == 0 || field["count"].get<uint64_t>() > 4096))))
								{
									return "Buffer field type/count is invalid.";
								}
								fields.push_back({ "field", field["offset"].get<uint32_t>(), type == "float16" ? GpuBufferScalar::Float16 : type == "float32" ? GpuBufferScalar::Float32 :
									type == "uint32" ? GpuBufferScalar::Uint32 : GpuBufferScalar::Uint8, field.value("count", 1u) });
							}
							uint64_t components = 0;
							for (const auto& field : fields)
							{
								if (field.m_Scalar == GpuBufferScalar::Float16 || field.m_Scalar == GpuBufferScalar::Float32)
								{
									components += field.m_Count;
								}
							}
							if (components > 4096)
							{
								return "A buffer interpretation is limited to 4096 floating-point components per element.";
							}
							const uint32_t stride = buffer["stride"].get<uint32_t>();
							const uint32_t valid = buffer["validElementCount"].get<uint32_t>();
							target.m_BufferLayout = BuildNonFiniteBufferScanLayout({ stride, fields }, static_cast<uint64_t>(stride) * valid, valid);
							if (target.m_BufferLayout.m_Failure != NonFiniteScanLayoutFailure::None)
							{
								return "The buffer interpretation is invalid, oversized or contains no floating-point fields.";
							}
						}
						configuration.m_Targets.push_back(std::move(target));
					}
				}
				else if (key == "inject" && value.is_object())
				{
#if GGLAB_DEVELOPMENT_BUILD
					if (auto error = ParseWatchInjection(value, configuration.m_Injection); !error.empty())
					{
						return error;
					}
#else
					return "Watch field 'inject' is available only in Debug and Develop builds.";
#endif
				}
				else
				{
					return std::format("Unknown or invalid watch field '{}'.", key);
				}
			}
			if (!configuration.m_Enabled && document.size() != 4)
			{
				return "Disabling watch takes only 'enabled': false.";
			}
			return {};
		}

		// Fills the sequence request; returns an error text.
		[[nodiscard]] std::string ParseSequenceFields(
			const Json& document, ApplicationControlRequest& request)
		{
			FrameSequenceRequest& sequence = request.m_Sequence;
			for (const auto& [key, value] : document.items())
			{
				if (key == "protocol" || key == "id" || key == "command")
				{
					continue;
				}
				if (key == "path" || key == "requiredContentId" || key == "label" ||
					key == "note")
				{
					if (!value.is_string())
					{
						return std::format("Field '{}' must be a string.", key);
					}
					std::string& target = key == "path" ? sequence.m_CameraPathId
						: key == "requiredContentId" ? sequence.m_RequiredContentId
						: key == "label" ? sequence.m_Label
						: sequence.m_Note;
					target = value.get<std::string>();
				}
				else if (key == "source")
				{
					const std::string source = value.is_string() ? value.get<std::string>() : "";
					if (source == "scene")
					{
						sequence.m_CaptureSource = FrameCaptureSource::Scene;
					}
					else if (source == "composited")
					{
						sequence.m_CaptureSource = FrameCaptureSource::Composited;
					}
					else if (source == "diagnostic")
					{
						sequence.m_CaptureSource = FrameCaptureSource::Diagnostic;
					}
					else
					{
						return "Field 'source' must be 'scene', 'composited' or 'diagnostic'.";
					}
				}
				else if (key == "diagnosticTap")
				{
					if (std::string error = ParseDiagnosticTap(value, sequence.m_DiagnosticTap);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "exports")
				{
					if (!value.is_array() || value.size() > MaxSequenceCaptureFrames)
					{
						return "Field 'exports' must be a bounded array.";
					}
					for (const auto& item : value)
					{
						if (!item.is_object() || !item.contains("frame") || !item["frame"].is_number_unsigned() ||
							item["frame"].get<uint64_t>() > UINT32_MAX || !item.contains("targets"))
						{
							return "Each sequence export requires a uint32 'frame' and 'targets'.";
						}
						FrameSequenceResourceExport resourceExport{ .m_Frame = item["frame"].get<uint32_t>() };
						for (const auto& [field, fieldValue] : item.items())
						{
							if (field == "frame" || field == "targets")
							{
								continue;
							}
							if (field != "graph" || !fieldValue.is_boolean())
							{
								return "Only a boolean 'graph' is optional in a sequence export.";
							}
							resourceExport.m_IncludeGraph = fieldValue.get<bool>();
						}
						if (auto error = ParseResourceAddresses(item["targets"], resourceExport.m_Targets); !error.empty())
						{
							return error;
						}
						sequence.m_Exports.push_back(std::move(resourceExport));
					}
				}
				else if (key == "referenceSamples")
				{
					if (!value.is_number_unsigned() ||
						value.get<uint64_t>() > MaxTemporalReferenceSamples)
					{
						return std::format("Field 'referenceSamples' must be 0 to {}.",
							MaxTemporalReferenceSamples);
					}
					sequence.m_ReferenceSamples = value.get<uint32_t>();
				}
				else if (key == "referenceTextureLodBias")
				{
					const double bias = value.is_number() ? value.get<double>() : 0.0;
					if (!value.is_number() || !std::isfinite(bias) ||
						bias < -MaxReferenceTextureLodBias || bias > MaxReferenceTextureLodBias)
					{
						return std::format("Field 'referenceTextureLodBias' must be a number from "
							"{} to {}.", -MaxReferenceTextureLodBias, MaxReferenceTextureLodBias);
					}
					sequence.m_ReferenceTextureLodBias = static_cast<float>(bias);
				}
				else if (key == "captureFrames")
				{
					if (!value.is_array() || value.size() > MaxSequenceCaptureFrames)
					{
						return std::format("Field 'captureFrames' must be an array of at most {} "
							"frame numbers.", MaxSequenceCaptureFrames);
					}
					for (const Json& frame : value)
					{
						if (!frame.is_number_unsigned() ||
							frame.get<uint64_t>() > std::numeric_limits<uint32_t>::max())
						{
							return "Field 'captureFrames' must contain unsigned 32-bit frame numbers.";
						}
						sequence.m_CaptureFrames.push_back(frame.get<uint32_t>());
					}
				}
				else if (key == "outputDirectory")
				{
					if (std::string error = ParseOutputDirectory(value, sequence.m_OutputDirectory);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "temporalAA")
				{
					if (std::string error =
						ParseTemporalAAOverrides(value, sequence.m_TemporalAAOverrides);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "gtao")
				{
					if (std::string error = ParseGTAOOverrides(value, sequence.m_GTAOOverrides);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "gpuTiming")
				{
					if (!value.is_boolean())
					{
						return "Field 'gpuTiming' must be a boolean.";
					}
					sequence.m_GpuTiming = value.get<bool>();
				}
				else
				{
					return std::format("Unknown field '{}' for command 'sequence'.", key);
				}
			}
			if (sequence.m_CameraPathId.empty())
			{
				return "Command 'sequence' requires a non-empty string 'path'.";
			}
			return ValidateDiagnosticTap(sequence.m_CaptureSource, sequence.m_DiagnosticTap);
		}

		// Fills the capture request from optional fields; returns an error text.
		[[nodiscard]] std::string ParseCaptureFields(
			const Json& document, ApplicationControlRequest& request)
		{
			FrameCaptureRequest& capture = request.m_Capture;
			capture.m_Timing = FrameCaptureTiming::AfterReady;
			capture.m_SettleFrames = 8;
			const bool graph = request.m_Command == ApplicationControlCommand::Graph;
			const bool resources = request.m_Command == ApplicationControlCommand::Export;
			if (graph)
			{
				capture.m_Source = FrameCaptureSource::RenderGraph;
			}
			if (resources)
			{
				capture.m_Source = FrameCaptureSource::ResourceExport;
			}
			for (const auto& [key, value] : document.items())
			{
				if (key == "protocol" || key == "id" || key == "command")
				{
					continue;
				}
				if (key == "summary" && graph)
				{
					if (!value.is_boolean())
					{
						return "Field 'summary' must be a boolean.";
					}
					capture.m_GraphSummary = value.get<bool>();
				}
				else if (key == "targets" && resources)
				{
					if (auto error = ParseResourceAddresses(value, capture.m_ResourceTargets); !error.empty())
					{
						return error;
					}
				}
				else if (key == "graph" && resources)
				{
					if (!value.is_boolean())
					{
						return "Field 'graph' must be a boolean.";
					}
					capture.m_IncludeGraph = value.get<bool>();
				}
				else if (key == "source" && !graph && !resources)
				{
					const std::string source = value.is_string() ? value.get<std::string>() : "";
					if (source == "scene")
					{
						capture.m_Source = FrameCaptureSource::Scene;
					}
					else if (source == "composited")
					{
						capture.m_Source = FrameCaptureSource::Composited;
					}
					else if (source == "diagnostic")
					{
						capture.m_Source = FrameCaptureSource::Diagnostic;
					}
					else
					{
						return "Field 'source' must be 'scene', 'composited' or 'diagnostic'.";
					}
				}
				else if (key == "diagnosticTap" && !graph && !resources)
				{
					if (std::string error = ParseDiagnosticTap(value, capture.m_DiagnosticTap);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "timing")
				{
					const std::string timing = value.is_string() ? value.get<std::string>() : "";
					if (timing == "after-ready")
					{
						capture.m_Timing = FrameCaptureTiming::AfterReady;
					}
					else if (timing == "next-frame")
					{
						capture.m_Timing = FrameCaptureTiming::NextFrame;
					}
					else
					{
						return "Field 'timing' must be 'after-ready' or 'next-frame'.";
					}
				}
				else if (key == "settleFrames")
				{
					if (!value.is_number_unsigned() || value.get<uint64_t>() > MaxSettleFrames)
					{
						return std::format("Field 'settleFrames' must be 0 to {}.", MaxSettleFrames);
					}
					capture.m_SettleFrames = value.get<uint32_t>();
				}
				else if (key == "requiredContentId" || key == "view" || key == "label" ||
					key == "note")
				{
					if (!value.is_string())
					{
						return std::format("Field '{}' must be a string.", key);
					}
					std::string& target = key == "requiredContentId" ? capture.m_RequiredContentId
						: key == "view" ? capture.m_ReferenceViewId
						: key == "label" ? capture.m_Label
						: capture.m_Note;
					target = value.get<std::string>();
				}
				else if (key == "outputDirectory")
				{
					if (std::string error = ParseOutputDirectory(value, capture.m_OutputDirectory);
						!error.empty())
					{
						return error;
					}
				}
				else if (key == "wait")
				{
					if (!value.is_boolean())
					{
						return "Field 'wait' must be a boolean.";
					}
					request.m_Wait = value.get<bool>();
				}
				else
				{
					return std::format("Unknown field '{}' for command '{}'.", key, graph ? "graph" : resources ? "export" : "capture");
				}
			}
			if (resources && capture.m_ResourceTargets.empty())
			{
				return "Command 'export' requires 'targets'.";
			}
			return ValidateDiagnosticTap(capture.m_Source, capture.m_DiagnosticTap);
		}

		[[nodiscard]] const char* ClaimName(NonFiniteClaimStatus status)
		{
			switch (status)
			{
			case NonFiniteClaimStatus::Passed: return "Passed";
			case NonFiniteClaimStatus::Failed: return "Failed";
			case NonFiniteClaimStatus::NotApplicable: return "NotApplicable";
			case NonFiniteClaimStatus::Unavailable: return "Unavailable";
			default: return "Incomplete";
			}
		}

		[[nodiscard]] const char* WatchFailureName(NonFiniteWatchFailure failure)
		{
			switch (failure)
			{
			case NonFiniteWatchFailure::None: return "None";
			case NonFiniteWatchFailure::NonFiniteDetected: return "NonFiniteDetected";
			case NonFiniteWatchFailure::NonFiniteReplaced: return "NonFiniteReplaced";
			case NonFiniteWatchFailure::ClampPolicyExceeded: return "ClampPolicyExceeded";
			default: return "MissingCoverage";
			}
		}

		[[nodiscard]] Json SerializeWatchTarget(const NonFiniteWatchTargetStatus& target)
		{
			const auto& address = target.m_Address;
			Json at = "final";
			if (address.m_Position != RGObservationPosition::Final)
			{
				at = Json{ { address.m_Position == RGObservationPosition::Before ? "before" : "after", address.m_Pass } };
			}
			Json source = { { "resource", address.m_Resource }, { "at", at }, { "mip", address.m_Mip } };
			if (address.m_Occurrence)
			{
				source["occurrence"] = *address.m_Occurrence;
			}
			if (address.m_SliceCount)
			{
				source["slices"] = { address.m_FirstSlice, *address.m_SliceCount };
			}
			if (address.m_Aspect != RHITextureAspect::None)
			{
				source["aspect"] = address.m_Aspect == RHITextureAspect::Depth ? "depth" : address.m_Aspect == RHITextureAspect::Stencil ? "stencil" : "color";
			}
			Json result = { { "address", address.m_Resource.empty() ? Json() : source },
				{ "semantic", target.m_Semantic ? Json(GetNonFiniteWatchSemanticName(*target.m_Semantic)) : Json() },
				{ "watchTier", target.m_Semantic ? Json(GetNonFiniteWatchTier(*target.m_Semantic)) : Json() },{ "failure", target.m_Failure },
				{ "generation", target.m_Generation.IsValid() ? Json(target.m_Generation.m_Revision) : Json() },
				{ "resourceIdentity", target.m_Generation.IsValid() ? Json{ { "index", target.m_Generation.m_Texture.Index() },
					{ "generation", target.m_Generation.m_Texture.Generation() } } : Json() },
				{ "lastCheckedFrameSerial", target.m_CheckedFrameSerial },
				{ "claim", ClaimName(target.m_Result.m_Claim.m_Status) }, { "reason", WatchFailureName(target.m_Result.m_Claim.m_Failure) },
				{ "checked", target.m_Record.m_CheckedCount }, { "nan", target.m_Record.m_NanCount },
				{ "positiveInf", target.m_Record.m_PositiveInfCount }, { "negativeInf", target.m_Record.m_NegativeInfCount },
				{ "minimum", target.m_Result.m_Minimum ? Json(*target.m_Result.m_Minimum) : Json() },
				{ "maximum", target.m_Result.m_Maximum ? Json(*target.m_Result.m_Maximum) : Json() },
				{ "firstIndex", target.m_Result.m_FirstNonFiniteIndex ? Json(*target.m_Result.m_FirstNonFiniteIndex) : Json() },
				{ "location", Json() } };
			if (const auto& location = target.m_TextureLocation; location)
			{
				result["location"] = { { "x", location->m_X }, { "y", location->m_Y }, { "slice", location->m_Slice }, { "channel", location->m_Channel } };
			}
			else if (target.m_Element && target.m_ByteOffset)
			{
				result["location"] = { { "element", *target.m_Element }, { "byteOffset", *target.m_ByteOffset } };
			}
			return result;
		}

		[[nodiscard]] Json SerializeInjection(const NonFiniteInjection& injection)
		{
			return { { "point", injection.m_PointIndex < nonfinite::InjectionPoints.size()
					? Json(nonfinite::InjectionPoints[injection.m_PointIndex].m_Id) : Json() },
				{ "value", GetNonFiniteInjectionValueName(injection.m_Value) },
				{ "x", injection.m_X }, { "y", injection.m_Y }, { "component", injection.m_Component } };
		}

		[[nodiscard]] Json SerializeWatchReport(const NonFiniteWatchStatus& status)
		{
			Json claims = Json::array();
			if (status.m_BoundaryRequested)
			{
				claims.push_back("boundary");
			}
			if (status.m_ProductionRequested)
			{
				claims.push_back("production");
			}
			Json report = { { "configurationVersion", status.m_ConfigurationVersion }, { "enabled", status.m_Enabled },
				{ "mode", status.m_Mode == NonFiniteWatchMode::Fail ? "fail" : "report" }, { "claims", claims }, { "coverage", "latest-completed-frame" },
				{ "targetsMode", status.m_DefaultTargets ? "defaults" : "explicit" },
				{ "defaultTiers", status.m_DefaultTargets && status.m_BoundaryRequested ? Json::array({ "A", "B" }) : Json::array() },
				{ "boundary", ClaimName(status.m_Boundary.m_Status) }, { "boundaryFrameSerial", status.m_BoundaryFrameSerial },
				{ "production", ClaimName(status.m_Production.m_Status) }, { "productionFrameSerial", status.m_ProductionFrameSerial },
				{ "failure", status.m_Failure }, { "productionFailure", status.m_ProductionFailure }, { "targets", Json::array() }, { "firstAnomaly", Json() } };
			for (const auto& target : status.m_Targets)
			{
				report["targets"].push_back(SerializeWatchTarget(target));
			}
			// Since-configuration history: every site and target with clamps or anomalies.
			report["productionFrames"] = status.m_ProductionFrames;
			report["siteTotals"] = Json::array();
			for (const auto& totals : status.m_SiteTotals)
			{
				const auto& counts = totals.m_Counters;
				report["siteTotals"].push_back({
					{ "program", totals.m_ProgramIndex < nonfinite::ProgramCount ? Json(nonfinite::Programs[totals.m_ProgramIndex]) : Json() },
					{ "site", totals.m_SiteIndex < nonfinite::SiteCount ? Json(nonfinite::Sites[totals.m_SiteIndex].m_Id) : Json() },
					{ "frames", totals.m_Frames }, { "failedFrames", totals.m_FailedFrames }, { "firstFrameSerial", totals.m_FirstFrameSerial },
					{ "nonFiniteReplacements", counts.m_NonFiniteReplacements }, { "negativeClamps", counts.m_NegativeClamps },
					{ "fp16MaximumClamps", counts.m_Fp16MaximumClamps },
					{ "mostNegative", counts.m_MostNegative ? Json(*counts.m_MostNegative) : Json() },
					{ "maximum", counts.m_Maximum ? Json(*counts.m_Maximum) : Json() } });
			}
			report["targetTotals"] = Json::array();
			for (const auto& totals : status.m_TargetTotals)
			{
				report["targetTotals"].push_back({
					{ "resource", totals.m_Address.m_Resource }, { "mip", totals.m_Address.m_Mip },
					{ "semantic", totals.m_Semantic ? Json(GetNonFiniteWatchSemanticName(*totals.m_Semantic)) : Json() },
					{ "checkedFrames", totals.m_CheckedFrames }, { "failedFrames", totals.m_FailedFrames },
					{ "firstFailedFrameSerial", totals.m_FirstFailedFrameSerial ? Json(totals.m_FirstFailedFrameSerial) : Json() },
					{ "nan", totals.m_NanCount }, { "positiveInf", totals.m_PositiveInfCount }, { "negativeInf", totals.m_NegativeInfCount },
					{ "minimum", totals.m_Minimum ? Json(*totals.m_Minimum) : Json() },
					{ "maximum", totals.m_Maximum ? Json(*totals.m_Maximum) : Json() } });
			}
			report["injection"] = Json();
			if (status.m_Injection)
			{
				// Debug verification: anomalies under this configuration were injected.
				report["injection"] = SerializeInjection(*status.m_Injection);
				report["injection"]["frameSerial"] = status.m_InjectionFrameSerial;
				report["injection"]["appliedCount"] = status.m_InjectionAppliedCount;
			}
			if (const auto& anomaly = status.m_FirstAnomaly; anomaly)
			{
				Json first = { { "frameSerial", anomaly->m_FrameSerial }, { "reason", WatchFailureName(anomaly->m_Failure) } };
				if (anomaly->m_Target)
				{
					first["target"] = SerializeWatchTarget(*anomaly->m_Target);
				}
				if (anomaly->m_ProgramIndex && anomaly->m_SiteIndex && *anomaly->m_ProgramIndex < nonfinite::ProgramCount && *anomaly->m_SiteIndex < nonfinite::SiteCount)
				{
					first["program"] = nonfinite::Programs[*anomaly->m_ProgramIndex];
					first["site"] = nonfinite::Sites[*anomaly->m_SiteIndex].m_Id;
				}
				report["firstAnomaly"] = std::move(first);
			}
			return report;
		}

		[[nodiscard]] Json SerializeGates(const FrameCaptureReadiness& readiness)
		{
			Json gates = Json::array();
			for (const FrameCaptureGate& gate : readiness.m_Gates)
			{
				gates.push_back({
					{ "name", gate.m_Name },
					{ "state", GetFrameCaptureGateStateName(gate.m_State) },
					{ "detail", gate.m_Detail },
					});
			}
			return gates;
		}

		[[nodiscard]] Json SerializeTimingSummary(std::span<const double> milliseconds)
		{
			const FrameSequenceTimingSummary summary = SummarizeFrameSequenceTiming(milliseconds);
			return Json{
				{ "count", summary.m_Count },
				{ "meanMs", summary.m_Mean },
				{ "medianMs", summary.m_Median },
				{ "p90Ms", summary.m_P90 },
				{ "minMs", summary.m_Min },
				{ "maxMs", summary.m_Max },
			};
		}

		[[nodiscard]] Json SerializeGpuTiming(const FrameSequenceGpuTiming& timing)
		{
			Json scopes = Json::array();
			for (const FrameSequenceGpuTimingSeries& series : timing.m_Scopes)
			{
				Json scope = SerializeTimingSummary(series.m_Milliseconds);
				scope["name"] = series.m_Name;
				scopes.push_back(std::move(scope));
			}
			return Json{
				{ "frames", timing.m_FrameMilliseconds.size() },
				{ "frame", SerializeTimingSummary(timing.m_FrameMilliseconds) },
				{ "scopes", std::move(scopes) },
			};
		}

		[[nodiscard]] Json SerializeSequence(const FrameSequenceStatus& status)
		{
			Json sequence = {
				{ "id", status.m_SequenceId },
				{ "state", GetFrameSequenceStateName(status.m_State) },
				{ "path", status.m_CameraPathId },
				{ "pathVersion", status.m_CameraPathVersion },
				{ "frameCount", status.m_FrameCount },
				{ "referenceSamples", status.m_ReferenceSamples },
				{ "submittedFrames", status.m_SubmittedFrames },
				{ "captureRequestIds", status.m_CaptureRequestIds },
				{ "completedCaptures", status.m_CompletedCaptures },
			};
			if (status.m_GpuTiming)
			{
				sequence["gpuTiming"] = SerializeGpuTiming(*status.m_GpuTiming);
			}
			if (status.m_WatchCoverage)
			{
				sequence["watch"] = Json::parse(SerializeNonFiniteWatchCoverage(*status.m_WatchCoverage));
			}
			if (!status.m_Failure.empty())
			{
				sequence["failure"] = status.m_Failure;
			}
			return sequence;
		}
	}

	ApplicationControlParseResult ParseApplicationControlRequest(std::string_view line) noexcept
	{
		ApplicationControlParseResult result{};
		const Json document = Json::parse(line, nullptr, false);
		if (document.is_discarded() || !document.is_object())
		{
			result.m_Error = "The request is not a JSON object.";
			return result;
		}

		const auto id = document.find("id");
		if (id == document.end() || !id->is_number_unsigned())
		{
			result.m_Error = "Field 'id' must be an unsigned integer.";
			return result;
		}
		result.m_Id = id->get<uint64_t>();

		const auto protocol = document.find("protocol");
		if (protocol == document.end() || !protocol->is_number_unsigned() ||
			protocol->get<uint64_t>() != ApplicationControlProtocolVersion)
		{
			result.m_Error = std::format(
				"Field 'protocol' must be {}.", ApplicationControlProtocolVersion);
			return result;
		}

		const auto commandField = document.find("command");
		const std::optional<ApplicationControlCommand> command =
			commandField != document.end() && commandField->is_string()
			? ParseCommand(commandField->get<std::string>())
			: std::nullopt;
		if (!command)
		{
			result.m_Error = "Field 'command' must be 'status', 'capture', 'graph', 'export', 'result', 'stop', "
				"'watch', 'sequence' or 'sequence-cancel'.";
			return result;
		}

		ApplicationControlRequest request{ .m_Id = result.m_Id, .m_Command = *command };
		std::string error;
		switch (*command)
		{
		case ApplicationControlCommand::Capture:
		case ApplicationControlCommand::Graph:
		case ApplicationControlCommand::Export:
			error = ParseCaptureFields(document, request);
			break;
		case ApplicationControlCommand::Watch:
			error = ParseWatchFields(document, request.m_Watch);
			break;
		case ApplicationControlCommand::Result:
		{
			const auto requestId = document.find("requestId");
			if (requestId == document.end() || !requestId->is_number_unsigned() ||
				requestId->get<uint64_t>() == 0 || document.size() != 4)
			{
				error = "Command 'result' takes exactly one non-zero unsigned 'requestId'.";
				break;
			}
			request.m_CaptureRequestId = requestId->get<uint64_t>();
			break;
		}
		case ApplicationControlCommand::Sequence:
			error = ParseSequenceFields(document, request);
			break;
		case ApplicationControlCommand::Status:
		case ApplicationControlCommand::Stop:
		case ApplicationControlCommand::SequenceCancel:
			if (document.size() != 3)
			{
				error = std::format("Command '{}' takes no fields.",
					commandField->get<std::string>());
			}
			break;
		}
		if (!error.empty())
		{
			result.m_Error = std::move(error);
			return result;
		}
		result.m_Request = std::move(request);
		return result;
	}

	std::string SerializeApplicationControlError(uint64_t id, std::string_view error) noexcept
	{
		Json response = MakeResponse(id, false);
		response["error"] = std::string(error);
		return Dump(response);
	}

	std::string SerializeApplicationControlStatus(
		uint64_t id, const ApplicationControlStatus& status) noexcept
	{
		Json response = MakeResponse(id, true);
		response["session"] = {
			{ "id", status.m_SessionId },
			{ "pid", status.m_ProcessId },
			{ "uptimeSeconds", status.m_UptimeSeconds },
			{ "hidden", status.m_Hidden },
			{ "width", status.m_Width },
			{ "height", status.m_Height },
		};
		response["capture"] = { { "unfinished", status.m_UnfinishedCaptures } };
		const auto claimName = [](NonFiniteClaimStatus value)
		{
			switch (value)
			{
			case NonFiniteClaimStatus::Passed: return "Passed";
			case NonFiniteClaimStatus::Failed: return "Failed";
			case NonFiniteClaimStatus::NotApplicable: return "NotApplicable";
			case NonFiniteClaimStatus::Unavailable: return "Unavailable";
			default: return "Incomplete";
			}
		};
		response["watch"] = { { "instrumentation", status.m_NonFiniteInstrumentation ? "available" : "unavailable" },
			{ "production", status.m_NonFiniteInstrumentation ? "Incomplete" : "Unavailable" }, { "sites", Json::array() } };
		if (const auto& production = status.m_NonFiniteProduction; production)
		{
			response["watch"]["production"] = claimName(production->m_Claim.m_Status);
			response["watch"]["frameSerial"] = production->m_FrameSerial;
			response["watch"]["failure"] = production->m_Failure;
			response["watch"]["injection"] = Json();
			if (production->m_Injection)
			{
				response["watch"]["injection"] = SerializeInjection(production->m_Injection->m_Request);
				response["watch"]["injection"]["appliedCount"] = production->m_Injection->m_AppliedCount;
			}
			response["watch"]["submission"] = { { "fenceIndex", production->m_SubmissionFenceIndex },
				{ "fenceGeneration", production->m_SubmissionFenceGeneration }, { "fenceValue", production->m_SubmissionFenceValue },
				{ "adapter", production->m_AdapterCompatibilityIdentity } };
			for (const auto& site : production->m_Sites)
			{
				const auto& counts = site.m_Counters;
				if (counts.m_NonFiniteReplacements == 0 && counts.m_NegativeClamps == 0 && counts.m_Fp16MaximumClamps == 0 && site.m_Claim.m_Status == NonFiniteClaimStatus::Passed)
				{
					continue;
				}
				response["watch"]["sites"].push_back({ { "program", nonfinite::Programs[site.m_ProgramIndex] },
					{ "site", nonfinite::Sites[site.m_SiteIndex].m_Id }, { "claim", claimName(site.m_Claim.m_Status) },
					{ "nonFiniteReplacements", counts.m_NonFiniteReplacements }, { "negativeClamps", counts.m_NegativeClamps },
					{ "fp16MaximumClamps", counts.m_Fp16MaximumClamps },
					{ "mostNegative", counts.m_MostNegative ? Json(*counts.m_MostNegative) : Json() },
					{ "maximum", counts.m_Maximum ? Json(*counts.m_Maximum) : Json() } });
			}
		}
		if (status.m_Watch)
		{
			response["watch"]["report"] = SerializeWatchReport(*status.m_Watch);
		}
		response["sequence"] = status.m_Sequence ? SerializeSequence(*status.m_Sequence) : Json();
		if (!status.m_Frame)
		{
			response["frame"] = nullptr;
			return Dump(response);
		}

		const FrameCaptureFrameState& frame = *status.m_Frame;
		const FrameCaptureCameraState& camera = frame.m_Camera;
		response["frame"] = {
			{ "backend", frame.m_Backend },
			{ "demoId", frame.m_DemoId },
			{ "labId", frame.m_LabId },
			{ "frameIndex", frame.m_FrameIndex },
			{ "ready", frame.m_Readiness.IsReady() },
			{ "settledFrames", status.m_SettledFrames },
			{ "gates", SerializeGates(frame.m_Readiness) },
			{ "camera", {
				{ "name", camera.m_Name },
				{ "position", camera.m_Position },
				{ "forward", camera.m_Forward },
				{ "verticalFovDegrees", camera.m_VerticalFovDegrees },
			} },
			{ "referenceViews", frame.m_ReferenceViewIds },
			{ "fixedDeltaTime", frame.m_FixedDeltaTime ? Json(*frame.m_FixedDeltaTime) : Json() },
		};
		return Dump(response);
	}

	std::string SerializeApplicationControlCaptureQueued(
		uint64_t id, uint64_t captureRequestId) noexcept
	{
		Json response = MakeResponse(id, true);
		response["requestId"] = captureRequestId;
		response["status"] = "queued";
		return Dump(response);
	}

	std::string SerializeApplicationControlCaptureResult(
		uint64_t id, const FrameCaptureRequestResult& result) noexcept
	{
		Json response = MakeResponse(id, true);
		response["requestId"] = result.m_RequestId;
		switch (result.m_Status)
		{
		case FrameCaptureRequestStatus::Completed:
			response["status"] = "completed";
			if (result.m_Metadata && result.m_Metadata->m_Source == FrameCaptureSource::ResourceExport)
			{
				response["exports"] = Json::array();
				for (const auto& path : result.m_ResourcePaths) response["exports"].push_back(ToUtf8(path));
				if (!result.m_GraphPath.empty())
				{
					response["graph"] = ToUtf8(result.m_GraphPath);
				}
			}
			else if (result.m_Metadata && result.m_Metadata->m_Source == FrameCaptureSource::RenderGraph)
			{
				response["graph"] = ToUtf8(result.m_MetadataPath);
			}
			else
			{
				response["image"] = ToUtf8(result.m_ImagePath);
			}
			response["metadata"] = ToUtf8(result.m_MetadataPath);
			break;
		case FrameCaptureRequestStatus::Cancelled:
			response["status"] = "cancelled";
			response["failure"] = result.m_Failure;
			break;
		case FrameCaptureRequestStatus::Failed:
			response["status"] = "failed";
			response["failure"] = result.m_Failure;
			if (result.m_ResourceFailure != RGObservationFailure::None)
			{
				response["failureCode"] = GetRGObservationFailureName(result.m_ResourceFailure);
			}
			break;
		}
		if (result.m_Metadata)
		{
			response["frameSerial"] = result.m_Metadata->m_FrameSerial;
			if (result.m_Metadata->m_WatchCoverage)
			{
				response["watch"] = Json::parse(SerializeNonFiniteWatchCoverage(*result.m_Metadata->m_WatchCoverage));
			}
		}
		return Dump(response);
	}

	std::string SerializeApplicationControlWatch(uint64_t id, const NonFiniteWatchStatus& status, bool instrumentation) noexcept
	{
		Json response = MakeResponse(id, true);
		response["watch"] = { { "instrumentation", instrumentation ? "available" : "unavailable" }, { "report", SerializeWatchReport(status) } };
		return Dump(response);
	}

	std::string SerializeApplicationControlStopping(uint64_t id) noexcept
	{
		Json response = MakeResponse(id, true);
		response["status"] = "stopping";
		return Dump(response);
	}

	std::string SerializeApplicationControlSequence(
		uint64_t id, const FrameSequenceStatus& status) noexcept
	{
		Json response = MakeResponse(id, true);
		response["sequence"] = SerializeSequence(status);
		return Dump(response);
	}
}
