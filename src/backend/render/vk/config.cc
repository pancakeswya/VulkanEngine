#include "backend/render/vk/config.h"

#include <stb_image.h>

#ifdef DEBUG
#include <cstring>
#endif
#include <iostream>

namespace render::vk::config {

#ifdef DEBUG

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL MessageCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT message_severity[[maybe_unused]],
  VkDebugUtilsMessageTypeFlagsEXT message_type[[maybe_unused]],
  const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
  void* user_data[[maybe_unused]])
{
  std::cerr << callback_data->pMessage << std::endl;
  return VK_FALSE;
}

} // namespace

bool InstanceLayersIsSupported() {
  uint32_t layer_count;
  vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

  std::vector<VkLayerProperties> available_layers(layer_count);
  vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

  for (const char* validation_layer : GetInstanceLayers()) {
    bool layer_found = false;
    for (const VkLayerProperties& layer_properties : available_layers) {
      if (std::strcmp(validation_layer, layer_properties.layerName) == 0) {
        layer_found = true;
        break;
      }
    }
    if (!layer_found) {
      return false;
    }
  }
  return true;
}

std::vector<const char*> GetInstanceLayers() {
  return {
    "VK_LAYER_KHRONOS_validation"
  };
}

VkDebugUtilsMessengerCreateInfoEXT GetMessengerCreateInfo() noexcept {
  VkDebugUtilsMessengerCreateInfoEXT create_info = {};

  create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  create_info.pfnUserCallback = MessageCallback;

  return create_info;
}
#endif // DEBUG

VkApplicationInfo GetApplicationInfo() noexcept {
  VkApplicationInfo app_info = {};

  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "VulkanFun";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "Simple Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;

  return app_info;
}

ImageSettings GetImageSettings() noexcept {
  return {
    STBI_rgb_alpha,
    VK_FORMAT_R8G8B8A8_SRGB
  };
}

std::vector<const char*> GetInstanceExtensions() {
  std::vector<const char*> extensions;
#ifdef DEBUG
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
#ifdef __APPLE__
  extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif
  return extensions;
}

std::vector<const char*> GetDeviceExtensions() {
  return { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
}

std::vector<VkDynamicState> GetDynamicStates() {
  return {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR
  };
}

std::vector<VkPipelineStageFlags> GetPipelineStageFlags() {
  return {
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
  };
}

} // namespace vk::config