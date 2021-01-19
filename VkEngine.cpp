#include "VkEngine.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>

#include <iostream>
#include <exception>
#include <vector>
#include <algorithm>

namespace {
	vk::SurfaceFormatKHR pick_surface_format(std::vector<vk::SurfaceFormatKHR> const& formats)
	{
		vk::SurfaceFormatKHR picked_format = formats[0];
		if (formats.size() == 1)
		{
			if (formats[0].format == vk::Format::eUndefined)
			{
				picked_format.format = vk::Format::eB8G8R8A8Unorm;
				picked_format.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
			}
		}
		else
		{
			// request several formats, the first found will be used
			vk::Format requested_formats[] = {
			  vk::Format::eB8G8R8A8Unorm, vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8Unorm, vk::Format::eR8G8B8Unorm
			};
			vk::ColorSpaceKHR requested_color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
			for (size_t i = 0; i < sizeof(requested_formats) / sizeof(requested_formats[0]); i++)
			{
				vk::Format requested_format = requested_formats[i];
				auto       it = std::find_if(
					formats.begin(), formats.end(), [requested_format, requested_color_space](vk::SurfaceFormatKHR const& f) {
						return (f.format == requested_format) && (f.colorSpace == requested_color_space);
					});
				if (it != formats.end())
				{
					picked_format = *it;
					break;
				}
			}
		}
		assert(picked_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
		return picked_format;
	}
}

namespace vk_engine {
	VkEngine::VkEngine(vk::Extent2D dimensions) : _window_extent{ dimensions } {
		init_sdl();
		init_vulkan();
		_is_initialized = true;
	}

	VkEngine::~VkEngine() {
		if (_is_initialized) {
			SDL_DestroyWindow(_window);
			SDL_Quit();
		}
		else {
			abort();
		}
	}

	void VkEngine::init_sdl() {
		if (SDL_Init(SDL_INIT_VIDEO) != 0) { // TODO make it parameter?
			std::cerr << "Could not initialize SDL." << std::endl; // TODO use proper logger instead of cout
			throw std::runtime_error("Could not initialize SDL.");
		}
		_window = SDL_CreateWindow("Vulkan Window", SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED, _window_extent.width, _window_extent.height, SDL_WINDOW_VULKAN); // TODO parameters should not be hardcoded?
		if (_window == nullptr) {
			std::cerr << "Could not create SDL window." << std::endl;
			throw std::runtime_error("Could not create SDL window.");
		}
	}

	void VkEngine::init_vulkan() {
		vk::ApplicationInfo app_info = vk::ApplicationInfo()
			.setPApplicationName("VkEngine")
			.setApplicationVersion(1)
			.setPEngineName("VkEngine")
			.setEngineVersion(1)
			.setApiVersion(VK_MAKE_VERSION(1, 1, 0));
		std::vector<const char*> layers;
#if defined(_DEBUG)
		layers.push_back("VK_LAYER_KHRONOS_validation");
#endif
		uint32_t extensionCount = 0;
		if (!SDL_Vulkan_GetInstanceExtensions(_window, &extensionCount, nullptr)) {
			std::cerr << "Could not enumerate extensions." << std::endl;
			throw std::runtime_error("Could not enumerate extensions.");
		}
		std::vector<const char*> extensions(extensionCount);
		if (!SDL_Vulkan_GetInstanceExtensions(_window, &extensionCount, extensions.data())) {
			std::cerr << "Could not get extensions names." << std::endl;
			throw std::runtime_error("Could not get extensions names.");
		}
		vk::InstanceCreateInfo inst_info = vk::InstanceCreateInfo()
			.setFlags(vk::InstanceCreateFlags())
			.setPApplicationInfo(&app_info)
			.setEnabledExtensionCount(static_cast<std::uint32_t>(extensions.size()))
			.setPpEnabledExtensionNames(extensions.data())
			.setEnabledLayerCount(static_cast<std::uint32_t>(layers.size()))
			.setPpEnabledLayerNames(layers.data());
		_vk_instance= vk::createInstanceUnique(inst_info);
		VkSurfaceKHR c_surface;
		if (!SDL_Vulkan_CreateSurface(_window, static_cast<VkInstance>(_vk_instance.get()), &c_surface)) {
			std::cerr << "Could not create a Vulkan surface." << std::endl;
			throw std::runtime_error("Could not create a Vulkan surface.");
		}
		_surface = c_surface;
		const auto device_count{ _vk_instance->enumeratePhysicalDevices().size() };
		_physical_device = _vk_instance->enumeratePhysicalDevices().front(); // TODO select device based on features
		const auto queue_family_properties = _physical_device.getQueueFamilyProperties();
		auto  graphics_queue_family_index = std::distance(
			queue_family_properties.begin(),
			std::find_if(
				queue_family_properties.begin(), queue_family_properties.end(), [](const auto& qfp) {
					return qfp.queueFlags & vk::QueueFlagBits::eGraphics;
						//&& _physical_device.getSurfaceSupportKHR(static_cast<uint32_t>(i), _surface));
				}));
		if (graphics_queue_family_index < 0 
			|| static_cast<std::size_t>(graphics_queue_family_index) >= queue_family_properties.size()) {
			std::cerr << "Could not find physical device supporting graphics." << std::endl;
			throw std::runtime_error("Could not find physical device supporting graphics.");
		}
		auto present_queue_family_index{ _physical_device.getSurfaceSupportKHR(static_cast<uint32_t>(graphics_queue_family_index), _surface)
			? graphics_queue_family_index
			: queue_family_properties.size() };
		if (present_queue_family_index == queue_family_properties.size()) {
			// the graphicsQueueFamilyIndex doesn't support present -> look for an other family index that supports both
			// graphics and present
			for (std::size_t i = 0; i < queue_family_properties.size(); ++i) {
				if ((queue_family_properties[i].queueFlags & vk::QueueFlagBits::eGraphics) 
					&& _physical_device.getSurfaceSupportKHR(static_cast<uint32_t>(i), _surface)) {
					graphics_queue_family_index = static_cast<uint32_t>(i);
					present_queue_family_index = i;
					break;
				}
			}
			if (present_queue_family_index == queue_family_properties.size()) {
				// there's nothing like a single family index that supports both graphics and present -> look for an other
				// family index that supports present
				for (std::size_t i = 0; i < queue_family_properties.size(); ++i) {
					if (_physical_device.getSurfaceSupportKHR(static_cast<uint32_t>(i), _surface)) {
						present_queue_family_index = i;
						break;
					}
				}
			}
		}
		if ((graphics_queue_family_index == queue_family_properties.size()) ||
			(present_queue_family_index == queue_family_properties.size())) {
			std::cerr << "Could not find a queue for graphics or present" << std::endl;
			throw std::runtime_error("Could not find a queue for graphics or present");
		}

		auto queue_priority = 0.0f;

		vk::DeviceQueueCreateInfo device_queue_create_info(
			vk::DeviceQueueCreateFlags(), static_cast<std::uint32_t>(graphics_queue_family_index), 1, &queue_priority);
		std::vector<const char*> enabled_extensions{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
		_device = _physical_device.createDeviceUnique(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), device_queue_create_info, {}, enabled_extensions));
		const auto formats = _physical_device.getSurfaceFormatsKHR(_surface);
		if (formats.empty()) {
			std::cerr << "Surface formats are empty" << std::endl;
			throw std::runtime_error("Surface formats are empty");
		}
		const auto format = (formats[0].format == vk::Format::eUndefined) ? vk::Format::eB8G8R8A8Unorm : formats[0].format;
		const auto surface_capabilities = _physical_device.getSurfaceCapabilitiesKHR(_surface);
		const auto swapchain_present_mode = vk::PresentModeKHR::eFifo;
		const auto pre_transform{ (surface_capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
			? vk::SurfaceTransformFlagBitsKHR::eIdentity
			: surface_capabilities.currentTransform };
		const auto composite_alpha{ (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
			? vk::CompositeAlphaFlagBitsKHR::ePreMultiplied
			: (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
			? vk::CompositeAlphaFlagBitsKHR::ePostMultiplied
			: (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
			? vk::CompositeAlphaFlagBitsKHR::eInherit
			: vk::CompositeAlphaFlagBitsKHR::eOpaque };
		vk::SwapchainCreateInfoKHR swap_chain_create_info{ vk::SwapchainCreateFlagsKHR(),
			_surface,
			surface_capabilities.minImageCount,
			format,
			vk::ColorSpaceKHR::eSrgbNonlinear,
			_window_extent,
			1,
			vk::ImageUsageFlagBits::eColorAttachment,
			vk::SharingMode::eExclusive,
			{},
			pre_transform,
			composite_alpha,
			swapchain_present_mode,
			true,
			nullptr };
		std::uint32_t queue_family_indices[2] {
			static_cast<uint32_t>(graphics_queue_family_index),
			static_cast<uint32_t>(present_queue_family_index)
		};
		if (graphics_queue_family_index != present_queue_family_index)
		{
			// If the graphics and present queues are from different queue families, we either have to explicitly transfer
			// ownership of images between the queues, or we have to create the swapchain with imageSharingMode as
			// VK_SHARING_MODE_CONCURRENT
			swap_chain_create_info.imageSharingMode = vk::SharingMode::eConcurrent;
			swap_chain_create_info.queueFamilyIndexCount = 2;
			swap_chain_create_info.pQueueFamilyIndices = queue_family_indices;
		}
		assert(_device);
		_swapchain = _device->createSwapchainKHRUnique(swap_chain_create_info);
		_swapchain_images = _device->getSwapchainImagesKHR(_swapchain.get());
		_swapchain_image_views.reserve(_swapchain_images.size());
		_swapchain_image_format = pick_surface_format(_physical_device.getSurfaceFormatsKHR(_surface)).format;
		vk::ComponentMapping component_mapping{ vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
		vk::ImageSubresourceRange sub_resource_range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		for (auto image : _swapchain_images) {
			vk::ImageViewCreateInfo image_view_create_info{ vk::ImageViewCreateFlags(), image, vk::ImageViewType::e2D, format, component_mapping, sub_resource_range };
			_swapchain_image_views.push_back(_device->createImageViewUnique(image_view_create_info));
		}
		_command_pool = _device->createCommandPoolUnique(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlags(), graphics_queue_family_index));
		_command_buffer = std::move(_device
			->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(_command_pool.get(), vk::CommandBufferLevel::ePrimary, 1))
			.front());
		_graphics_queue = _device->getQueue(graphics_queue_family_index, 0);
		_present_queue = _device->getQueue(present_queue_family_index, 0);

		std::vector<vk::AttachmentDescription> attachment_descriptions;
		attachment_descriptions.emplace_back(
			vk::AttachmentDescriptionFlags(),
			_swapchain_image_format,
			vk::SampleCountFlagBits::e1,
			vk::AttachmentLoadOp::eClear,
			vk::AttachmentStoreOp::eStore,
			vk::AttachmentLoadOp::eDontCare,
			vk::AttachmentStoreOp::eDontCare,
			vk::ImageLayout::eUndefined,
			vk::ImageLayout::ePresentSrcKHR
		);
		vk::AttachmentReference color_attachment{ 0, vk::ImageLayout::eColorAttachmentOptimal };
		vk::SubpassDescription subpass_description{ vk::SubpassDescriptionFlags(),
			vk::PipelineBindPoint::eGraphics,
			{},
			color_attachment,
			{},
			nullptr };
		_render_pass = _device->createRenderPassUnique(vk::RenderPassCreateInfo{ vk::RenderPassCreateFlags(), attachment_descriptions, subpass_description });

		_frame_buffers.reserve(_swapchain_images.size());
		for (std::size_t i{ 0 }; i < _swapchain_images.size(); ++i) {
			_frame_buffers.push_back(_device->createFramebufferUnique({
				vk::FramebufferCreateFlags(),
				*_render_pass,
				1,
				&_swapchain_image_views[i].get(),
				_window_extent.width,
				_window_extent.height,
				1
			}));
		}
		_render_fence = _device->createFenceUnique({});
		_present_semaphore = _device->createSemaphoreUnique({});
		_render_semaphore = _device->createSemaphoreUnique({});
	}

	void VkEngine::draw() {
		{
			const auto result{ _device->waitForFences(_render_fence.get(), VK_TRUE, 1000000000) }; // TODO do not ignore
		}
		_device->resetFences(_render_fence.get());
		auto swapchain_image_index{ _device->acquireNextImageKHR(_swapchain.get(), 1000000000, _present_semaphore.get(), nullptr) };
		_command_buffer->reset();
		_command_buffer->begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlags() });
		vk::ClearValue clear_color{ vk::ClearColorValue { std::array<float, 4>{0.f, 0.f, abs(sin(_frame_number / 120.f)), 1.0f } } };
		vk::RenderPassBeginInfo render_pass_begin_info(_render_pass.get(),
			_frame_buffers[swapchain_image_index.value].get(),
			vk::Rect2D(vk::Offset2D(0, 0), _window_extent),
			clear_color);
		_command_buffer->beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);
		_command_buffer->endRenderPass();
		_command_buffer->end();
		vk::PipelineStageFlags wait_destination_stage_mask{ vk::PipelineStageFlagBits::eColorAttachmentOutput };
		vk::SubmitInfo submit_info{ *_present_semaphore, wait_destination_stage_mask, *_command_buffer, *_render_semaphore };
		_graphics_queue.submit(submit_info, _render_fence.get());
		while (vk::Result::eTimeout == _device->waitForFences(_render_fence.get(), VK_TRUE, 1000000000)) {
		}
		const auto result = _present_queue.presentKHR(vk::PresentInfoKHR{ *_render_semaphore, *_swapchain, swapchain_image_index.value });
		switch (result)
		{
		case vk::Result::eSuccess: break;
		default:
			std::cerr << "Problem when drawing" << std::endl; // TODO fix this
			throw std::runtime_error("Problem when drawing");
			break;
		};
		++_frame_number;
	}

	void VkEngine::run() {
		SDL_Event e;
		bool bQuit{ false };

		//main loop
		while (!bQuit) {
			//Handle events on queue
			while (SDL_PollEvent(&e) != 0) {
				if (e.type == SDL_QUIT) {
					bQuit = true;
				}
			}
			draw();
			SDL_Delay(10);
		}
	}
}