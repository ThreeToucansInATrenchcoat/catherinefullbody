// Exact static read extent of the translator's simple vec4[4096] uniform bank.
// Any unrecognized declaration or pointer use keeps the existing conservative padding.
using UniformExtents = std::array<uint32_t, 64>;

UniformExtents uniform_extents(const ShaderInfo& shader) {
    const auto& code = shader.spirv;
    UniformExtents out{};
    if (code.size() < 5 || code[0] != 0x07230203 || code[3] > (1u << 20)) return out;
    constexpr uint32_t TypeInt = 21, TypeFloat = 22, TypeVector = 23, TypeArray = 28, TypeStruct = 30;
    constexpr uint32_t TypePointer = 32, Constant = 43, ConstantNull = 46, Function = 54, Variable = 59;
    constexpr uint32_t Load = 61, AccessChain = 65, InBoundsAccessChain = 66, Decorate = 71, MemberDecorate = 72;
    constexpr uint32_t Block = 2, ArrayStride = 6, Binding = 33, DescriptorSet = 34, Offset = 35, Uniform = 2;
    struct Definition {
        const uint32_t* words = nullptr;
        uint32_t op = 0, count = 0;
        int set = -1, binding = -1;
        uint32_t stride = 0, member_offset = UINT32_MAX;
        bool block = false;
    };
    std::vector<Definition> ids(code[3]);
    size_t first_function = code.size();
    for (size_t i = 5; i < code.size();) {
        const uint32_t* w = code.data() + i;
        const uint32_t n = w[0] >> 16, op = w[0] & 0xffff;
        if (!n || n > code.size() - i) return {};
        if (op == Function) {
            first_function = i;
            break;
        }
        uint32_t id = 0;
        if (op >= TypeInt && op <= TypePointer && n >= 2) id = w[1];
        if ((op == Variable || op == Constant || op == ConstantNull) && n >= 3) id = w[2];
        if (id >= ids.size()) return {};
        if (id) ids[id].words = w, ids[id].op = op, ids[id].count = n;
        if (op == Decorate && n >= 3 && w[1] < ids.size()) {
            auto& d = ids[w[1]];
            if (w[2] == Block) d.block = true;
            if (n == 4 && w[2] == ArrayStride) d.stride = w[3];
            if (n == 4 && w[2] == DescriptorSet) d.set = int(w[3]);
            if (n == 4 && w[2] == Binding) d.binding = int(w[3]);
        }
        if (op == MemberDecorate && n == 5 && w[1] < ids.size() && w[2] == 0 && w[3] == Offset)
            ids[w[1]].member_offset = w[4];
        i += n;
    }
    auto definition = [&](uint32_t id, uint32_t op, uint32_t count) -> const Definition* {
        if (!id || id >= ids.size()) return nullptr;
        const auto& d = ids[id];
        return d.op == op && d.count == count ? &d : nullptr;
    };
    auto integer = [&](uint32_t id) -> uint32_t {
        if (!id || id >= ids.size()) return UINT32_MAX;
        const auto& c = ids[id];
        if ((c.op != Constant || c.count != 4) && (c.op != ConstantNull || c.count != 3)) return UINT32_MAX;
        const auto* t = definition(c.words[1], TypeInt, 4);
        return t && t->words[2] == 32 ? (c.op == ConstantNull ? 0 : c.words[3]) : UINT32_MAX;
    };
    struct Pointer {
        uint8_t binding = 0;
        bool watched = false, root = false;
    };
    std::vector<Pointer> pointers(ids.size());
    std::array<bool, 64> rejected{};
    size_t active = 0;
    for (const auto& b : shader.refl.bindings) {
        if (b.set != 0 || b.binding >= out.size() || b.count != 1 ||
            b.nvn.source != NvnSource::UniformBlock || VkDescriptorType(b.type) != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) continue;
        bool found = false;
        for (size_t id = 1; id < ids.size(); ++id) {
            const auto& v = ids[id];
            if (v.op != Variable || v.set != 0 || v.binding != int(b.binding)) continue;
            if (found || v.count != 4 || v.words[3] != Uniform) {
                rejected[b.binding] = true;
                break;
            }
            found = true;
            const auto* pointer = definition(v.words[1], TypePointer, 4);
            const auto* structure = pointer && pointer->words[2] == Uniform
                                        ? definition(pointer->words[3], TypeStruct, 3) : nullptr;
            const auto* array = structure && structure->block && structure->member_offset == 0
                                    ? definition(structure->words[2], TypeArray, 4) : nullptr;
            const auto* vector = array && array->stride == 16 && integer(array->words[3]) == 4096
                                     ? definition(array->words[2], TypeVector, 4) : nullptr;
            if (!vector || vector->words[3] != 4) {
                rejected[b.binding] = true;
                break;
            }
            const auto* scalar = definition(vector->words[2], TypeInt, 4);
            if (!scalar) scalar = definition(vector->words[2], TypeFloat, 3);
            if (!scalar || scalar->words[2] != 32) {
                rejected[b.binding] = true;
                break;
            }
            pointers[id] = {uint8_t(b.binding), true, true};
            ++active;
        }
    }
    if (!active) return out;
    for (size_t i = first_function; i < code.size();) {
        const uint32_t* w = code.data() + i;
        const uint32_t n = w[0] >> 16, op = w[0] & 0xffff;
        if (!n || n > code.size() - i) return {};
        bool direct = false;
        if ((op == AccessChain || op == InBoundsAccessChain) && n >= 4 && w[3] < pointers.size()) {
            auto base = pointers[w[3]];
            if (base.watched && base.root && (n == 6 || n == 7) && w[2] < pointers.size() &&
                integer(w[4]) == 0 && integer(w[5]) < 4096 && (n == 6 || integer(w[6]) < 4)) {
                out[base.binding] = std::max(out[base.binding], (integer(w[5]) + 1) * 16);
                direct = true;
            }
        }
        // A pointer escaping a direct load/chain invalidates the proof. Treating a literal collision as a use
        // only keeps extra padding. IDs defined by this instruction are added after checking its operands.
        for (uint32_t j = 1; j < n; ++j) {
            if (w[j] >= pointers.size()) continue;
            const auto p = pointers[w[j]];
            if (!p.watched) continue;
            if (j == 3 && (direct || (op == Load && !p.root))) continue;
            rejected[p.binding] = true;
        }
        if (direct) pointers[w[2]] = {pointers[w[3]].binding, true, false};
        i += n;
    }
    for (size_t b = 0; b < out.size(); ++b)
        if (rejected[b]) out[b] = 0;
    return out;
}

const UniformExtents* shader_uniform_extents(const ShaderInfo* shader) {
    static const bool enabled = [] {
        const char* setting = std::getenv("NX_UNIFORM_STATIC_EXTENT");
        return setting && setting[0] != '0';
    }();
    if (!enabled || draw_slow(kSlowUploads)) return nullptr;
    static std::unordered_map<const ShaderInfo*, UniformExtents> cache;
    auto it = cache.find(shader);
    if (it == cache.end()) it = cache.emplace(shader, uniform_extents(*shader)).first;
    return &it->second;
}

VkDescriptorBufferInfo zero_uniform(uint64_t size) {
    // Power-of-two sizes occupy disjoint immutable extents and therefore dedup separately.
    static const std::array<uint8_t, 0x20000> zeros{};
    Upload u = dedup_upload(zeros.data() + size, size, size, 256);
    return {u.buffer, u.offset, size};
}
