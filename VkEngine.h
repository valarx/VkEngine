#pragma once

#include <vulkan/vulkan.hpp>

#include <vector>

struct SDL_Window;

namespace vk_engine {
	class VkEngine final {
	public:
		VkEngine(vk::Extent2D dimensions);
		~VkEngine();

		void run();
	private:
		void draw();
		
		void init_sdl();
		void init_vulkan();

		SDL_Window* _window{ nullptr };
		bool _is_initialized{ false };
		std::size_t _frame_number{ 0 };
		vk::Extent2D _window_extent{ 1920, 1080 };
		vk::UniqueInstance _vk_instance;
		//vk::DebugUtilsMessengerEXT _debug_messenger;
		vk::PhysicalDevice _physical_device;
		vk::UniqueDevice _device;
		vk::SurfaceKHR _surface;
		vk::UniqueCommandPool _command_pool;
		vk::UniqueCommandBuffer _command_buffer;
		vk::UniqueSwapchainKHR _swapchain;
		vk::Format _swapchain_image_format;
		std::vector<vk::Image> _swapchain_images;
		std::vector<vk::UniqueImageView> _swapchain_image_views;
		vk::Queue _graphics_queue;
		vk::Queue _present_queue;
		vk::UniqueRenderPass _render_pass;
		std::vector<vk::UniqueFramebuffer> _frame_buffers;
		vk::UniqueSemaphore _present_semaphore;
		vk::UniqueSemaphore _render_semaphore;
		vk::UniqueFence _render_fence;
	};
}

