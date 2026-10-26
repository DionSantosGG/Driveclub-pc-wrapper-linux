// SPDX-FileCopyrightText: Copyright 2026 Driveclub-PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <unordered_map>
#include <utility>
#include <spirv/unified1/spirv.hpp>

#include "driveclub/shader_fixes.h"

namespace DriveclubPC {

ShaderFixResult FixDeinterleaveBounds(std::span<const u32> code, u64 guest_hash, bool enabled) {
    if (guest_hash != DeinterleaveShaderHash) {
        return {};
    }
    if (!enabled) {
        return {ShaderFixStatus::Disabled, {}};
    }
    const auto mismatch = [] { return ShaderFixResult{ShaderFixStatus::SignatureMismatch, {}}; };
    if (code.size() < 5 || code[0] != spv::MagicNumber ||
        code[3] > std::numeric_limits<u32>::max() - 4) {
        return mismatch();
    }

    struct Instruction {
        size_t offset;
        u32 count;
        spv::Op op;
        u32 block;
    };
    std::vector<Instruction> instructions;
    std::unordered_map<u32, size_t> definitions;
    std::unordered_map<u32, std::pair<u32, u32>> constants;
    size_t write_offset = 0;
    u32 writes = 0, fetches = 0, comparisons = 0, entry = 0, entries = 0, block = 0;
    bool local_size = false, image_query = false, query_capability = false;
    for (size_t offset = 5; offset < code.size();) {
        const u32 count = code[offset] >> 16;
        const auto op = static_cast<spv::Op>(code[offset] & 0xffff);
        if (!count || count > code.size() - offset) {
            return mismatch();
        }
        const auto words = code.subspan(offset, count);
        switch (op) {
        case spv::OpEntryPoint:
            if (count < 4 || words[1] != spv::ExecutionModelGLCompute) {
                return mismatch();
            }
            entry = words[2];
            ++entries;
            break;
        case spv::OpExecutionMode:
            local_size |= count == 6 && words[1] == entry &&
                          words[2] == spv::ExecutionModeLocalSize && words[3] == 8 &&
                          words[4] == 8 && words[5] == 1;
            break;
        case spv::OpCapability:
            query_capability |= count == 2 && words[1] == spv::CapabilityImageQuery;
            break;
        case spv::OpTypeInt:
        case spv::OpTypeVector:
        case spv::OpTypeImage:
        case spv::OpTypePointer:
            if (count < 2) {
                return mismatch();
            }
            definitions[words[1]] = offset;
            break;
        case spv::OpLoad:
        case spv::OpVariable:
            if (count < 4) {
                return mismatch();
            }
            definitions[words[2]] = offset;
            break;
        case spv::OpConstant:
            if (count == 4) {
                constants[words[2]] = {words[1], words[3]};
            }
            break;
        case spv::OpImageFetch:
            ++fetches;
            break;
        case spv::OpImageWrite:
            if (count < 4) {
                return mismatch();
            }
            write_offset = offset;
            ++writes;
            break;
        case spv::OpImageQuerySize:
        case spv::OpImageQuerySizeLod:
            image_query = true;
            break;
        case spv::OpUGreaterThan:
            ++comparisons;
            break;
        case spv::OpLabel:
            if (count != 2) {
                return mismatch();
            }
            block = words[1];
            break;
        default:
            break;
        }
        instructions.push_back({offset, count, op, block});
        offset += count;
    }
    if (image_query) {
        return {ShaderFixStatus::AlreadyDynamic, {}};
    }
    if (entries != 1 || !local_size || fetches != 4 || writes != 1 || comparisons != 2) {
        return mismatch();
    }

    const auto definition = [&](u32 id, spv::Op op, size_t minimum) -> std::span<const u32> {
        const auto it = definitions.find(id);
        if (it == definitions.end()) {
            return {};
        }
        const auto offset = it->second;
        const auto count = code[offset] >> 16;
        return (code[offset] & 0xffff) == op && count >= minimum ? code.subspan(offset, count)
                                                                 : std::span<const u32>{};
    };
    const auto image_load = definition(code[write_offset + 1], spv::OpLoad, 4);
    if (image_load.empty()) {
        return mismatch();
    }
    const auto image_type = definition(image_load[1], spv::OpTypeImage, 9);
    const auto variable = definition(image_load[3], spv::OpVariable, 4);
    if (image_type.empty() || variable.empty() || image_type[3] != spv::Dim2D ||
        image_type[5] != 0 || image_type[6] != 0 || image_type[7] != 2 ||
        variable[3] != spv::StorageClassUniformConstant) {
        return mismatch();
    }
    const auto pointer = definition(variable[1], spv::OpTypePointer, 4);
    if (pointer.empty() || pointer[2] != spv::StorageClassUniformConstant ||
        pointer[3] != image_type[1]) {
        return mismatch();
    }

    std::array<const Instruction*, 2> bounds{};
    u32 integer_type = 0;
    for (const auto& inst : instructions) {
        if (inst.op != spv::OpUGreaterThan || inst.count != 5) {
            continue;
        }
        const auto it = constants.find(code[inst.offset + 3]);
        if (it == constants.end()) {
            return mismatch();
        }
        const auto [type, value] = it->second;
        const auto scalar = definition(type, spv::OpTypeInt, 4);
        if (scalar.empty() || scalar[2] != 32 || scalar[3] != 0 ||
            (integer_type != 0 && type != integer_type) || (value != 960 && value != 540)) {
            return mismatch();
        }
        integer_type = type;
        auto& bound = bounds[value == 960 ? 0 : 1];
        if (bound) {
            return mismatch();
        }
        bound = &inst;
    }
    if (!bounds[0] || !bounds[1] || bounds[0]->block == 0 || bounds[0]->block != bounds[1]->block) {
        return mismatch();
    }
    u32 vector_type = 0;
    for (const auto& inst : instructions) {
        if (inst.op == spv::OpTypeVector && inst.count == 4 &&
            code[inst.offset + 2] == integer_type && code[inst.offset + 3] == 2) {
            vector_type = code[inst.offset + 1];
            break;
        }
    }
    if (!vector_type) {
        return mismatch();
    }

    ShaderFixResult result{ShaderFixStatus::Fixed, {code.begin(), code.begin() + 5}};
    auto& output = result.code;
    const auto emit = [&](spv::Op op, std::initializer_list<u32> operands) {
        output.push_back((static_cast<u32>(operands.size() + 1) << 16) | op);
        output.insert(output.end(), operands);
    };
    if (!query_capability) {
        emit(spv::OpCapability, {spv::CapabilityImageQuery});
    }
    const u32 image = code[3], size = image + 1, width = image + 2, height = image + 3;
    const auto first_bound = std::min(bounds[0]->offset, bounds[1]->offset);
    for (const auto& inst : instructions) {
        if (inst.offset == first_bound) {
            emit(spv::OpLoad, {image_type[1], image, variable[2]});
            emit(spv::OpImageQuerySize, {vector_type, size, image});
            emit(spv::OpCompositeExtract, {integer_type, width, size, 0});
            emit(spv::OpCompositeExtract, {integer_type, height, size, 1});
        }
        const auto start = output.size();
        output.insert(output.end(), code.begin() + inst.offset,
                      code.begin() + inst.offset + inst.count);
        if (inst.offset == bounds[0]->offset || inst.offset == bounds[1]->offset) {
            output[start + 3] = inst.offset == bounds[0]->offset ? width : height;
        }
    }
    output[3] += 4;
    return result;
}

} // namespace DriveclubPC
