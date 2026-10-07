#include "Capture/FrameCaptureMetadata.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
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

	std::string SerializeFrameCaptureMetadata(const FrameCaptureMetadata& metadata) noexcept
	{
		JsonWriter writer;
		writer.BeginObject();
		writer.Unsigned("schemaVersion", FrameCaptureMetadataSchemaVersion);
		writer.Unsigned("requestId", metadata.m_RequestId);
		writer.String("label", metadata.m_Label);
		writer.String("note", metadata.m_Note);
		writer.String("source", GetFrameCaptureSourceName(metadata.m_Source));
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
		writer.EndObject();

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
		writer.String("historyFilter", temporal.m_HistoryFilter);
		writer.String("currentFilter", temporal.m_CurrentFilter);
		writer.String("motionSelection", temporal.m_MotionSelection);
		writer.Number("textureLodBiasOffset", temporal.m_TextureLodBiasOffset);
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
