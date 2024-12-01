#include "backend/render/vk/render.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <array>
#include <chrono>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <string>

#include "backend/render/vk/config.h"
#include "backend/render/vk/error.h"

namespace vk {

namespace {

void UpdateDescriptorSets(VkDevice logical_device, VkBuffer ubo_buffer, const std::vector<Device::Dispatchable<VkImage>>& images, VkDescriptorSet descriptor_set) {
  VkDescriptorBufferInfo buffer_info = {};
  buffer_info.buffer = ubo_buffer;
  buffer_info.offset = 0;
  buffer_info.range = sizeof(UniformBufferObject);

  std::vector<VkDescriptorImageInfo> image_infos(images.size());
  for(size_t i = 0; i < images.size(); ++i) {
    image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_infos[i].imageView = images[i].View().Handle();
    image_infos[i].sampler = images[i].Sampler().Handle();
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

Render::Render(GLFWwindow* window)
  : framebuffer_resized_(false), curr_frame_(), window_(window) {
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, int width[[maybe_unused]], int height[[maybe_unused]]) {
    auto render = static_cast<Render*>(glfwGetWindowUserPointer(window));
    render->framebuffer_resized_ = true;
  });

#ifdef DEBUG
  messenger_ = instance_.CreateMessenger();
#endif // DEBUG

  surface_ = instance_.CreateSurface(window);

  for(VkPhysicalDevice physical_device : instance_.EnumeratePhysicalDevices()) {
    if (auto[suitable, indices] = Device::PhysicalDeviceIsSuitable(physical_device, surface_.Handle()); suitable) {
      device_ = Device(physical_device, indices);
      break;
    }
  }
  swapchain_ = device_.CreateSwapchain(window, surface_.Handle());

  render_pass_ = device_.CreateRenderPass(swapchain_.ImageFormat(), swapchain_.DepthImageFormat());

  framebuffers_ = swapchain_.CreateFramebuffers(render_pass_.Handle());

  descriptor_set_layout_ = device_.CreateDescriptorSetLayout();
  descriptor_pool_ = device_.CreateDescriptorPool(config::kFrameCount);

  descriptor_sets_ = device_.CreateDescriptorSets(descriptor_set_layout_.Handle(), descriptor_pool_.Handle(), config::kFrameCount);
  pipeline_layout_ = device_.CreatePipelineLayout(descriptor_set_layout_.Handle());
  pipeline_ = device_.CreatePipeline(pipeline_layout_.Handle(), render_pass_.Handle(), Vertex::GetAttributeDescriptions(), Vertex::GetBindingDescriptions(), {
      {
          VK_SHADER_STAGE_VERTEX_BIT,
           device_.CreateShaderModule("../build/shaders/vert.spv"),
          "main"
      },
      {
          VK_SHADER_STAGE_FRAGMENT_BIT,
          device_.CreateShaderModule("../build/shaders/frag.spv"),
          "main"
      }
  });

  cmd_pool_ = device_.CreateCommandPool();
  cmd_buffers_ = device_.CreateCommandBuffers(cmd_pool_.Handle(), config::kFrameCount);

  image_semaphores_.reserve(config::kFrameCount);
  render_semaphores_.reserve(config::kFrameCount);
  fences_.reserve(config::kFrameCount);

  for(size_t i = 0; i < config::kFrameCount; ++i) {
    image_semaphores_.emplace_back(device_.CreateSemaphore());
    render_semaphores_.emplace_back(device_.CreateSemaphore());
    fences_.emplace_back(device_.CreateFence());
  }

  object_loader_ = ObjectLoader(&device_, cmd_pool_.Handle());
}

void Render::LoadModel(const std::string& path) {
  object_ = object_loader_.Load(path);

  ubo_buffers_mapped_.reserve(object_.ubo_buffers.size());
  for(size_t i = 0; i < object_.ubo_buffers.size(); ++i) {
    UpdateDescriptorSets(device_.Logical(), object_.ubo_buffers[i].Handle(), object_.textures, descriptor_sets_[i]);

    auto ubo_buffer_mapped = static_cast<UniformBufferObject*>(object_.ubo_buffers[i].Map());
    ubo_buffers_mapped_.emplace_back(ubo_buffer_mapped);
  }
}

void Render::UpdateUniforms() const {
  static const std::chrono::time_point start_time = std::chrono::high_resolution_clock::now();

  const std::chrono::time_point curr_time = std::chrono::high_resolution_clock::now();
  const float time = std::chrono::duration<float>(curr_time - start_time).count();

  UniformBufferObject* ubo = ubo_buffers_mapped_[curr_frame_];
  ubo->model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo->view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo->proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapchain_.ImageExtent().width) / swapchain_.ImageExtent().height, 0.1f, 10.0f);
  ubo->proj[1][1] *= -1;
}

void Render::RecreateSwapchain() {
  int width = 0, height = 0;
  glfwGetFramebufferSize(window_, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window_, &width, &height);
    glfwWaitEvents();
  }

  if (const VkResult result = vkDeviceWaitIdle(device_.Logical()); result != VK_SUCCESS) {
    throw Error("failed to idle device");
  }
  framebuffers_.clear();
  swapchain_ = Device::Dispatchable<VkSwapchainKHR>();

  swapchain_ = device_.CreateSwapchain(window_, surface_.Handle());
  framebuffers_ = swapchain_.CreateFramebuffers(render_pass_.Handle());
}

void Render::RecordCommandBuffer(VkCommandBuffer cmd_buffer, size_t image_idx) {
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
  render_pass_begin_info.renderPass = render_pass_.Handle();
  render_pass_begin_info.framebuffer = framebuffers_[image_idx].Handle();
  render_pass_begin_info.renderArea.offset = {0, 0};
  render_pass_begin_info.renderArea.extent = swapchain_.ImageExtent();
  render_pass_begin_info.clearValueCount = static_cast<uint32_t>(clear_values.size());
  render_pass_begin_info.pClearValues = clear_values.data();
  vkCmdBeginRenderPass(cmd_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.Handle());

  VkViewport viewport = {};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(swapchain_.ImageExtent().width);
  viewport.height = static_cast<float>(swapchain_.ImageExtent().height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd_buffer, 0, 1, &viewport);

  VkRect2D scissor = {};
  scissor.offset = {0, 0};
  scissor.extent = swapchain_.ImageExtent();
  vkCmdSetScissor(cmd_buffer, 0, 1, &scissor);

  VkBuffer vertices_buffer = object_.vertices.Handle();
  VkBuffer indices_buffer = object_.indices.Handle();
  VkDescriptorSet descriptor_set = descriptor_sets_[curr_frame_];

  VkDeviceSize prev_offset = 0;
  std::array<VkDeviceSize, 1> vertex_offsets = {};

  for(const auto[index, offset] : object_.usemtl) {
    const VkDeviceSize curr_offset = prev_offset * sizeof(Index::type);

    vkCmdPushConstants(cmd_buffer, pipeline_layout_.Handle(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(unsigned int), &index);
    vkCmdBindVertexBuffers(cmd_buffer, 0, vertex_offsets.size(), &vertices_buffer, vertex_offsets.data());
    vkCmdBindIndexBuffer(cmd_buffer, indices_buffer, curr_offset, Index::type_enum);
    vkCmdBindDescriptorSets(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_.Handle(), 0, 1, &descriptor_set, 0, nullptr);
    vkCmdDrawIndexed(cmd_buffer, static_cast<uint32_t>(offset - prev_offset), 1, 0, 0, 0);

    prev_offset = offset;
  }
  vkCmdEndRenderPass(cmd_buffer);
  if (const VkResult result = vkEndCommandBuffer(cmd_buffer); result != VK_SUCCESS) {
    throw Error("failed to record command buffer").WithCode(result);
  }
}

void Render::RenderFrame() {
  uint32_t image_idx;

  VkQueue graphics_queue = device_.GraphicsQueue();
  VkQueue present_queue = device_.PresentQueue();

  VkFence fence = fences_[curr_frame_].Handle();
  VkSemaphore image_semaphore = image_semaphores_[curr_frame_].Handle();
  VkSemaphore render_semaphore = render_semaphores_[curr_frame_].Handle();

  VkCommandBuffer cmd_buffer = cmd_buffers_[curr_frame_];

  if (const VkResult result = vkWaitForFences(device_.Logical(), 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()); result != VK_SUCCESS) {
    throw Error("failed to wait for fences").WithCode(result);
  }
  if (const VkResult result = vkAcquireNextImageKHR(device_.Logical(), swapchain_.Handle(), std::numeric_limits<uint64_t>::max(), image_semaphore, VK_NULL_HANDLE, &image_idx); result != VK_SUCCESS) {
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      RecreateSwapchain();
      return;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
      throw Error("failed to acquire next image").WithCode(result);
    }
  }
  UpdateUniforms();
  if (const VkResult result = vkResetFences(device_.Logical(), 1, &fence); result != VK_SUCCESS) {
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

  if (const VkResult result = vkQueueSubmit(graphics_queue, 1, &submit_info, fence); result != VK_SUCCESS) {
    throw Error("failed to submit draw command buffer").WithCode(result);
  }

  VkPresentInfoKHR present_info = {};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_semaphore;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapchain_.HandlePtr();
  present_info.pImageIndices = &image_idx;

  const VkResult result = vkQueuePresentKHR(present_queue, &present_info);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized_) {
    framebuffer_resized_ = false;
    RecreateSwapchain();
  } else if (result != VK_SUCCESS) {
    throw Error("failed to queue present").WithCode(result);
  }
  curr_frame_ = (curr_frame_ + 1) % config::kFrameCount;
}

Render::~Render() {
  vkDeviceWaitIdle(device_.Logical());
}

} // vk