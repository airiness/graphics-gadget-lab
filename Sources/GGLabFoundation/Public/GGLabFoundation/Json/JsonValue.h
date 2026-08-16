#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gglab::json
{
	struct JsonValue;

	using JsonArray = std::vector<JsonValue>;
	using JsonMember = std::pair<std::string, JsonValue>;
	using JsonObject = std::vector<JsonMember>;

	// Integer numbers preserve an exact int64_t representation; real numbers
	// (fraction or exponent) use double.
	enum class JsonNumberKind : uint8_t
	{
		Integer,
		Real,
	};

	struct JsonValue final
	{
		std::variant<std::monostate, bool, std::string, double, JsonArray, JsonObject> m_Value;
		JsonNumberKind m_NumberKind = JsonNumberKind::Real;

		[[nodiscard]] bool IsNull() const noexcept;
		[[nodiscard]] bool IsBool() const noexcept;
		[[nodiscard]] bool IsString() const noexcept;
		[[nodiscard]] bool IsNumber() const noexcept;
		[[nodiscard]] bool IsInteger() const noexcept;
		[[nodiscard]] bool IsArray() const noexcept;
		[[nodiscard]] bool IsObject() const noexcept;
		[[nodiscard]] bool AsBool() const noexcept;
		[[nodiscard]] const std::string& AsString() const noexcept;
		[[nodiscard]] std::int64_t AsInteger() const noexcept;
		[[nodiscard]] double AsReal() const noexcept;
		[[nodiscard]] const JsonArray& AsArray() const noexcept;
		[[nodiscard]] const JsonObject& AsObject() const noexcept;
	};

	// Parses a complete JSON document (RFC 8259). Returns nullopt on any
	// syntax error, invalid UTF-8 escape handling, duplicate object keys, or
	// trailing content after the document.
	[[nodiscard]] std::optional<JsonValue> ParseJsonDocument(std::string_view text) noexcept;

	// Deterministic JSON emitter: object keys keep insertion order, strings
	// use the standard escape set, and no whitespace is emitted.
	class JsonWriter final
	{
	public:
		[[nodiscard]] std::string Finish() &&;

		void BeginObject();
		void EndObject();
		void BeginArray();
		void EndArray();
		void Key(std::string_view key);
		void WriteNull();
		void WriteBool(bool value);
		void WriteString(std::string_view value);
		void WriteInteger(std::int64_t value);
		void WriteReal(double value);

	private:
		void AppendCommaIfNeeded();
		void AppendRaw(std::string_view raw);

		std::string m_Content;
		bool m_PendingValue = false;
	};
}
