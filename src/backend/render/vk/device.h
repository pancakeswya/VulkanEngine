#ifndef BACKEND_RENDER_VK_DEVICE_H_
#define BACKEND_RENDER_VK_DEVICE_H_

#include "backend/render/vk/dispatchable.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string_view>
#include <vector>

namespace vk {

class Device {
public:
  using HandleType = VkDevice;

  template<typename Tp>
  class Dispatchable : public vk::Dispatchable<Tp, Device> {
  public:
    using Base = vk::Dispatchable<Tp, Device>;
    using Base::Base;
  };

  struct ShaderStage {
    VkShaderStageFlagBits bits;
    Dispatchable<VkShaderModule> module;

    std::string_view name;
  };

  struct QueueFamilyIndices {
    uint32_t graphic, present;
  };

  static std::pair<bool, QueueFamilyIndices> PhysicalDeviceIsSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);

  Device() noexcept;
  Device(const Device& other) = delete;
  Device(Device&& other) noexcept;
  Device(VkPhysicalDevice physical_device, const QueueFamilyIndices& indices, const VkAllocationCallbacks* allocator = nullptr);
  ~Device();

  Device& operator=(const Device& other) = delete;
  Device& operator=(Device&& other) noexcept;

  [[nodiscard]] VkDevice Logical() const noexcept;
  [[nodiscard]] VkPhysicalDevice Physical() const noexcept;
  [[nodiscard]] VkQueue GraphicsQueue() const noexcept;
  [[nodiscard]] VkQueue PresentQueue() const noexcept;

  [[nodiscard]] Dispatchable<VkShaderModule> CreateShaderModule(const std::string& path) const;
  [[nodiscard]] Dispatchable<VkRenderPass> CreateRenderPass(VkFormat image_format, VkFormat depth_format) const;
  [[nodiscard]] Dispatchable<VkPipelineLayout> CreatePipelineLayout(VkDescriptorSetLayout descriptor_set_layout) const;
  [[nodiscard]] Dispatchable<VkPipeline> CreatePipeline(VkPipelineLayout pipeline_layout, VkRenderPass render_pass, const std::vector<VkVertexInputAttributeDescription>& attribute_descriptions, const std::vector<VkVertexInputBindingDescription>& binding_descriptions, const std::initializer_list<ShaderStage>& shader_stages) const;
  [[nodiscard]] Dispatchable<VkCommandPool> CreateCommandPool() const;
  [[nodiscard]] Dispatchable<VkSemaphore> CreateSemaphore() const;
  [[nodiscard]] Dispatchable<VkFence> CreateFence() const;
  [[nodiscard]] Dispatchable<VkDescriptorSetLayout> CreateDescriptorSetLayout() const;
  [[nodiscard]] Dispatchable<VkDescriptorPool> CreateDescriptorPool(size_t count) const;
  [[nodiscard]] Dispatchable<VkBuffer> CreateBuffer(VkBufferUsageFlags usage, uint32_t data_size) const;
  [[nodiscard]] Dispatchable<VkImage> CreateImage(VkImageUsageFlags usage,
                                                  VkExtent2D extent,
                                                  VkFormat format,
                                                  VkImageTiling tiling) const;
  [[nodiscard]] Dispatchable<VkSwapchainKHR> CreateSwapchain(GLFWwindow* window, VkSurfaceKHR surface) const;
  [[nodiscard]] std::vector<VkCommandBuffer> CreateCommandBuffers(VkCommandPool cmd_pool, uint32_t count) const;
  [[nodiscard]] std::vector<VkDescriptorSet> CreateDescriptorSets(VkDescriptorSetLayout descriptor_set_layout, VkDescriptorPool descriptor_pool, size_t count) const;
private:
  VkDevice logical_device_;
  VkPhysicalDevice physical_device_;
  const VkAllocationCallbacks* allocator_;
  QueueFamilyIndices indices_;
};

template<>
class Device::Dispatchable<VkBuffer> : public vk::Dispatchable<VkBuffer, Device> {
public:
  using Base = vk::Dispatchable<VkBuffer, Device>;

  Dispatchable() noexcept;
  Dispatchable(const Dispatchable& other) = delete;
  Dispatchable(Dispatchable&& other) noexcept;
  Dispatchable(VkBuffer buffer,
               VkDevice logical_device,
               VkPhysicalDevice physical_device,
               const VkAllocationCallbacks* allocator,
               uint32_t size) noexcept;
  ~Dispatchable() override = default;

  Dispatchable& operator=(const Dispatchable& other) = delete;
  Dispatchable& operator=(Dispatchable&& other) noexcept;

  void Bind() const;
  void Allocate(VkMemoryPropertyFlags properties);
  [[nodiscard]] void* Map() const;
  void Unmap() const noexcept;

  [[nodiscard]] uint32_t Size() const noexcept;
private:
  Dispatchable<VkDeviceMemory> memory_;
  VkPhysicalDevice physical_device_;

  uint32_t size_;
};

template<>
class Device::Dispatchable<VkImage> : public vk::Dispatchable<VkImage, Device> {
public:
  using Base = vk::Dispatchable<VkImage, Device>;

  Dispatchable() noexcept;
  Dispatchable(const Dispatchable& other) = delete;
  Dispatchable(Dispatchable&& other) noexcept;
  Dispatchable(VkImage image,
               VkDevice logical_device,
               VkPhysicalDevice physical_device,
               const VkAllocationCallbacks* allocator,
               VkExtent2D extent,
               VkFormat format,
               uint32_t mip_levels) noexcept;
  ~Dispatchable() override = default;

  Dispatchable& operator=(const Dispatchable&) = delete;
  Dispatchable& operator=(Dispatchable&& other) noexcept;

  void Bind() const;
  void Allocate(VkMemoryPropertyFlags properties);
  void CreateView(VkImageAspectFlags aspect_flags);
  void CreateSampler(VkSamplerMipmapMode mipmap_mode);
  [[nodiscard]] bool FormatFeatureSupported(VkFormatFeatureFlagBits feature) const;
  [[nodiscard]] const Dispatchable<VkImageView>& View() const noexcept;
  [[nodiscard]] const Dispatchable<VkSampler>& Sampler() const noexcept;
  [[nodiscard]] VkFormat Format() const noexcept;
  [[nodiscard]] VkExtent2D Extent() const noexcept;
  [[nodiscard]] uint32_t MipLevels() const noexcept;
private:
  uint32_t mip_levels_;

  VkPhysicalDevice physical_device_;
  VkExtent2D extent_;
  VkFormat format_;

  Dispatchable<VkImageView> view_;
  Dispatchable<VkSampler> sampler_;
  Dispatchable<VkDeviceMemory> memory_;
};

template<>
class Device::Dispatchable<VkSwapchainKHR> : public vk::Dispatchable<VkSwapchainKHR, Device> {
public:
  using Base = vk::Dispatchable<VkSwapchainKHR, Device>;

  Dispatchable() noexcept;
  Dispatchable(const Dispatchable& other) = delete;
  Dispatchable(Dispatchable&& other) noexcept;
  Dispatchable(VkSwapchainKHR swapchain,
               VkDevice logical_device,
               VkPhysicalDevice physical_device,
               const VkAllocationCallbacks* allocator,
               VkExtent2D extent,
               VkFormat format) noexcept;

  ~Dispatchable() override = default;

  Dispatchable& operator=(const Dispatchable&) = delete;
  Dispatchable& operator=(Dispatchable&& other) noexcept;

  [[nodiscard]] VkExtent2D ImageExtent() const noexcept;
  [[nodiscard]] VkFormat ImageFormat() const noexcept;
  [[nodiscard]] VkFormat DepthImageFormat() const noexcept;

  [[nodiscard]] std::vector<Dispatchable<VkFramebuffer>> CreateFramebuffers(VkRenderPass render_pass) const;
private:
  VkExtent2D extent_;
  VkFormat format_;

  VkPhysicalDevice physical_device_;
  Dispatchable<VkImage> depth_image_;
  std::vector<Dispatchable<VkImageView>> image_views_;

  [[nodiscard]] Dispatchable<VkImage> CreateDepthImage() const;
  [[nodiscard]] std::vector<VkImage> GetImages() const;
  [[nodiscard]] std::vector<Dispatchable<VkImageView>> CreateImageViews() const;
  [[nodiscard]] Dispatchable<VkFramebuffer> CreateFramebuffer(const std::vector<VkImageView>& views, VkRenderPass render_pass) const;
};

inline VkDevice Device::Logical() const noexcept {
  return logical_device_;
}

inline VkPhysicalDevice Device::Physical() const noexcept {
  return physical_device_;
}

inline VkQueue Device::GraphicsQueue() const noexcept {
  VkQueue graphics_queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(logical_device_, indices_.graphic, 0, &graphics_queue);
  return graphics_queue;
}

inline VkQueue Device::PresentQueue() const noexcept {
  VkQueue present_queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(logical_device_, indices_.present, 0, &present_queue);
  return present_queue;
}

inline const Device::Dispatchable<VkImageView>& Device::Dispatchable<VkImage>::View() const noexcept {
  return view_;
}

inline const Device::Dispatchable<VkSampler>& Device::Dispatchable<VkImage>::Sampler() const noexcept {
  return sampler_;
}

inline VkFormat Device::Dispatchable<VkImage>::Format() const noexcept {
  return format_;
}

inline VkExtent2D Device::Dispatchable<VkImage>::Extent() const noexcept {
  return extent_;
}

inline uint32_t Device::Dispatchable<VkImage>::MipLevels() const noexcept {
  return mip_levels_;
}

inline VkExtent2D Device::Dispatchable<VkSwapchainKHR>::ImageExtent() const noexcept {
  return extent_;
}

inline VkFormat Device::Dispatchable<VkSwapchainKHR>::ImageFormat() const noexcept {
  return format_;
}

inline VkFormat Device::Dispatchable<VkSwapchainKHR>::DepthImageFormat() const noexcept {
  return depth_image_.Format();
}


} // namespace vk

#endif // BACKEND_RENDER_VK_DEVICE_H_