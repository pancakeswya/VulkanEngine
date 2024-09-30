#include "backend/render/vk_wrappers.h"

namespace vk {

namespace {

void EndSingleTimeCommands(VkCommandBuffer cmd_buffer, VkQueue graphics_queue) {
  if (const VkResult result = vkEndCommandBuffer(cmd_buffer); result != VK_SUCCESS) {
    throw Error("failed to end cmd buffer").WithCode(result);
  }

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd_buffer;

  if (const VkResult result = vkQueueSubmit(graphics_queue, 1, &submitInfo, VK_NULL_HANDLE); result != VK_SUCCESS) {
    throw Error("failed to submin the graphics queue");
  }
  if (const VkResult result = vkQueueWaitIdle(graphics_queue); result != VK_SUCCESS) {
    throw Error("failed to wait idle graphics queue").WithCode(result);
  }
}

HandleWrapper<VkCommandBuffer> BeginSingleTimeCommands(VkDevice logical_device, VkCommandPool cmd_pool) {
  VkCommandBufferAllocateInfo alloc_info = {};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandPool = cmd_pool;
  alloc_info.commandBufferCount = 1;

  VkCommandBuffer cmd_buffer;
  if (const VkResult result = vkAllocateCommandBuffers(logical_device, &alloc_info, &cmd_buffer); result != VK_SUCCESS) {
    throw Error("failed to allocate command buffers").WithCode(result);
  }
  HandleWrapper<VkCommandBuffer> cmd_buffer_wrapper(
    cmd_buffer,
    [logical_device, cmd_pool](VkCommandBuffer cmd_buffer) {
      vkFreeCommandBuffers(logical_device, cmd_pool, 1, &cmd_buffer);
    }
  );

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (const VkResult result = vkBeginCommandBuffer(cmd_buffer, &begin_info); result != VK_SUCCESS) {
    throw Error("failed to begin command buffer").WithCode(result);
  }

  return cmd_buffer_wrapper;
}

} // namespace

void Buffer::CopyBuffer(const Buffer& src, VkCommandPool cmd_pool, VkQueue graphics_queue) {
    VkDevice logical_device = logical_device_;

    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = cmd_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd_buffer = VK_NULL_HANDLE;
    if (const VkResult result = vkAllocateCommandBuffers(logical_device, &alloc_info, &cmd_buffer); result != VK_SUCCESS) {
      throw Error("failed allocate command buffer").WithCode(result);
    }
    HandleWrapper<VkCommandBuffer> cmd_buffer_wrapper(
      cmd_buffer,
      [logical_device, cmd_pool](VkCommandBuffer cmd_buffer) {
      vkFreeCommandBuffers(logical_device, cmd_pool, 1, &cmd_buffer);
    });

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (const VkResult result = vkBeginCommandBuffer(cmd_buffer, &begin_info); result != VK_SUCCESS) {
      throw Error("failed to begin recording command buffer").WithCode(result);
    }

    VkBufferCopy copy_region = {};
    copy_region.size = src.size_;
    vkCmdCopyBuffer(cmd_buffer, src.Get(), object_wrapper_.get(), 1, &copy_region);

    if (const VkResult result = vkEndCommandBuffer(cmd_buffer); result != VK_SUCCESS) {
      throw Error("failed to record command buffer").WithCode(result);
    }
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffer;

    if (const VkResult result = vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE); result != VK_SUCCESS) {
      throw Error("failed to submit draw command buffer").WithCode(result);
    }
    if (const VkResult result = vkQueueWaitIdle(graphics_queue); result != VK_SUCCESS) {
      throw Error("failed to queue wait idle").WithCode(result);
    }
  }

void Image::TransitImageLayout(VkCommandPool cmd_pool, VkQueue graphics_queue, VkImageLayout old_layout, VkImageLayout new_layout) {
  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = object_wrapper_.get();
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags source_stage;
  VkPipelineStageFlags destination_stage;

  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    source_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destination_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    source_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destination_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw Error("unsupported layout transition");
  }
  HandleWrapper<VkCommandBuffer> cmd_buffer_wrapper = BeginSingleTimeCommands(logical_device_, cmd_pool);
  VkCommandBuffer cmd_buffer = cmd_buffer_wrapper.get();

  vkCmdPipelineBarrier(
      cmd_buffer,
      source_stage, destination_stage,
      0,
      0, nullptr,
      0, nullptr,
      1, &barrier
  );

  EndSingleTimeCommands(cmd_buffer, graphics_queue);
}

void Image::CopyBuffer(const Buffer& src, VkCommandPool cmd_pool, VkQueue graphics_queue) {
  HandleWrapper<VkCommandBuffer> cmd_buffer_wrapper = BeginSingleTimeCommands(logical_device_, cmd_pool);
  VkCommandBuffer cmd_buffer = cmd_buffer_wrapper.get();

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = { extent_.width, extent_.height, 1 };

  vkCmdCopyBufferToImage(cmd_buffer, src.Get(), object_wrapper_.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  EndSingleTimeCommands(cmd_buffer, graphics_queue);
}

} // namespace vk