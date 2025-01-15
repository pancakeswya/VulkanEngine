#ifndef BACKEND_VK_RENDERER_SHADER_H_
#define BACKEND_VK_RENDERER_SHADER_H_

#include "backend/vk/renderer/device.h"

#include <vector>
#include <vulkan/vulkan.h>
#include <string_view>

namespace vk {

struct ShaderInfoBase {
  VkShaderStageFlagBits stage;
  std::string_view entry_point;
};

struct ShaderInfo : ShaderInfoBase {
  std::vector<uint32_t> spirv;
};

extern std::vector<ShaderInfo> GetShaders();

class ShaderModule final : public Device::Dispatchable<VkShaderModule> {
public:
  ShaderModule() noexcept;
  ShaderModule(const ShaderModule& other) = delete;
  ShaderModule(ShaderModule&& other) noexcept;
  ShaderModule(VkShaderModule module,
               VkDevice logical_device,
               const VkAllocationCallbacks* allocator,
               const ShaderInfoBase& info) noexcept;
  ~ShaderModule() override = default;

  ShaderModule& operator=(const ShaderModule& other) = delete;
  ShaderModule& operator=(ShaderModule&& other) noexcept;

  [[nodiscard]] VkShaderStageFlagBits Stage() const noexcept;
  [[nodiscard]] std::string_view EntryPoint() const noexcept;
private:
  ShaderInfoBase info_;
};

inline VkShaderStageFlagBits ShaderModule::Stage() const noexcept {
  return info_.stage;
}

inline std::string_view ShaderModule::EntryPoint() const noexcept {
  return info_.entry_point;
}

} // namespace vk


#endif //BACKEND_VK_RENDERER_SHADER_H_