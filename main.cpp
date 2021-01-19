// Enable the WSI extensions
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#elif defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#define SDL_MAIN_HANDLED

#include "VkEngine.h"

#include <vulkan/vulkan.hpp>

int main()
{
    VkExtent2D dimensions{ 1280, 720 };
    vk_engine::VkEngine engine{ dimensions };
    engine.run();
}
