#include "backend/render/vk_backend.h"
#include "backend/render/vk_factory.h"
#include "backend/render/vk_config.h"
#include "backend/render/vk_types.h"
#include "backend/render/vk_wrappers.h"
#include "backend/render/vk_object.h"
#include "backend/render/vk_commander.h"
#include "obj/parser.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <chrono>
#include <string>
#include <limits>

namespace vk {

namespace {

VkFormat FindSupportedFormat(const std::vector<VkFormat>& formats, VkPhysicalDevice physical_device, VkImageTiling tiling, VkFormatFeatureFlags features) {
  for (const VkFormat format : formats) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    }
    if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }
  throw Error("failed to find supported format");
}

inline VkFormat FindDepthFormat(VkPhysicalDevice physical_device) {
  return FindSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
      physical_device,
      VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
  );
}

inline Image CreateDepthImage(VkDevice logical_device, VkPhysicalDevice physical_device, VkExtent2D extent) {
  VkFormat depth_format = FindDepthFormat(physical_device);

  Image depth_image(logical_device, physical_device, extent, config::GetImageSettings().channels, depth_format,  VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
  depth_image.Allocate(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  depth_image.Bind();
  depth_image.CreateView(VK_IMAGE_ASPECT_DEPTH_BIT);

  return depth_image;
}

void UpdateDescriptorSets(VkDevice logical_device, VkBuffer ubo_buffer, const std::vector<Image>& images, VkDescriptorSet descriptor_set) {
  VkDescriptorBufferInfo buffer_info = {};
  buffer_info.buffer = ubo_buffer;
  buffer_info.offset = 0;
  buffer_info.range = sizeof(UniformBufferObject);

  std::vector<VkDescriptorImageInfo> image_infos(images.size());
  for(size_t i = 0; i < images.size(); ++i) {
    image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[i].imageView = images[i].GetView();
    image_infos[i].sampler = images[i].GetSampler();
  }

  std::array<VkWriteDescriptorSet, 2> descriptor_writes = {};

  descriptor_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_writes[0].dstSet = descriptor_set;
  descriptor_writes[0].dstBinding = 0;
  descriptor_writes[0].dstArrayElement = 0;
  descriptor_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptor_writes[0].descriptorCount = 1;
  descriptor_writes[0].pBufferInfo = &buffer_info;

  descriptor_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptor_writes[1].dstSet = descriptor_set;
  descriptor_writes[1].dstBinding = 1;
  descriptor_writes[1].dstArrayElement = 0;
  descriptor_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptor_writes[1].descriptorCount = image_infos.size();
  descriptor_writes[1].pImageInfo = image_infos.data();

  vkUpdateDescriptorSets(logical_device, descriptor_writes.size(), descriptor_writes.data(), 0, nullptr);
}

} // namespace

class BackendImpl {
 public:
  explicit BackendImpl(GLFWwindow* window);
  ~BackendImpl();

  void Render();
  void LoadModel(const std::string& path);
 private:
  void RecreateSwapchain();
  void UpdateUniforms() const;

  void RecordCommandBuffer(VkCommandBuffer cmd_buffer, size_t image_idx);

  bool framebuffer_resized_;
  size_t curr_frame_;

  GLFWwindow* window_;

  HandleWrapper<VkInstance> instance_wrapper_;
#ifdef DEBUG
  HandleWrapper<VkDebugUtilsMessengerEXT> messenger_wrapper_;
#endif // DEBUG
  HandleWrapper<VkSurfaceKHR> surface_wrapper_;

  VkPhysicalDevice physical_device_;
  QueueFamilyIndices family_indices_;
  HandleWrapper<VkDevice> logical_device_wrapper_;
  VkQueue graphics_queue_, present_queue_;

  HandleWrapper<VkSwapchainKHR> swapchain_wrapper_;
  SwapchainDetails swapchain_details_;
  std::vector<VkImage> swapchain_images_;
  std::vector<HandleWrapper<VkImageView>> image_views_wrapped_;
  std::vector<HandleWrapper<VkFramebuffer>> framebuffers_wrapped_;

  HandleWrapper<VkRenderPass> render_pass_wrapper_;
  HandleWrapper<VkPipelineLayout> pipeline_layout_wrapper_;
  HandleWrapper<VkPipeline> pipeline_wrapper_;

  HandleWrapper<VkCommandPool> cmd_pool_wrapper_;
  std::vector<VkCommandBuffer> cmd_buffers_;

  std::vector<HandleWrapper<VkSemaphore>> image_semaphores_wrapped_;
  std::vector<HandleWrapper<VkSemaphore>> render_semaphores_wrapped_;
  std::vector<HandleWrapper<VkFence>> fences_wrapped_;

  HandleWrapper<VkDescriptorSetLayout> descriptor_set_layout_wrapper_;
  std::vector<VkDescriptorSet> descriptor_sets_;
  HandleWrapper<VkDescriptorPool> descriptor_pool_wrapper_;

  Image depth_image_;

  Object object_;
  ObjectLoader object_loader_;

  std::vector<UniformBufferObject*> ubo_buffers_mapped_;
};

BackendImpl::BackendImpl(GLFWwindow* window)
  : framebuffer_resized_(false), curr_frame_(), window_(window) {
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width[[maybe_unused]], int height[[maybe_unused]]) {
    auto impl = static_cast<BackendImpl*>(glfwGetWindowUserPointer(window));
    impl->framebuffer_resized_ = true;
  });

  instance_wrapper_ = factory::CreateInstance();
  VkInstance instance = instance_wrapper_.get();

#ifdef DEBUG
  messenger_wrapper_ = factory::CreateMessenger(instance);
#endif // DEBUG
  surface_wrapper_ = factory::CreateSurface(instance, window_);
  VkSurfaceKHR surface = surface_wrapper_.get();

  std::tie(physical_device_, family_indices_) = factory::CreatePhysicalDevice(instance, surface);
  logical_device_wrapper_ = factory::CreateLogicalDevice(physical_device_, family_indices_);
  VkDevice logical_device = logical_device_wrapper_.get();

  vkGetDeviceQueue(logical_device, family_indices_.graphic, 0, &graphics_queue_);
  vkGetDeviceQueue(logical_device, family_indices_.present, 0, &present_queue_);

  std::tie(swapchain_wrapper_, swapchain_details_) = factory::CreateSwapchain(window_, surface, physical_device_, family_indices_, logical_device);
  VkSwapchainKHR swapchain = swapchain_wrapper_.get();
  swapchain_images_ = factory::CreateSwapchainImages(swapchain, logical_device);

  depth_image_ = CreateDepthImage(logical_device, physical_device_, swapchain_details_.extent);

  render_pass_wrapper_ = factory::CreateRenderPass(logical_device, swapchain_details_.format, depth_image_.GetFormat());
  VkRenderPass render_pass = render_pass_wrapper_.get();

  descriptor_set_layout_wrapper_ = factory::CreateDescriptorSetLayout(logical_device);
  VkDescriptorSetLayout descriptor_set_layout = descriptor_set_layout_wrapper_.get();

  descriptor_pool_wrapper_ = factory::CreateDescriptorPool(logical_device, config::kFrameCount);
  VkDescriptorPool descriptor_pool = descriptor_pool_wrapper_.get();

  descriptor_sets_ = factory::CreateDescriptorSets(logical_device, descriptor_set_layout, descriptor_pool, config::kFrameCount);

  pipeline_layout_wrapper_ = factory::CreatePipelineLayout(logical_device, descriptor_set_layout);

  VkPipelineLayout pipeline_layout = pipeline_layout_wrapper_.get();
  pipeline_wrapper_ = factory::CreatePipeline(logical_device, pipeline_layout, render_pass,
  {
      {
          VK_SHADER_STAGE_VERTEX_BIT,
          factory::CreateShaderModule(logical_device, "../build/shaders/vert.spv"),
          "main"
      },
      {
          VK_SHADER_STAGE_FRAGMENT_BIT,
          factory::CreateShaderModule(logical_device, "../build/shaders/frag.spv"),
          "main"
      }
    }
  );
  image_views_wrapped_ = factory::CreateImageViews(swapchain_images_, logical_device, swapchain_details_.format);
  framebuffers_wrapped_ = factory::CreateFramebuffers(logical_device, image_views_wrapped_, depth_image_.GetView(), render_pass, swapchain_details_.extent);

  cmd_pool_wrapper_ = factory::CreateCommandPool(logical_device, family_indices_);
  VkCommandPool cmd_pool = cmd_pool_wrapper_.get();

  cmd_buffers_ = factory::CreateCommandBuffers(logical_device, cmd_pool_wrapper_.get(), config::kFrameCount);

  image_semaphores_wrapped_.reserve(config::kFrameCount);
  render_semaphores_wrapped_.reserve(config::kFrameCount);
  fences_wrapped_.reserve(config::kFrameCount);

  for(size_t i = 0; i < config::kFrameCount; ++i) {
    image_semaphores_wrapped_.emplace_back(factory::CreateSemaphore(logical_device));
    render_semaphores_wrapped_.emplace_back(factory::CreateSemaphore(logical_device));
    fences_wrapped_.emplace_back(factory::CreateFence(logical_device));
  }

  object_loader_ = ObjectLoader(logical_device, physical_device_, cmd_pool, graphics_queue_);
}

void BackendImpl::LoadModel(const std::string& path) {
  VkDevice logical_device = logical_device_wrapper_.get();
  VkCommandPool cmd_pool = cmd_pool_wrapper_.get();

  object_ = object_loader_.Load(path);

  ubo_buffers_mapped_.reserve(object_.ubo_buffers.size());
  for(size_t i = 0; i < object_.ubo_buffers.size(); ++i) {
    UpdateDescriptorSets(logical_device, object_.ubo_buffers[i].Get(), object_.textures, descriptor_sets_[i]);

    auto ubo_buffer_mapped = static_cast<UniformBufferObject*>(object_.ubo_buffers[i].Map());
    ubo_buffers_mapped_.emplace_back(ubo_buffer_mapped);
  }
}

void BackendImpl::UpdateUniforms() const {
  static const std::chrono::time_point start_time = std::chrono::high_resolution_clock::now();

  const std::chrono::time_point curr_time = std::chrono::high_resolution_clock::now();
  const float time = std::chrono::duration<float>(curr_time - start_time).count();

  UniformBufferObject* ubo = ubo_buffers_mapped_[curr_frame_];
  ubo->model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo->view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo->proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapchain_details_.extent.width) / swapchain_details_.extent.height, 0.1f, 10.0f);
  ubo->proj[1][1] *= -1;
}

void BackendImpl::RecreateSwapchain() {
  int width = 0, height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window_, &width, &height);
    glfwWaitEvents();
  }
  VkDevice logical_device = logical_device_wrapper_.get();
  VkRenderPass render_pass = render_pass_wrapper_.get();
  VkSurfaceKHR surface = surface_wrapper_.get();

  if (const VkResult result = vkDeviceWaitIdle(logical_device); result != VK_SUCCESS) {
    throw Error("failed to idle device");
  }

  framebuffers_wrapped_.clear();
  image_views_wrapped_.clear();
  swapchain_wrapper_.reset();

  std::tie(swapchain_wrapper_, swapchain_details_) = factory::CreateSwapchain(window_, surface, physical_device_, family_indices_, logical_device);
  swapchain_images_ = factory::CreateSwapchainImages(swapchain_wrapper_.get(), logical_device);

  image_views_wrapped_ = factory::CreateImageViews(swapchain_images_, logical_device, swapchain_details_.format);

  depth_image_ = CreateDepthImage(logical_device, physical_device_, swapchain_details_.extent);

  framebuffers_wrapped_ = factory::CreateFramebuffers(logical_device, image_views_wrapped_, depth_image_.GetView(), render_pass, swapchain_details_.extent);
}

void BackendImpl::RecordCommandBuffer(VkCommandBuffer cmd_buffer, size_t image_idx) {
  VkRenderPass render_pass = render_pass_wrapper_.get();
  VkPipelineLayout pipeline_layout = pipeline_layout_wrapper_.get();
  VkPipeline pipeline = pipeline_wrapper_.get();

  VkCommandBufferBeginInfo cmd_buffer_begin_info = {};
  cmd_buffer_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (const VkResult result = vkBeginCommandBuffer(cmd_buffer, &cmd_buffer_begin_info); result != VK_SUCCESS) {
    throw Error("failed to begin recording command buffer").WithCode(result);
  }
  std::array<VkClearValue, 2> clear_values = {};
  clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  clear_values[1].depthStencil = {1.0f, 0};

  VkRenderPassBeginInfo render_pass_begin_info = {};
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.renderPass = render_pass;
  render_pass_begin_info.framebuffer = framebuffers_wrapped_[image_idx].get();
  render_pass_begin_info.renderArea.offset = {0, 0};
  render_pass_begin_info.renderArea.extent = swapchain_details_.extent;
  render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
  render_pass_begin_info.pClearValues = clear_values.data();
  vkCmdBeginRenderPass(cmd_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapchain_details_.extent.width);
  viewport.height = static_cast<float>(swapchain_details_.extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swapchain_details_.extent;
  vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

  VkBuffer vertices_buffer = object_.vertices.Get();
  VkBuffer indices_buffer = object_.indices.Get();
  VkDescriptorSet descriptor_set = descriptor_sets_[curr_frame_];

  VkDeviceSize prev_offset = 0;
  std::array<VkDeviceSize, 1> vertex_offsets = {};

  for(const auto[index, offset] : object_.usemtl) {
    const VkDeviceSize curr_offset = prev_offset * sizeof(Index::type);

    vkCmdPushConstants(cmd_buffer, pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(unsigned int), &index);
    vkCmdBindVertexBuffers(cmd_buffer, 0, vertex_offsets.size(), &vertices_buffer, vertex_offsets.data());
    vkCmdBindIndexBuffer(cmd_buffer, indices_buffer, curr_offset, Index::type_enum);
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(cmd_buffer, static_cast<uint32_t>(offset - prev_offset), 1, 0, 0, 0);

    prev_offset = offset;
  }
  vkCmdEndRenderPass(cmd_buffer);
  if (const VkResult result = vkEndCommandBuffer(cmd_buffer); result != VK_SUCCESS) {
    throw Error("failed to record command buffer").WithCode(result);
  }
}

void BackendImpl::Render() {
  uint32_t image_idx;

  VkDevice logical_device = logical_device_wrapper_.get();

  VkFence fence = fences_wrapped_[curr_frame_].get();
  VkSemaphore image_semaphore = image_semaphores_wrapped_[curr_frame_].get();
  VkSemaphore render_semaphore = render_semaphores_wrapped_[curr_frame_].get();

  VkSwapchainKHR swapchain = swapchain_wrapper_.get();

  VkCommandBuffer cmd_buffer = cmd_buffers_[curr_frame_];

  if (const VkResult result = vkWaitForFences(logical_device, 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()); result != VK_SUCCESS) {
    throw Error("failed to wait for fences").WithCode(result);
  }
  if (const VkResult result = vkAcquireNextImageKHR(logical_device, swapchain, std::numeric_limits<uint64_t>::max(), image_semaphore, VK_NULL_HANDLE, &image_idx); result != VK_SUCCESS) {
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      RecreateSwapchain();
      return;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
      throw Error("failed to acquire next image").WithCode(result);
    }
  }
  UpdateUniforms();
  if (const VkResult result = vkResetFences(logical_device, 1, &fence); result != VK_SUCCESS) {
    throw Error("failed to reset fences").WithCode(result);
  }
  if (const VkResult result = vkResetCommandBuffer(cmd_buffer, 0); result != VK_SUCCESS) {
    throw Error("failed to reset command buffer").WithCode(result);
  }
  RecordCommandBuffer(cmd_buffer, image_idx);

  const std::vector<VkPipelineStageFlags> pipeline_stage_flags = config::GetPipelineStageFlags();

  VkSubmitInfo submit_info = {};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = &image_semaphore;
  submit_info.pWaitDstStageMask = pipeline_stage_flags.data();
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd_buffer;
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = &render_semaphore;

  if (const VkResult result = vkQueueSubmit(graphics_queue_, 1, &submit_info, fence); result != VK_SUCCESS) {
    throw Error("failed to submit draw command buffer").WithCode(result);
  }

  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_semaphore;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain;
  present_info.pImageIndices = &image_idx;

  const VkResult result = vkQueuePresentKHR(present_queue_, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized_) {
    framebuffer_resized_ = false;
    RecreateSwapchain();
  } else if (result != VK_SUCCESS) {
    throw Error("failed to queue present").WithCode(result);
  }
  curr_frame_ = (curr_frame_ + 1) % config::kFrameCount;
}

BackendImpl::~BackendImpl() {
  vkDeviceWaitIdle(logical_device_wrapper_.get());
}

Backend::Backend(GLFWwindow* window)
  : impl_(new BackendImpl(window))  {}

Backend::~Backend() { delete impl_; }

void Backend::Render() const {
  impl_->Render();
}

void Backend::LoadModel(const std::string& path) {
  impl_->LoadModel(path);
}

} // vk