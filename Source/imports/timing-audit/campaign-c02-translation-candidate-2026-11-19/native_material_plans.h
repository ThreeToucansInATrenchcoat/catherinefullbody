#pragma once
#include <array>
#include <cstdlib>
#include <memory>
#include <map>
#include <vector>
#include "nvn_draw_state.h"

namespace nx::nvn::replay::memo87 { struct Entry; }
namespace nx::nvn::replay::materials {
inline int mode() {
    static const int value=[] { const auto* s=std::getenv("CATHERINE_VULKAN_MATERIAL_PLANS");return s?std::atoi(s):1; }();
    return value;
}
struct Recipe;
struct Execution;
using Resolver=VkDescriptorBufferInfo(*)(VkCommandBuffer,const Recipe&,Execution&);
using MemoMatch=bool(*)(const Recipe&,const memo87::Entry&,float,float);
using MemoCapture=bool(*)(const Recipe&,memo87::Entry&,float,float);
struct Destination {
    VkDescriptorType type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uint32_t set=0,binding=0,count=1;
    bool dynamic=false;
};
struct Recipe {
    // Legacy identity is only an opaque memo key / mode-2 comparator input.
    // Native resolvers do not inspect its NVN meaning.
    const DescriptorBinding* legacy=nullptr;
    const ShaderInfo* shader=nullptr; // immutable shader-cache / batch variant identity
    Destination destination;
    Resolver resolve=nullptr;
    MemoMatch memo_match=nullptr;
    MemoCapture memo_capture=nullptr;
    int stage=0,slot=-1;
    bool uniform=false,model_transform=false,computed_index=false,uniform_descriptor=false;
    bool storage=false,compute=false,shader_writes=false,palette=false;
    uint32_t exact_extent=0;
};
struct StagePlan {
    const ShaderInfo* shader=nullptr;
    std::vector<uint8_t> constants;
    std::vector<Recipe> buffers;
    std::array<uint32_t,64> extents{};
    uint32_t texture_mask[2]{},image_mask[2]{};
    bool full_slots=false,has_storage=false,has_images=false;
};
struct Operation { const Recipe* recipe=nullptr;Destination destination; };
struct LinkedPlan {
    std::vector<const StagePlan*> stages;
    std::vector<Operation> buffers;
    std::vector<const Recipe*> storage;
    std::vector<Destination> set0_copies;
    uint32_t texture_mask[2]{},image_mask[2]{};
    bool full_slots=false,has_storage=false,has_images=false;
    bool supported=true;
};
struct TemplateMetadata {
    uint32_t texture_mask[2]{},image_mask[2]{};
    bool full_slots=false,has_storage=false,has_images=false;
};
struct Census {
    uint64_t compiled=0,linked=0,writes=0,memo_hits=0,compared=0,memo_compared=0,uniform_compared=0;
    uint64_t fallback_arrays=0,template_compared=0;
};
extern Census census;
const LinkedPlan& plan(const std::vector<const ShaderInfo*>&,const ProgramLayout&);
Recipe legacy_recipe(const ShaderInfo*,const DescriptorBinding&);
TemplateMetadata template_metadata(const std::vector<const ShaderInfo*>&,const ProgramLayout&);
Range uniform_range(const Recipe&);
VkDescriptorBufferInfo uniform_info(VkCommandBuffer,const Recipe&);
bool matrix_bytes(const Recipe&,uint8_t*);
void verify_template(const LinkedPlan&,const std::vector<const ShaderInfo*>&);
}
