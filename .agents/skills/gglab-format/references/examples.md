# GGLab formatting examples

These fragments illustrate the rules in [Layout rules](layout-rules.md).
Identifiers are illustrative; preserve the edited code's actual types and APIs.

## Control flow and HLSL attributes

Keep labels and their scope braces aligned with `switch`; put the terminating
`break;` inside the scoped branch.

```cpp
switch (state)
{
case State::Ready:
{
	Publish();
	break;
}
default:
	break;
}
```

HLSL attributes align with the statement they modify. Comments inside `#if`
retain the surrounding code's indentation.

```hlsl
[unroll]
for (uint i = 0; i < sampleCount; ++i)
{
#if ENABLE_HISTORY
	// Accumulate only samples admitted by the history contract.
	if (IsHistoryValid(i))
	{
		AccumulateHistory(i);
	}
#endif
}
```

## Ternary expressions

A condition consisting of a wrapped call and comparison has one condition base.
The parameter continuation and the branches can therefore share an indentation
level. The branches do not gain another level from the parameter line.

```cpp
return ConvertUtf8(source, inputSize,
	result.data(), requiredSize) == requiredSize ?
	result :
	std::wstring{};
```

A compound condition has its own outer continuation level. Here the branches
are one level beyond the `NearAccumulation` line. The inner argument line is
not an additional basis for indenting the branches.

```hlsl
return IsEnabled() &&
	HasHistory() &&
	NearAccumulation(ComputeHistoryWeight(samples, confidence,
		capacity, sampleWeight), expected) ?
		0.0.xxxx :
		1.0.xxxx;
```

Preserve nesting in a conditional chain and inside aggregate-valued branches.

```cpp
const auto outcome = succeeded ?
	Outcome
	{
		.m_Status = Status::Ready,
		.m_Value = value,
	} :
	retryAllowed ?
		Outcome{ .m_Status = Status::Retry } :
		Outcome{ .m_Status = Status::Failed };
```

## Lambdas

An ordinary assigned lambda's braces align with its declaration.

```cpp
const auto matches = [&expected](const Result& actual) noexcept
{
	return actual == expected;
};
```

For a long argument-lambda header, keep the complete capture list together and
align the following parameter-list line with it.

```cpp
graph.AddPass("Example",
	[pipeline, bindings, source, destination, frameState]
	(const PassExecutionContext& context)
	{
		ExecutePass(context, pipeline, bindings, source, destination, frameState);
	});
```

Sibling subconditions and the independent statement after the expression return
to their own levels; a preceding lambda does not add continuation indentation.

```cpp
const bool matches =
	Check([](const Item& item) { return item.IsReady(); }) &&
	Check([](const Item& item)
		{
			return item.IsPublished();
		}) &&
	CheckCount(expectedCount);
context.Check(matches, "All publication conditions hold");
```

## Arrays and JSON

Keep uniform records complete, even when a readable entry exceeds 100 columns.
Complex records with nested fields can remain expanded.

```cpp
constexpr std::array cases
{
	Case{ Format::R8Unorm, 1, 1, 1, true },
	Case{ Format::R16G16B16A16Float, 8, 1, 1, true },
};
```

Put each JSON field on its own line. This example keeps outer closing braces
after the final entry and accepts the native initializer-brace spacing.

```cpp
return Json{
	{ "status", status },
	{ "observation",{
		{ "frameSerial", frameSerial },
		{ "slices",{ firstSlice, sliceCount } } } } };
```

## HLSL conversions

Use parentheses around both a conversion's type and its entire operand.
Component construction and bit reinterpretation retain their distinct syntax.

```hlsl
float weight = (float)(sampleCount);
float3 normalWS = mul(normalOS, (float3x3)(objData.NormalMat));
GTAOSurface surface = (GTAOSurface)(0);
float scaled = (float)(value + (offset * scale));
float3 direction = float3(x, y, z);
float4 positionCS = float4(position, 1.0);
float nonFinite = asfloat(0x7fc00000u);
```

## Raw strings and macros

Only the C++ statement indentation and whitespace outside the literal may be
adjusted here. Moving the raw string's content or closing delimiter changes
its bytes, even if the result still parses as JSON.

```cpp
void WriteFixture(std::ostream& stream)
{
	stream << R"json({
  "asset": { "version": "2.0" }
})json" << '\n';
}
```

End statement-like and declaration-like macro invocations with a semicolon at
the call site, including when their expansion already supplies one.

```cpp
GGLAB_DELETE_COPYABLE_MOVABLE(ResourceOwner);
GGLAB_UNUSED(result);
```
