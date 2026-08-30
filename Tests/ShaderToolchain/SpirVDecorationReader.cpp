#include "SpirVDecorationReader.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace gglab
{
	namespace
	{
		constexpr uint32_t SpirVMagic = 0x07230203u;
		constexpr uint16_t OpName = 5;
		constexpr uint16_t OpMemberName = 6;
		constexpr uint16_t OpEntryPoint = 15;
		constexpr uint16_t OpTypeInt = 21;
		constexpr uint16_t OpTypeFloat = 22;
		constexpr uint16_t OpTypeVector = 23;
		constexpr uint16_t OpTypeRuntimeArray = 29;
		constexpr uint16_t OpTypeStruct = 30;
		constexpr uint16_t OpTypePointer = 32;
		constexpr uint16_t OpVariable = 59;
		constexpr uint16_t OpDecorate = 71;
		constexpr uint16_t OpMemberDecorate = 72;

		constexpr uint32_t DecorationBuiltIn = 11;
		constexpr uint32_t DecorationArrayStride = 6;
		constexpr uint32_t DecorationLocation = 30;
		constexpr uint32_t DecorationBinding = 33;
		constexpr uint32_t DecorationDescriptorSet = 34;
		constexpr uint32_t DecorationOffset = 35;
		constexpr uint32_t StorageClassInput = 1;
		constexpr uint32_t StorageClassOutput = 3;

		struct DecorationInfo
		{
			std::optional<uint32_t> m_DescriptorSet;
			std::optional<uint32_t> m_Binding;
			std::optional<uint32_t> m_Location;
			std::optional<uint32_t> m_ArrayStride;
			std::optional<uint32_t> m_Offset;
			bool m_BuiltIn = false;
		};

		struct PointerTypeInfo
		{
			uint32_t m_StorageClass = 0;
			uint32_t m_PointeeType = 0;
		};

		struct VariableInfo
		{
			uint32_t m_ResultType = 0;
			uint32_t m_StorageClass = 0;
		};

		struct RawEntryPoint
		{
			uint32_t m_Id = 0;
			uint32_t m_ExecutionModel = 0;
			std::string m_Name;
			std::vector<uint32_t> m_Interfaces;
		};

		[[nodiscard]] constexpr uint64_t MemberKey(uint32_t typeId, uint32_t member) noexcept
		{
			return (static_cast<uint64_t>(typeId) << 32) | member;
		}

		[[nodiscard]] constexpr SpirVExecutionModel ToExecutionModel(uint32_t model) noexcept
		{
			switch (model)
			{
			case 0:
				return SpirVExecutionModel::Vertex;
			case 1:
				return SpirVExecutionModel::TessellationControl;
			case 2:
				return SpirVExecutionModel::TessellationEvaluation;
			case 3:
				return SpirVExecutionModel::Geometry;
			case 4:
				return SpirVExecutionModel::Fragment;
			case 5:
				return SpirVExecutionModel::Compute;
			case 5267: // TaskNV
			case 5364: // TaskEXT
				return SpirVExecutionModel::Task;
			case 5268: // MeshNV
			case 5365: // MeshEXT
				return SpirVExecutionModel::Mesh;
			default:
				return SpirVExecutionModel::Unknown;
			}
		}

		void ApplyDecoration(DecorationInfo& destination, uint32_t decoration,
			std::optional<uint32_t> value) noexcept
		{
			switch (decoration)
			{
			case DecorationBuiltIn:
				destination.m_BuiltIn = true;
				break;
			case DecorationArrayStride:
				destination.m_ArrayStride = value;
				break;
			case DecorationLocation:
				destination.m_Location = value;
				break;
			case DecorationBinding:
				destination.m_Binding = value;
				break;
			case DecorationDescriptorSet:
				destination.m_DescriptorSet = value;
				break;
			case DecorationOffset:
				destination.m_Offset = value;
				break;
			default:
				break;
			}
		}

		[[nodiscard]] bool ReadLiteralString(const uint32_t* words, uint16_t wordCount,
			uint16_t firstWord, std::string& outString, uint16_t& outNextWord) noexcept
		{
			outString.clear();
			for (uint16_t wordIndex = firstWord; wordIndex < wordCount; ++wordIndex)
			{
				const uint32_t word = words[wordIndex];
				for (uint32_t byteIndex = 0; byteIndex < sizeof(uint32_t); ++byteIndex)
				{
					const char value = static_cast<char>((word >> (byteIndex * 8)) & 0xffu);
					if (value == '\0')
					{
						outNextWord = wordIndex + 1;
						return true;
					}
					outString.push_back(value);
				}
			}
			return false;
		}

		void SortUnique(std::vector<uint32_t>& values) noexcept
		{
			std::ranges::sort(values);
			values.erase(std::unique(values.begin(), values.end()), values.end());
		}
	}

	const SpirVEntryPointReflection* SpirVDecorationReflection::FindEntryPoint(
		std::string_view name) const noexcept
	{
		const auto iterator = std::ranges::find(m_EntryPoints, name,
			&SpirVEntryPointReflection::m_Name);
		return iterator != m_EntryPoints.end() ? &*iterator : nullptr;
	}

	const SpirVStructLayoutReflection* SpirVDecorationReflection::FindStructLayout(
		std::string_view name) const noexcept
	{
		const auto iterator =
			std::ranges::find(m_StructLayouts, name, &SpirVStructLayoutReflection::m_Name);
		return iterator != m_StructLayouts.end() ? &*iterator : nullptr;
	}

	bool ReadSpirVDecorations(
		const ShaderBinary& binary, SpirVDecorationReflection& outReflection) noexcept
	{
		outReflection = {};
		if (!binary.IsValid() || binary.SizeInBytes() % sizeof(uint32_t) != 0 ||
			binary.SizeInBytes() < 5 * sizeof(uint32_t))
		{
			outReflection.m_Error = "SPIR-V binary has an invalid byte size";
			return false;
		}

		std::vector<uint32_t> words(binary.SizeInBytes() / sizeof(uint32_t));
		std::memcpy(words.data(), binary.Data(), binary.SizeInBytes());
		if (words[0] != SpirVMagic)
		{
			outReflection.m_Error = "SPIR-V binary has an invalid magic number";
			return false;
		}

		std::unordered_map<uint32_t, DecorationInfo> decorations;
		std::unordered_map<uint64_t, DecorationInfo> memberDecorations;
		std::unordered_map<uint32_t, PointerTypeInfo> pointerTypes;
		std::unordered_map<uint32_t, std::vector<uint32_t>> structTypes;
		std::unordered_map<uint32_t, uint32_t> typeSizes;
		std::unordered_map<uint32_t, uint32_t> runtimeArrayElementTypes;
		std::unordered_map<uint32_t, VariableInfo> variables;
		std::unordered_map<uint32_t, std::string> names;
		std::unordered_map<uint64_t, std::string> memberNames;
		std::vector<RawEntryPoint> rawEntryPoints;

		for (size_t offset = 5; offset < words.size();)
		{
			const uint32_t instruction = words[offset];
			const uint16_t wordCount = static_cast<uint16_t>(instruction >> 16);
			const uint16_t opcode = static_cast<uint16_t>(instruction & 0xffffu);
			if (wordCount == 0 || offset + wordCount > words.size())
			{
				outReflection.m_Error = "SPIR-V instruction exceeds the word stream";
				return false;
			}
			const uint32_t* operands = words.data() + offset;

			switch (opcode)
			{
			case OpName:
				if (wordCount >= 3)
				{
					uint16_t nextWord = 0;
					if (!ReadLiteralString(operands, wordCount, 2, names[operands[1]], nextWord))
					{
						outReflection.m_Error = "SPIR-V object name is not terminated";
						return false;
					}
				}
				break;
			case OpMemberName:
				if (wordCount >= 4)
				{
					uint16_t nextWord = 0;
					if (!ReadLiteralString(operands, wordCount, 3,
							memberNames[MemberKey(operands[1], operands[2])], nextWord))
					{
						outReflection.m_Error = "SPIR-V member name is not terminated";
						return false;
					}
				}
				break;
			case OpEntryPoint:
				if (wordCount >= 4)
				{
					RawEntryPoint entry{
						.m_Id = operands[2],
						.m_ExecutionModel = operands[1],
					};
					uint16_t interfaceWord = 0;
					if (!ReadLiteralString(operands, wordCount, 3, entry.m_Name, interfaceWord))
					{
						outReflection.m_Error = "SPIR-V entry-point name is not terminated";
						return false;
					}
					entry.m_Interfaces.assign(
						operands + interfaceWord, operands + wordCount);
					rawEntryPoints.push_back(std::move(entry));
				}
				break;
			case OpDecorate:
				if (wordCount >= 3)
				{
					ApplyDecoration(decorations[operands[1]], operands[2],
						wordCount >= 4 ? std::optional<uint32_t>(operands[3]) : std::nullopt);
				}
				break;
			case OpMemberDecorate:
				if (wordCount >= 4)
				{
					ApplyDecoration(memberDecorations[MemberKey(operands[1], operands[2])],
						operands[3], wordCount >= 5 ? std::optional<uint32_t>(operands[4]) :
						std::nullopt);
				}
				break;
			case OpTypeInt:
			case OpTypeFloat:
				if (wordCount >= 3 && operands[2] % 8u == 0u)
				{
					typeSizes[operands[1]] = operands[2] / 8u;
				}
				break;
			case OpTypeVector:
				if (wordCount == 4)
				{
					const auto componentSize = typeSizes.find(operands[2]);
					if (componentSize != typeSizes.end())
					{
						typeSizes[operands[1]] = componentSize->second * operands[3];
					}
				}
				break;
			case OpTypeStruct:
				if (wordCount >= 2)
				{
					structTypes[operands[1]] =
						std::vector<uint32_t>(operands + 2, operands + wordCount);
				}
				break;
			case OpTypeRuntimeArray:
				if (wordCount == 3)
				{
					runtimeArrayElementTypes[operands[1]] = operands[2];
				}
				break;
			case OpTypePointer:
				if (wordCount == 4)
				{
					pointerTypes[operands[1]] = {
						.m_StorageClass = operands[2],
						.m_PointeeType = operands[3],
					};
				}
				break;
			case OpVariable:
				if (wordCount >= 4)
				{
					variables[operands[2]] = {
						.m_ResultType = operands[1],
						.m_StorageClass = operands[3],
					};
				}
				break;
			default:
				break;
			}
			offset += wordCount;
		}

		for (const auto& [targetId, decoration] : decorations)
		{
			if (decoration.m_DescriptorSet && decoration.m_Binding)
			{
				outReflection.m_DescriptorBindings.push_back({
					.m_TargetId = targetId,
					.m_DescriptorSet = *decoration.m_DescriptorSet,
					.m_Binding = *decoration.m_Binding,
					});
			}
		}
		std::ranges::sort(outReflection.m_DescriptorBindings,
			[](const auto& lhs, const auto& rhs) noexcept
			{
				return std::tie(lhs.m_DescriptorSet, lhs.m_Binding, lhs.m_TargetId) <
					std::tie(rhs.m_DescriptorSet, rhs.m_Binding, rhs.m_TargetId);
			});

		for (const auto& [structId, memberTypes] : structTypes)
		{
			const auto nameIterator = names.find(structId);
			if (nameIterator == names.end())
			{
				continue;
			}
			SpirVStructLayoutReflection layout{
				.m_TypeId = structId,
				.m_Name = nameIterator->second,
			};
			layout.m_Members.reserve(memberTypes.size());
			uint32_t reflectedSize = 0;
			bool hasCompleteSize = true;
			for (uint32_t member = 0; member < memberTypes.size(); ++member)
			{
				const uint64_t key = MemberKey(structId, member);
				const auto decorationIterator = memberDecorations.find(key);
				const auto memberNameIterator = memberNames.find(key);
				if (decorationIterator == memberDecorations.end() ||
					!decorationIterator->second.m_Offset || memberNameIterator == memberNames.end())
				{
					layout.m_Members.clear();
					break;
				}
				layout.m_Members.push_back({
					.m_Name = memberNameIterator->second,
					.m_Offset = *decorationIterator->second.m_Offset,
				});
				const auto typeSize = typeSizes.find(memberTypes[member]);
				if (typeSize == typeSizes.end())
				{
					hasCompleteSize = false;
				}
				else
				{
					reflectedSize = std::max(reflectedSize,
						*decorationIterator->second.m_Offset + typeSize->second);
				}
			}
			if (hasCompleteSize && !layout.m_Members.empty())
			{
				layout.m_Size = reflectedSize;
			}
			for (const auto& [arrayTypeId, elementTypeId] : runtimeArrayElementTypes)
			{
				if (elementTypeId != structId)
				{
					continue;
				}
				const auto decorationIterator = decorations.find(arrayTypeId);
				if (decorationIterator != decorations.end())
				{
					layout.m_ArrayStride = decorationIterator->second.m_ArrayStride;
				}
				break;
			}
			if (!layout.m_Members.empty())
			{
				outReflection.m_StructLayouts.push_back(std::move(layout));
			}
		}
		std::ranges::sort(
			outReflection.m_StructLayouts, [](const auto& lhs, const auto& rhs) noexcept {
				return std::tie(lhs.m_Name, lhs.m_TypeId) < std::tie(rhs.m_Name, rhs.m_TypeId);
			});

		for (const RawEntryPoint& rawEntry : rawEntryPoints)
		{
			SpirVEntryPointReflection entry{
				.m_Id = rawEntry.m_Id,
				.m_ExecutionModel = ToExecutionModel(rawEntry.m_ExecutionModel),
				.m_Name = rawEntry.m_Name,
			};
			for (uint32_t interfaceId : rawEntry.m_Interfaces)
			{
				const auto variableIterator = variables.find(interfaceId);
				if (variableIterator == variables.end())
				{
					continue;
				}
				const VariableInfo& variable = variableIterator->second;
				std::vector<uint32_t>* locations = nullptr;
				uint32_t* builtInCount = nullptr;
				if (variable.m_StorageClass == StorageClassInput)
				{
					locations = &entry.m_InputLocations;
					builtInCount = &entry.m_InputBuiltInCount;
				}
				else if (variable.m_StorageClass == StorageClassOutput)
				{
					locations = &entry.m_OutputLocations;
					builtInCount = &entry.m_OutputBuiltInCount;
				}
				else
				{
					continue;
				}

				const auto decorationIterator = decorations.find(interfaceId);
				if (decorationIterator != decorations.end())
				{
					const DecorationInfo& decoration = decorationIterator->second;
					if (decoration.m_BuiltIn)
					{
						++*builtInCount;
						continue;
					}
					if (decoration.m_Location)
					{
						locations->push_back(*decoration.m_Location);
						continue;
					}
				}

				const auto pointerIterator = pointerTypes.find(variable.m_ResultType);
				if (pointerIterator == pointerTypes.end())
				{
					continue;
				}
				const uint32_t structId = pointerIterator->second.m_PointeeType;
				const auto structIterator = structTypes.find(structId);
				if (structIterator == structTypes.end())
				{
					continue;
				}
				for (uint32_t member = 0; member < structIterator->second.size(); ++member)
				{
					const auto memberIterator =
						memberDecorations.find(MemberKey(structId, member));
					if (memberIterator == memberDecorations.end())
					{
						continue;
					}
					const DecorationInfo& decoration = memberIterator->second;
					if (decoration.m_BuiltIn)
					{
						++*builtInCount;
					}
					else if (decoration.m_Location)
					{
						locations->push_back(*decoration.m_Location);
					}
				}
			}
			SortUnique(entry.m_InputLocations);
			SortUnique(entry.m_OutputLocations);
			outReflection.m_EntryPoints.push_back(std::move(entry));
		}

		if (outReflection.m_EntryPoints.empty())
		{
			outReflection.m_Error = "SPIR-V binary contains no entry points";
			return false;
		}
		return true;
	}
}
