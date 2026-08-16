#include "GGLabFoundation/Json/JsonValue.h"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace gglab::json
{
	namespace
	{
		[[nodiscard]] constexpr bool IsWhitespace(char value) noexcept
		{
			return value == ' ' || value == '\t' || value == '\n' || value == '\r';
		}

		[[nodiscard]] constexpr int HexDigitValue(char value) noexcept
		{
			if (value >= '0' && value <= '9')
			{
				return value - '0';
			}
			if (value >= 'a' && value <= 'f')
			{
				return value - 'a' + 10;
			}
			if (value >= 'A' && value <= 'F')
			{
				return value - 'A' + 10;
			}
			return -1;
		}

		void AppendUtf8(std::string& out, std::uint32_t codePoint)
		{
			if (codePoint < 0x80)
			{
				out += static_cast<char>(codePoint);
			}
			else if (codePoint < 0x800)
			{
				out += static_cast<char>(0xC0 | (codePoint >> 6));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else if (codePoint < 0x10000)
			{
				out += static_cast<char>(0xE0 | (codePoint >> 12));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
			else
			{
				out += static_cast<char>(0xF0 | (codePoint >> 18));
				out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (codePoint & 0x3F));
			}
		}
	}

	bool JsonValue::IsNull() const noexcept
	{
		return std::holds_alternative<std::monostate>(m_Value);
	}
	bool JsonValue::IsBool() const noexcept
	{
		return std::holds_alternative<bool>(m_Value);
	}
	bool JsonValue::IsString() const noexcept
	{
		return std::holds_alternative<std::string>(m_Value);
	}
	bool JsonValue::IsNumber() const noexcept
	{
		return std::holds_alternative<double>(m_Value);
	}
	bool JsonValue::IsInteger() const noexcept
	{
		return IsNumber() && m_NumberKind == JsonNumberKind::Integer;
	}
	bool JsonValue::IsArray() const noexcept
	{
		return std::holds_alternative<JsonArray>(m_Value);
	}
	bool JsonValue::IsObject() const noexcept
	{
		return std::holds_alternative<JsonObject>(m_Value);
	}
	bool JsonValue::AsBool() const noexcept
	{
		return std::get<bool>(m_Value);
	}
	const std::string& JsonValue::AsString() const noexcept
	{
		return std::get<std::string>(m_Value);
	}
	std::int64_t JsonValue::AsInteger() const noexcept
	{
		return static_cast<std::int64_t>(std::get<double>(m_Value));
	}
	double JsonValue::AsReal() const noexcept
	{
		return std::get<double>(m_Value);
	}
	const JsonArray& JsonValue::AsArray() const noexcept
	{
		return std::get<JsonArray>(m_Value);
	}
	const JsonObject& JsonValue::AsObject() const noexcept
	{
		return std::get<JsonObject>(m_Value);
	}

	namespace
	{
		class JsonParser final
		{
		public:
			explicit JsonParser(std::string_view text) noexcept : m_Text(text) {}

			[[nodiscard]] std::optional<JsonValue> ParseDocument() noexcept
			{
				SkipWhitespace();
				std::optional<JsonValue> value = ParseValue();
				if (!value.has_value())
				{
					return std::nullopt;
				}
				SkipWhitespace();
				if (m_Position != m_Text.size())
				{
					return std::nullopt;
				}
				return value;
			}

		private:
			[[nodiscard]] bool AtEnd() const noexcept { return m_Position >= m_Text.size(); }

			void SkipWhitespace() noexcept
			{
				while (!AtEnd() && IsWhitespace(m_Text[m_Position]))
				{
					++m_Position;
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseValue() noexcept
			{
				if (AtEnd())
				{
					return std::nullopt;
				}
				switch (m_Text[m_Position])
				{
				case '{':
					return ParseObject();
				case '[':
					return ParseArray();
				case '"':
					return ParseString();
				case 't':
					return ParseLiteral("true", JsonValue{ true });
				case 'f':
					return ParseLiteral("false", JsonValue{ false });
				case 'n':
					return ParseLiteral("null", JsonValue{});
				default:
					return ParseNumber();
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseLiteral(
				std::string_view literal, JsonValue value) noexcept
			{
				if (m_Text.substr(m_Position, literal.size()) != literal)
				{
					return std::nullopt;
				}
				m_Position += literal.size();
				return value;
			}

			[[nodiscard]] std::optional<JsonValue> ParseObject() noexcept
			{
				++m_Position; // '{'
				JsonObject object;
				SkipWhitespace();
				if (!AtEnd() && m_Text[m_Position] == '}')
				{
					++m_Position;
					return JsonValue{ std::move(object) };
				}
				while (true)
				{
					SkipWhitespace();
					if (AtEnd() || m_Text[m_Position] != '"')
					{
						return std::nullopt;
					}
					std::optional<JsonValue> key = ParseString();
					if (!key.has_value())
					{
						return std::nullopt;
					}
					for (const JsonMember& existing : object)
					{
						if (existing.first == key->AsString())
						{
							return std::nullopt; // duplicate keys rejected
						}
					}
					SkipWhitespace();
					if (AtEnd() || m_Text[m_Position] != ':')
					{
						return std::nullopt;
					}
					++m_Position;
					SkipWhitespace();
					std::optional<JsonValue> value = ParseValue();
					if (!value.has_value())
					{
						return std::nullopt;
					}
					object.emplace_back(std::move(key->AsString()), std::move(*value));
					SkipWhitespace();
					if (AtEnd())
					{
						return std::nullopt;
					}
					if (m_Text[m_Position] == ',')
					{
						++m_Position;
						continue;
					}
					if (m_Text[m_Position] == '}')
					{
						++m_Position;
						return JsonValue{ std::move(object) };
					}
					return std::nullopt;
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseArray() noexcept
			{
				++m_Position; // '['
				JsonArray array;
				SkipWhitespace();
				if (!AtEnd() && m_Text[m_Position] == ']')
				{
					++m_Position;
					return JsonValue{ std::move(array) };
				}
				while (true)
				{
					SkipWhitespace();
					std::optional<JsonValue> value = ParseValue();
					if (!value.has_value())
					{
						return std::nullopt;
					}
					array.push_back(std::move(*value));
					SkipWhitespace();
					if (AtEnd())
					{
						return std::nullopt;
					}
					if (m_Text[m_Position] == ',')
					{
						++m_Position;
						continue;
					}
					if (m_Text[m_Position] == ']')
					{
						++m_Position;
						return JsonValue{ std::move(array) };
					}
					return std::nullopt;
				}
			}

			[[nodiscard]] std::optional<JsonValue> ParseString() noexcept
			{
				++m_Position; // '"'
				std::string value;
				while (true)
				{
					if (AtEnd())
					{
						return std::nullopt;
					}
					const char current = m_Text[m_Position++];
					if (current == '"')
					{
						return JsonValue{ std::move(value) };
					}
					if (static_cast<unsigned char>(current) < 0x20)
					{
						return std::nullopt; // raw control characters are invalid
					}
					if (current != '\\')
					{
						value += current;
						continue;
					}

					if (AtEnd())
					{
						return std::nullopt;
					}
					const char escaped = m_Text[m_Position++];
					switch (escaped)
					{
					case '"':
						value += '"';
						break;
					case '\\':
						value += '\\';
						break;
					case '/':
						value += '/';
						break;
					case 'b':
						value += '\b';
						break;
					case 'f':
						value += '\f';
						break;
					case 'n':
						value += '\n';
						break;
					case 'r':
						value += '\r';
						break;
					case 't':
						value += '\t';
						break;
					case 'u':
					{
						const std::optional<std::uint32_t> codeUnit = ParseHexCodeUnit();
						if (!codeUnit.has_value())
						{
							return std::nullopt;
						}
						std::uint32_t codePoint = *codeUnit;
						if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
						{
							// High surrogate: a low surrogate must follow.
							if (m_Position + 2 > m_Text.size() ||
								m_Text[m_Position] != '\\' || m_Text[m_Position + 1] != 'u')
							{
								return std::nullopt;
							}
							m_Position += 2;
							const std::optional<std::uint32_t> low = ParseHexCodeUnit();
							if (!low.has_value() || *low < 0xDC00 || *low > 0xDFFF)
							{
								return std::nullopt;
							}
							codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*low - 0xDC00);
						}
						else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
						{
							return std::nullopt; // lone low surrogate
						}
						AppendUtf8(value, codePoint);
						break;
					}
					default:
						return std::nullopt;
					}
				}
			}

			[[nodiscard]] std::optional<std::uint32_t> ParseHexCodeUnit() noexcept
			{
				if (m_Position + 4 > m_Text.size())
				{
					return std::nullopt;
				}
				std::uint32_t codeUnit = 0;
				for (int index = 0; index < 4; ++index)
				{
					const int digit = HexDigitValue(m_Text[m_Position + index]);
					if (digit < 0)
					{
						return std::nullopt;
					}
					codeUnit = (codeUnit << 4) | static_cast<std::uint32_t>(digit);
				}
				m_Position += 4;
				return codeUnit;
			}

			[[nodiscard]] std::optional<JsonValue> ParseNumber() noexcept
			{
				const std::size_t begin = m_Position;
				if (!AtEnd() && m_Text[m_Position] == '-')
				{
					++m_Position;
				}
				if (AtEnd())
				{
					return std::nullopt;
				}
				if (m_Text[m_Position] == '0')
				{
					++m_Position;
					if (!AtEnd() && m_Text[m_Position] >= '0' && m_Text[m_Position] <= '9')
					{
						return std::nullopt; // leading zeros are invalid
					}
				}
				else if (m_Text[m_Position] >= '1' && m_Text[m_Position] <= '9')
				{
					while (!AtEnd() && m_Text[m_Position] >= '0' && m_Text[m_Position] <= '9')
					{
						++m_Position;
					}
				}
				else
				{
					return std::nullopt;
				}

				bool isReal = false;
				if (!AtEnd() && m_Text[m_Position] == '.')
				{
					isReal = true;
					++m_Position;
					if (AtEnd() || m_Text[m_Position] < '0' || m_Text[m_Position] > '9')
					{
						return std::nullopt;
					}
					while (!AtEnd() && m_Text[m_Position] >= '0' && m_Text[m_Position] <= '9')
					{
						++m_Position;
					}
				}
				if (!AtEnd() && (m_Text[m_Position] == 'e' || m_Text[m_Position] == 'E'))
				{
					isReal = true;
					++m_Position;
					if (!AtEnd() && (m_Text[m_Position] == '+' || m_Text[m_Position] == '-'))
					{
						++m_Position;
					}
					if (AtEnd() || m_Text[m_Position] < '0' || m_Text[m_Position] > '9')
					{
						return std::nullopt;
					}
					while (!AtEnd() && m_Text[m_Position] >= '0' && m_Text[m_Position] <= '9')
					{
						++m_Position;
					}
				}

				const std::string_view token = m_Text.substr(begin, m_Position - begin);
				JsonValue value{};
				if (!isReal)
				{
					std::int64_t integer = 0;
					const auto [end, error] = std::from_chars(
						token.data(), token.data() + token.size(), integer);
					if (error == std::errc{} && end == token.data() + token.size())
					{
						value.m_Value = static_cast<double>(integer);
						value.m_NumberKind = JsonNumberKind::Integer;
						return value;
					}
					// Overflow falls through to the real representation.
				}
				value.m_Value = std::strtod(std::string(token).c_str(), nullptr);
				value.m_NumberKind = JsonNumberKind::Real;
				return value;
			}

			std::string_view m_Text;
			std::size_t m_Position = 0;
		};
	}

	std::optional<JsonValue> ParseJsonDocument(std::string_view text) noexcept
	{
		return JsonParser(text).ParseDocument();
	}

	void JsonWriter::AppendCommaIfNeeded()
	{
		if (m_PendingValue)
		{
			m_Content += ',';
		}
	}

	void JsonWriter::AppendRaw(std::string_view raw)
	{
		m_Content.append(raw);
	}

	std::string JsonWriter::Finish() &&
	{
		return std::move(m_Content);
	}

	void JsonWriter::BeginObject()
	{
		AppendCommaIfNeeded();
		m_Content += '{';
		m_PendingValue = false;
	}

	void JsonWriter::EndObject()
	{
		m_Content += '}';
		m_PendingValue = true;
	}

	void JsonWriter::BeginArray()
	{
		AppendCommaIfNeeded();
		m_Content += '[';
		m_PendingValue = false;
	}

	void JsonWriter::EndArray()
	{
		m_Content += ']';
		m_PendingValue = true;
	}

	void JsonWriter::Key(std::string_view key)
	{
		AppendCommaIfNeeded();
		// WriteString appends a member separator of its own; suppress it for
		// the key so the pair separator stays a single comma.
		m_PendingValue = false;
		WriteString(key);
		m_Content += ':';
		m_PendingValue = false;
	}

	void JsonWriter::WriteNull()
	{
		AppendCommaIfNeeded();
		m_Content += "null";
		m_PendingValue = true;
	}

	void JsonWriter::WriteBool(bool value)
	{
		AppendCommaIfNeeded();
		m_Content += value ? "true" : "false";
		m_PendingValue = true;
	}

	void JsonWriter::WriteString(std::string_view value)
	{
		AppendCommaIfNeeded();
		m_Content += '"';
		for (char current : value)
		{
			switch (current)
			{
			case '"':
				m_Content += "\\\"";
				break;
			case '\\':
				m_Content += "\\\\";
				break;
			case '\b':
				m_Content += "\\b";
				break;
			case '\f':
				m_Content += "\\f";
				break;
			case '\n':
				m_Content += "\\n";
				break;
			case '\r':
				m_Content += "\\r";
				break;
			case '\t':
				m_Content += "\\t";
				break;
			default:
				if (static_cast<unsigned char>(current) < 0x20)
				{
					constexpr char HexChars[] = "0123456789abcdef";
					const unsigned char byte = static_cast<unsigned char>(current);
					m_Content += "\\u00";
					m_Content += HexChars[(byte >> 4) & 0xF];
					m_Content += HexChars[byte & 0xF];
				}
				else
				{
					m_Content += current;
				}
				break;
			}
		}
		m_Content += '"';
		m_PendingValue = true;
	}

	void JsonWriter::WriteInteger(std::int64_t value)
	{
		AppendCommaIfNeeded();
		m_Content += std::to_string(value);
		m_PendingValue = true;
	}

	void JsonWriter::WriteReal(double value)
	{
		AppendCommaIfNeeded();
		m_Content += std::to_string(value);
		m_PendingValue = true;
	}
}
