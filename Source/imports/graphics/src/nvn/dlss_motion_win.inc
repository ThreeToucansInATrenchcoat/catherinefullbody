// GPU-owned temporal inputs, never guest images whose lifetime ends at submit.
// Included inside nx::nvn's Windows backend namespace.
using namespace nx::vk;
#include "dlss_motion_shader.inc"
struct TemporalInputs {
 VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;
 VkPipeline pipeline=VK_NULL_HANDLE;VkSampler sampler=VK_NULL_HANDLE;
 GpuImage motion,depth,history[2];unsigned current=0;bool history_valid=false,failed=false;
 VkImageView depth_view=VK_NULL_HANDLE;VkImage depth_source=VK_NULL_HANDLE;
} temporal_inputs;
bool temporal_image(GpuImage& image,unsigned width,unsigned height,VkFormat format,VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT){
 VkDevice device=vk_device();image.width=width;image.height=height;image.format=format;image.aspect=aspect;
 VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};create.imageType=VK_IMAGE_TYPE_2D;create.format=format;
 create.extent={width,height,1};create.mipLevels=create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;
 create.tiling=VK_IMAGE_TILING_OPTIMAL;create.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;if(aspect==VK_IMAGE_ASPECT_COLOR_BIT)create.usage|=VK_IMAGE_USAGE_STORAGE_BIT;
 if(p_vkCreateImage(device,&create,nullptr,&image.image)!=VK_SUCCESS)return false;
 VkMemoryRequirements requirements{};p_vkGetImageMemoryRequirements(device,image.image,&requirements);
 VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
 if(p_vkAllocateMemory(device,&allocate,nullptr,&image.memory)!=VK_SUCCESS||p_vkBindImageMemory(device,image.image,image.memory,0)!=VK_SUCCESS)return false;
 VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={aspect,0,1,0,1};
 return p_vkCreateImageView(device,&view,nullptr,&image.view)==VK_SUCCESS;
}
void temporal_destroy_image(GpuImage& image){
 VkDevice device=vk_device();if(image.view)p_vkDestroyImageView(device,image.view,nullptr);
 if(image.image)p_vkDestroyImage(device,image.image,nullptr);if(image.memory)p_vkFreeMemory(device,image.memory,nullptr);image={};
}
bool prepare_temporal(unsigned width,unsigned height){
 auto& t=temporal_inputs;if(t.failed)return false;VkDevice device=vk_device();
 if(!t.pipeline){
  VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sampler.minFilter=sampler.magFilter=VK_FILTER_NEAREST;
  sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  if(p_vkCreateSampler(device,&sampler,nullptr,&t.sampler)!=VK_SUCCESS)return false;
  VkDescriptorSetLayoutBinding bindings[6]{};
  for(unsigned i=0;i<6;++i){bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].descriptorType=i<3?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;}
  VkDescriptorSetLayoutCreateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};set.bindingCount=6;set.pBindings=bindings;
  if(p_vkCreateDescriptorSetLayout(device,&set,nullptr,&t.descriptors)!=VK_SUCCESS)return false;
  VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,80};
  VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&t.descriptors;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&push;
  if(p_vkCreatePipelineLayout(device,&layout,nullptr,&t.layout)!=VK_SUCCESS)return false;
  VkShaderModule shader=VK_NULL_HANDLE;VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=sizeof(kMotionShader);module.pCode=kMotionShader;
  if(p_vkCreateShaderModule(device,&module,nullptr,&shader)!=VK_SUCCESS)return false;
  VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=t.layout;
  pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=shader;pipeline.stage.pName="main";
  VkResult result=p_vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline,nullptr,&t.pipeline);p_vkDestroyShaderModule(device,shader,nullptr);
  if(result!=VK_SUCCESS)return false;
 }
 if(t.motion.image&&t.depth.image&&t.history[0].image&&t.history[1].image&&t.motion.width==width&&t.motion.height==height)return true;
 p_vkDeviceWaitIdle(device);temporal_destroy_image(t.motion);temporal_destroy_image(t.depth);for(auto& image:t.history)temporal_destroy_image(image);
 t.history_valid=false;
 return temporal_image(t.motion,width,height,VK_FORMAT_R16G16_SFLOAT)&&temporal_image(t.depth,width,height,VK_FORMAT_R32_SFLOAT)&&temporal_image(t.history[0],width,height,VK_FORMAT_R8G8B8A8_UNORM)&&temporal_image(t.history[1],width,height,VK_FORMAT_R8G8B8A8_UNORM);
}
bool record_temporal(VkCommandBuffer cb,GpuImage& color,GpuImage& depth,const temporal::Matrix& clip_to_previous,bool reset){
 const unsigned width=std::max(160u,color.width/2),height=std::max(100u,color.height/2);
 if(!prepare_temporal(width,height)){if(!temporal_inputs.failed)std::fprintf(stderr,"[DLSS-G] Cannot create temporal GPU inputs; frame generation disabled.\n");temporal_inputs.failed=true;return false;}
 auto& t=temporal_inputs;
 if(t.depth_source!=depth.image){
  p_vkDeviceWaitIdle(vk_device());if(t.depth_view)p_vkDestroyImageView(vk_device(),t.depth_view,nullptr);
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=depth.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=depth.format;view.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
  if(p_vkCreateImageView(vk_device(),&view,nullptr,&t.depth_view)!=VK_SUCCESS)return false;t.depth_source=depth.image;
 }
 transition(cb,color,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);transition(cb,depth,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 for(auto& image:t.history)if(image.layout==VK_IMAGE_LAYOUT_UNDEFINED){transition(cb,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue black{};VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};p_vkCmdClearColorImage(cb,image.image,image.layout,&black,1,&range);}
 GpuImage& current=t.history[t.current],&previous=t.history[t.current^1];
 transition(cb,previous,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 transition(cb,current,VK_IMAGE_LAYOUT_GENERAL);transition(cb,t.motion,VK_IMAGE_LAYOUT_GENERAL);transition(cb,t.depth,VK_IMAGE_LAYOUT_GENERAL);
 VkDescriptorImageInfo images[6]={{t.sampler,color.view,color.layout},{t.sampler,previous.view,previous.layout},{t.sampler,t.depth_view,depth.layout},{VK_NULL_HANDLE,t.motion.view,t.motion.layout},{VK_NULL_HANDLE,t.depth.view,t.depth.layout},{VK_NULL_HANDLE,current.view,current.layout}};
 VkDescriptorSet descriptor=descriptor_set(t.descriptors);VkWriteDescriptorSet writes[6]{};
 for(unsigned i=0;i<6;++i){writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dstSet=descriptor;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i<3?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&images[i];}
 p_vkUpdateDescriptorSets(vk_device(),6,writes,0,nullptr);
 struct Push{float matrix[16],dimensions[4];} push{};
 // GLSL matrix storage is column-major; camera helper matrices are row-major.
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)push.matrix[c*4+r]=clip_to_previous[r*4+c];
 push.dimensions[0]=float(width);push.dimensions[1]=float(height);push.dimensions[2]=t.history_valid&&!reset?1.f:0.f;
 p_vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,t.pipeline);p_vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,t.layout,0,1,&descriptor,0,nullptr);
 p_vkCmdPushConstants(cb,t.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);p_vkCmdDispatch(cb,(width+7)/8,(height+7)/8,1);
 transition(cb,t.motion,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);transition(cb,t.depth,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);transition(cb,current,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
 t.current^=1;t.history_valid=true;return true;
}
sl::Resource temporal_resource(const GpuImage& image){
 sl::Resource resource(sl::ResourceType::eTex2d,reinterpret_cast<void*>(image.image),reinterpret_cast<void*>(image.memory),reinterpret_cast<void*>(image.view),image.layout);
 resource.width=image.width;resource.height=image.height;resource.nativeFormat=image.format;resource.mipLevels=resource.arrayLayers=1;
 resource.flags=0;resource.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 return resource;
}
