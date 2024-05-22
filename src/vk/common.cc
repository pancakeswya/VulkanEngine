#include "vk/common.h"

#include <GLFW/glfw3.h>

namespace vk::common {

std::vector<const char*> RequiredExtensions() {
  uint32_t ext_count = 0;
  const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&ext_count);
  std::vector<const char*> extensions(glfw_extensions, glfw_extensions + ext_count);
#ifdef DEBUG
  extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
  return extensions;
}

} // namespace vk