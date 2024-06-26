#include "vk/sync.h"

#include <stdlib.h>

static inline Error semaphoreCreate(VkDevice logical_device,
                             VkSemaphore* semaphore) {
  const VkSemaphoreCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  const VkResult vk_res =
      vkCreateSemaphore(logical_device, &create_info, NULL, semaphore);
  if (vk_res != VK_SUCCESS) {
    return VulkanErrorCreate(vk_res);
  }
  return kSuccess;
}

static inline Error fenceCreate(VkDevice logical_device, VkFence* fence) {
  const VkFenceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = VK_FENCE_CREATE_SIGNALED_BIT};
  const VkResult vk_res =
      vkCreateFence(logical_device, &create_info, NULL, fence);
  if (vk_res != VK_SUCCESS) {
    return VulkanErrorCreate(vk_res);
  }
  return kSuccess;
}

static Error semaphoresCreate(
    VkDevice logical_device,
    const uint32_t count,
    VkSemaphore** semaphores_ptr,
    uint32_t* semaphore_count_ptr
) {
  VkSemaphore* semaphores = (VkSemaphore*)malloc(count * sizeof(VkSemaphore));
  if (semaphores == NULL) {
    return StdErrorCreate(kStdErrorOutOfMemory);
  }
  for(size_t i = 0; i < count; ++i) {
    const Error err = semaphoreCreate(logical_device, semaphores + i);
    if (!ErrorEqual(err, kSuccess)) {
      free(semaphores);
      return err;
    }
  }
  *semaphores_ptr = semaphores;
  *semaphore_count_ptr = count;

  return kSuccess;
}

static Error fencesCreate(
    VkDevice logical_device,
    const uint32_t count,
    VkFence** fences_ptr,
    uint32_t* fence_count_ptr
) {
  VkFence* fences = (VkFence*)malloc(count * sizeof(VkFence));
  if (fences == NULL) {
    return StdErrorCreate(kStdErrorOutOfMemory);
  }
  for(size_t i = 0; i < count; ++i) {
    const Error err = fenceCreate(logical_device, fences + i);
    if (!ErrorEqual(err, kSuccess)) {
      free(fences);
      return err;
    }
  }
  *fences_ptr = fences;
  *fence_count_ptr = count;

  return kSuccess;
}

static inline void semaphoresDestroy(VkDevice logical_device, VkSemaphore* semaphores, const uint32_t semaphore_count) {
  if (semaphores == NULL) {
    return;
  }
  for(size_t i = 0; i < semaphore_count; ++i) {
    vkDestroySemaphore(logical_device, semaphores[i], NULL);
  }
  free(semaphores);
}

static inline void fencesDestroy(VkDevice logical_device, VkFence* fences, const uint32_t fence_count) {
  if (fences == NULL) {
    return;
  }
  for(size_t i = 0; i < fence_count; ++i) {
    vkDestroyFence(logical_device, fences[i], NULL);
  }
  free(fences);
}

Error VulkanSyncCreate(VkDevice logical_device, const uint32_t count, VulkanSync* sync) {
  Error err = semaphoresCreate(logical_device, count, &sync->image_semaphores, &sync->image_semaphore_count);
  if (!ErrorEqual(err, kSuccess)) {
    return err;
  }
  err = semaphoresCreate(logical_device, count, &sync->render_semaphores, &sync->render_semaphore_count);
  if (!ErrorEqual(err, kSuccess)) {
    semaphoresDestroy(logical_device, sync->image_semaphores, sync->image_semaphore_count);
    return err;
  }
  err = fencesCreate(logical_device, count, &sync->fences, &sync->fence_count);
  if (!ErrorEqual(err, kSuccess)) {
    semaphoresDestroy(logical_device, sync->render_semaphores, sync->render_semaphore_count);
    semaphoresDestroy(logical_device, sync->image_semaphores, sync->image_semaphore_count);
    return err;
  }
  return kSuccess;
}

void VulkanSyncDestroy(VkDevice logical_device, VulkanSync* sync) {
  fencesDestroy(logical_device, sync->fences, sync->render_semaphore_count);
  semaphoresDestroy(logical_device, sync->render_semaphores, sync->render_semaphore_count);
  semaphoresDestroy(logical_device, sync->image_semaphores, sync->image_semaphore_count);
}