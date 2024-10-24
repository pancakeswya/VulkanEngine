#ifndef BACKEND_RENDER_VK_CONFIG_H_
#define BACKEND_RENDER_VK_CONFIG_H_

#include "backend/render/vk_types.h"

#include <vulkan/vulkan.h>

#include <vector>

#define vkGetInstanceProcAddrByType(instance, proc) \
  reinterpret_cast<decltype(&(proc))>(vkGetInstanceProcAddr(instance, #proc))
namespace vk::config {

inline constexpr size_t kFrameCount = 2;
inline constexpr size_t kMaxTextureCount = MAX_TEXTURE_COUNT;
inline constexpr VkExtent2D kDummyImageExtent = {16, 16};

#undef MAX_TEXTURE_COUNT

#ifdef DEBUG
extern bool InstanceLayersIsSupported();
extern std::vector<const char*> GetInstanceLayers();
extern VkDebugUtilsMessengerCreateInfoEXT GetMessengerCreateInfo() noexcept;
#endif  // DEBUG

extern ImageSettings GetImageSettings() noexcept;
extern const VkAllocationCallbacks* GetAllocationCallbacks() noexcept;
extern VkApplicationInfo GetApplicationInfo() noexcept;

extern std::vector<const char*> GetInstanceExtensions();
extern std::vector<const char*> GetDeviceExtensions();
extern std::vector<VkDynamicState> GetDynamicStates();
extern std::vector<VkPipelineStageFlags> GetPipelineStageFlags();

}  // namespace vk::config

#endif // BACKEND_RENDER_VK_CONFIG_H_