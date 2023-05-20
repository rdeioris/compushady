#include <Python.h>
#include "structmember.h"
#include <vulkan/vulkan.hpp>
#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#else
#ifdef __APPLE__
#include <vulkan/vulkan_metal.h>
#else
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif
#endif

#include <unordered_map>
#include <string>

#include "compushady.h"

#define VK_FORMAT(x, size) vulkan_formats[x] = {VK_FORMAT_##x, size}
#define VK_FORMAT_FLOAT(x, size) vulkan_formats[x##_FLOAT] = {VK_FORMAT_##x##_SFLOAT, size}
#define VK_FORMAT_SRGB(x, size) vulkan_formats[x##_UNORM_SRGB] = {VK_FORMAT_##x##_SRGB, size}

std::unordered_map<uint32_t, std::pair<VkFormat, uint32_t>> vulkan_formats;
std::vector<std::string> vulkan_debug_messages;

static bool vulkan_debug = false;
static VkInstance vulkan_instance = VK_NULL_HANDLE;
static bool vulkan_supports_swapchain = true;

typedef struct vulkan_Device
{
	PyObject_HEAD;
	VkPhysicalDevice physical_device;
	VkDevice device;
	VkQueue queue;
	PyObject* name;
	size_t dedicated_video_memory;
	size_t dedicated_system_memory;
	size_t shared_system_memory;
	VkPhysicalDeviceMemoryProperties mem_props;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	uint32_t device_id;
	uint32_t vendor_id;
	uint32_t queue_family_index;
	char is_hardware;
	char is_discrete;
	VkPhysicalDeviceFeatures features;
} vulkan_Device;

typedef struct vulkan_Resource
{
	PyObject_HEAD;
	vulkan_Device* py_device;
	VkBuffer buffer;
	VkImage image;
	VkImageView image_view;
	VkBufferView buffer_view;
	VkDeviceMemory memory;
	size_t size;
	uint32_t stride;
	VkExtent3D image_extent;
	VkDescriptorBufferInfo descriptor_buffer_info;
	VkDescriptorImageInfo descriptor_image_info;
	uint32_t row_pitch;
	VkFormat format;
} vulkan_Resource;

typedef struct vulkan_Compute
{
	PyObject_HEAD;
	vulkan_Device* py_device;
	VkDescriptorPool descriptor_pool;
	VkPipeline pipeline;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkDescriptorSet descriptor_set;
	VkShaderModule shader_module;
	PyObject* py_cbv_list;
	PyObject* py_srv_list;
	PyObject* py_uav_list;
} vulkan_Compute;

typedef struct vulkan_Swapchain
{
	PyObject_HEAD;
	vulkan_Device* py_device;
	VkSwapchainKHR swapchain;
	VkSemaphore copy_semaphore;
	VkSemaphore present_semaphore;
	VkSurfaceKHR surface;
	std::vector<VkImage> images;
	VkExtent2D image_extent;
} vulkan_Swapchain;

static const char* vulkan_get_spirv_entry_point(const uint32_t* words, size_t len)
{
	if (len < 20) // strip SPIR-V header
		return NULL;
	if (len % 4) // SPIR-V is a stream of 32bits words
		return NULL;
	if (words[0] != 0x07230203) // SPIR-V magic
		return NULL;

	size_t offset = 5; // (20 / 4 of SPIR-V header)
	size_t words_num = len / 4;
	while (offset < words_num)
	{
		uint32_t word = words[offset];
		uint16_t opcode = word & 0xFFFF;
		uint16_t size = word >> 16;
		if (size == 0) // avoid loop!
			return NULL;
		if (opcode == 0x0F && (offset + size < words_num) && words[offset + 1] == 5) // OpEntryPoint(0x0F) + GLCompute(0x05)
		{
			if (size > 3)
			{
				const char* name = (const char*)&words[offset + 3];
				size_t max_namelen = (size - 3) * 4;
				// check for trailing 0
				for (size_t i = 0; i < max_namelen; i++)
				{
					if (name[i] == 0)
						return name;
				}
			}
		}
		offset += size;
	}
	return NULL;
}

/*
 * This issue has been discovered by the old Marco Beri:
 * when mapping a descriptor to an image with a vulkan unsupported layout (B8G8R8A8_UNORM and B8G8R8A8_SRGB)
 * the intel driver will spit out errors and will ignore the access to the image object
 * Given that we need to support BGRA8 layouts (this is the format used by swapchains) the only solution is
 * to brutally patch the SPIR-V blob by adding an opcode marking the descriptor as NonReadable
 *
 *
 */
static uint32_t* vulkan_patch_spirv_unknown_uav(const uint32_t* words, size_t len, uint32_t binding)
{
	if (len < 20) // strip SPIR-V header
		return NULL;
	if (len % 4) // SPIR-V is a stream of 32bits words
		return NULL;
	if (words[0] != 0x07230203) // SPIR-V magic
		return NULL;

	size_t offset = 5; // (20 / 4 of SPIR-V header)
	size_t words_num = len / 4;

	bool found = false;
	uint32_t binding_id = 0;

	size_t injection_offset = 0;

	// first step, search for Binding
	while (offset < words_num)
	{
		uint32_t word = words[offset];
		uint16_t opcode = word & 0xFFFF;
		uint16_t size = word >> 16;
		if (size == 0) // avoid loop!
			return NULL;
		if (opcode == 71 && (offset + size < words_num)) // OpDecorate(71) + id + Binding
		{
			if (size > 3)
			{
				if (words[offset + 2] == 33 && words[offset + 3] == binding)
				{
					binding_id = words[offset + 1];
					found = true;
					injection_offset = offset + size;
					break;
				}
			}
		}
		offset += size;
	}

	if (!found)
		return NULL;

	// second step, check if NonReadable is already set
	offset = 5; // (20 / 4 of SPIR-V header)
	found = false;
	while (offset < words_num)
	{
		uint32_t word = words[offset];
		uint16_t opcode = word & 0xFFFF;
		uint16_t size = word >> 16;
		if (size == 0) // avoid loop!
			return NULL;
		if (opcode == 71 && (offset + size < words_num)) // OpDecorate(71) + id + NonReadable
		{
			if (size > 2)
			{
				if (words[offset + 2] == 25)
				{
					found = true;
					break;
				}
			}
		}
		offset += size;
	}

	if (found)
		return NULL;

	// inject NonReadable

	uint32_t* patched_blob = (uint32_t*)PyMem_Malloc(len + (4 * 3));
	if (!patched_blob)
		return NULL;

	memcpy(patched_blob, words, injection_offset * 4);
	patched_blob[injection_offset++] = 3 << 16 | 71; // OpDecorate(71)
	patched_blob[injection_offset++] = binding_id;
	patched_blob[injection_offset++] = 25; // NonReadable
	memcpy(patched_blob + injection_offset, &words[injection_offset - 3], len - ((injection_offset - 3) * 4));

	return patched_blob;
}



static VkImage vulkan_create_image(VkDevice device, VkImageType image_type, VkFormat format, const uint32_t width, const uint32_t height, const uint32_t depth)
{
	VkImage image;
	VkImageCreateInfo image_create_info = {};
	image_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_create_info.extent.width = width;
	image_create_info.extent.height = height;
	image_create_info.extent.depth = depth;
	image_create_info.imageType = image_type;
	image_create_info.mipLevels = 1;
	image_create_info.arrayLayers = 1;
	image_create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_create_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	image_create_info.format = format;
	VkResult result = vkCreateImage(device, &image_create_info, NULL, &image);
	if (result != VK_SUCCESS)
	{
		PyObject* exception = PyExc_Exception;
		switch (image_type)
		{
		case(VK_IMAGE_TYPE_1D):
			exception = Compushady_Texture1DError;
			break;
		case(VK_IMAGE_TYPE_2D):
			exception = Compushady_Texture2DError;
			break;
		case(VK_IMAGE_TYPE_3D):
			exception = Compushady_Texture3DError;
			break;
		default:
			break;
		}
		PyErr_Format(exception, "unable to create vulkan Image");
		return VK_NULL_HANDLE;
	}
	return image;
}

static void vulkan_Resource_dealloc(vulkan_Resource* self)
{
	if (self->py_device)
	{
		VkDevice device = self->py_device->device;
		if (self->image_view)
			vkDestroyImageView(device, self->image_view, NULL);
		if (self->buffer_view)
			vkDestroyBufferView(device, self->buffer_view, NULL);
		if (self->memory)
			vkFreeMemory(device, self->memory, NULL);
		if (self->image)
			vkDestroyImage(self->py_device->device, self->image, NULL);
		if (self->buffer)
			vkDestroyBuffer(self->py_device->device, self->buffer, NULL);
		Py_DECREF(self->py_device);
	}

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject vulkan_Resource_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.vulkan.Resource", /* tp_name */
	sizeof(vulkan_Resource),											  /* tp_basicsize */
	0,																	  /* tp_itemsize */
	(destructor)vulkan_Resource_dealloc,								  /* tp_dealloc */
	0,																	  /* tp_print */
	0,																	  /* tp_getattr */
	0,																	  /* tp_setattr */
	0,																	  /* tp_reserved */
	0,																	  /* tp_repr */
	0,																	  /* tp_as_number */
	0,																	  /* tp_as_sequence */
	0,																	  /* tp_as_mapping */
	0,																	  /* tp_hash  */
	0,																	  /* tp_call */
	0,																	  /* tp_str */
	0,																	  /* tp_getattro */
	0,																	  /* tp_setattro */
	0,																	  /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,													  /* tp_flags */
	"compushady vulkan Resource",										  /* tp_doc */
};

static void vulkan_Device_dealloc(vulkan_Device* self)
{
	Py_XDECREF(self->name);

	if (self->device)
	{
		if (self->command_pool)
		{
			if (self->command_buffer)
			{
				vkFreeCommandBuffers(self->device, self->command_pool, 1, &self->command_buffer);
			}
			vkDestroyCommandPool(self->device, self->command_pool, NULL);
		}
		vkDestroyDevice(self->device, NULL);
	}

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject vulkan_Device_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.vulkan.Device", /* tp_name */
	sizeof(vulkan_Device),												/* tp_basicsize */
	0,																	/* tp_itemsize */
	(destructor)vulkan_Device_dealloc,									/* tp_dealloc */
	0,																	/* tp_print */
	0,																	/* tp_getattr */
	0,																	/* tp_setattr */
	0,																	/* tp_reserved */
	0,																	/* tp_repr */
	0,																	/* tp_as_number */
	0,																	/* tp_as_sequence */
	0,																	/* tp_as_mapping */
	0,																	/* tp_hash  */
	0,																	/* tp_call */
	0,																	/* tp_str */
	0,																	/* tp_getattro */
	0,																	/* tp_setattro */
	0,																	/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,													/* tp_flags */
	"compushady vulkan Device",											/* tp_doc */
};

static void vulkan_Compute_dealloc(vulkan_Compute* self)
{
	if (self->py_device)
	{
		VkDevice device = self->py_device->device;
		if (self->pipeline)
			vkDestroyPipeline(device, self->pipeline, NULL);
		if (self->pipeline_layout)
			vkDestroyPipelineLayout(device, self->pipeline_layout, NULL);
		if (self->descriptor_pool)
		{
			// descriptor sets free is implicit when destroying the descriptor pool
			/*if (self->descriptor_set)
				vkFreeDescriptorSets(device, self->descriptor_pool, 1, &self->descriptor_set);*/
			vkDestroyDescriptorPool(device, self->descriptor_pool, NULL);
		}
		if (self->descriptor_set_layout)
			vkDestroyDescriptorSetLayout(device, self->descriptor_set_layout, NULL);
		if (self->shader_module)
			vkDestroyShaderModule(device, self->shader_module, NULL);

		Py_DECREF(self->py_device);
	}

	Py_XDECREF(self->py_cbv_list);
	Py_XDECREF(self->py_srv_list);
	Py_XDECREF(self->py_uav_list);

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject vulkan_Compute_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.vulkan.Compute", /* tp_name */
	sizeof(vulkan_Compute),												 /* tp_basicsize */
	0,																	 /* tp_itemsize */
	(destructor)vulkan_Compute_dealloc,									 /* tp_dealloc */
	0,																	 /* tp_print */
	0,																	 /* tp_getattr */
	0,																	 /* tp_setattr */
	0,																	 /* tp_reserved */
	0,																	 /* tp_repr */
	0,																	 /* tp_as_number */
	0,																	 /* tp_as_sequence */
	0,																	 /* tp_as_mapping */
	0,																	 /* tp_hash  */
	0,																	 /* tp_call */
	0,																	 /* tp_str */
	0,																	 /* tp_getattro */
	0,																	 /* tp_setattro */
	0,																	 /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,													 /* tp_flags */
	"compushady vulkan Compute",										 /* tp_doc */
};

static void vulkan_Swapchain_dealloc(vulkan_Swapchain* self)
{
	self->images = {};

	if (self->py_device)
	{
		if (self->copy_semaphore)
			vkDestroySemaphore(self->py_device->device, self->copy_semaphore, NULL);
		if (self->present_semaphore)
			vkDestroySemaphore(self->py_device->device, self->present_semaphore, NULL);
		if (self->swapchain) // here images are destroyed too
			vkDestroySwapchainKHR(self->py_device->device, self->swapchain, NULL);
		if (self->surface)
			vkDestroySurfaceKHR(vulkan_instance, self->surface, NULL);
		Py_DECREF(self->py_device);
	}

	Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyTypeObject vulkan_Swapchain_Type = {
	PyVarObject_HEAD_INIT(NULL, 0) "compushady.backends.vulkan.Swapchain", /* tp_name */
	sizeof(vulkan_Swapchain),											   /* tp_basicsize */
	0,																	   /* tp_itemsize */
	(destructor)vulkan_Swapchain_dealloc,								   /* tp_dealloc */
	0,																	   /* tp_print */
	0,																	   /* tp_getattr */
	0,																	   /* tp_setattr */
	0,																	   /* tp_reserved */
	0,																	   /* tp_repr */
	0,																	   /* tp_as_number */
	0,																	   /* tp_as_sequence */
	0,																	   /* tp_as_mapping */
	0,																	   /* tp_hash  */
	0,																	   /* tp_call */
	0,																	   /* tp_str */
	0,																	   /* tp_getattro */
	0,																	   /* tp_setattro */
	0,																	   /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT,													   /* tp_flags */
	"compushady vulkan Swapchain",										   /* tp_doc */
};

static PyMemberDef vulkan_Device_members[] = {
	{"name", T_OBJECT_EX, offsetof(vulkan_Device, name), 0, "device name/description"},
	{"dedicated_video_memory", T_ULONGLONG, offsetof(vulkan_Device, dedicated_video_memory), 0, "device dedicated video memory amount"},
	{"dedicated_system_memory", T_ULONGLONG, offsetof(vulkan_Device, dedicated_system_memory), 0, "device dedicated system memory amount"},
	{"shared_system_memory", T_ULONGLONG, offsetof(vulkan_Device, shared_system_memory), 0, "device shared system memory amount"},
	{"vendor_id", T_UINT, offsetof(vulkan_Device, vendor_id), 0, "device VendorId"},
	{"device_id", T_UINT, offsetof(vulkan_Device, vendor_id), 0, "device DeviceId"},
	{"is_hardware", T_BOOL, offsetof(vulkan_Device, is_hardware), 0, "returns True if this is a hardware device and not an emulated/software one"},
	{"is_discrete", T_BOOL, offsetof(vulkan_Device, is_discrete), 0, "returns True if this is a discrete device"},
	{NULL} /* Sentinel */
};

static VkBool32 vulkan_debug_message_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	vulkan_debug_messages.push_back(pCallbackData->pMessage);
	return VK_FALSE;
}

static PyObject* vulkan_instance_check()
{
	if (vulkan_instance != VK_NULL_HANDLE)
		Py_RETURN_NONE;

	std::vector<const char*> extensions;
	std::vector<const char*> layers;

	uint32_t layers_count;
	vkEnumerateInstanceLayerProperties(&layers_count, NULL);
	std::vector<VkLayerProperties> available_layers(layers_count);
	vkEnumerateInstanceLayerProperties(&layers_count, available_layers.data());
	if (vulkan_debug)
	{
		vulkan_debug = false;
		for (VkLayerProperties& layer_prop : available_layers)
		{
			if (!strcmp(layer_prop.layerName, "VK_LAYER_KHRONOS_validation"))
			{
				layers.push_back("VK_LAYER_KHRONOS_validation");
				vulkan_debug = true;
				break;
			}
		}
	}

	uint32_t extensions_count;
	vkEnumerateInstanceExtensionProperties(NULL, &extensions_count, NULL);
	std::vector<VkExtensionProperties> available_extensions(extensions_count);
	vkEnumerateInstanceExtensionProperties(nullptr, &extensions_count, available_extensions.data());

	for (VkExtensionProperties& extension_prop : available_extensions)
	{
		if (!strcmp(extension_prop.extensionName, VK_KHR_SURFACE_EXTENSION_NAME))
		{
			extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
			continue;
		}

#ifdef _WIN32
		if (!strcmp(extension_prop.extensionName, VK_KHR_WIN32_SURFACE_EXTENSION_NAME))
		{
			extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
#ifdef __APPLE__
		if (!strcmp(extension_prop.extensionName, VK_EXT_METAL_SURFACE_EXTENSION_NAME))
		{
			extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#else
		if (!strcmp(extension_prop.extensionName, VK_KHR_XLIB_SURFACE_EXTENSION_NAME))
		{
			extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
#endif
			continue;
		}

		if (vulkan_debug && !strcmp(extension_prop.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			continue;
		}
	}

	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "compushady";
	app_info.applicationVersion = 0xDEADBEEF;
	app_info.pEngineName = app_info.pApplicationName;
	app_info.engineVersion = 0xDEADBEEF;
	app_info.apiVersion = VK_API_VERSION_1_1;

	for (;;)
	{
		VkInstanceCreateInfo instance_create_info = {};
		instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		instance_create_info.pApplicationInfo = &app_info;

		instance_create_info.enabledExtensionCount = (uint32_t)extensions.size();
		instance_create_info.ppEnabledExtensionNames = extensions.data();
		instance_create_info.enabledLayerCount = (uint32_t)layers.size();
		instance_create_info.ppEnabledLayerNames = layers.data();

		VkResult result = vkCreateInstance(&instance_create_info, nullptr, &vulkan_instance);
		if (result != VK_SUCCESS)
		{
			if (result == VK_ERROR_EXTENSION_NOT_PRESENT && !extensions.empty())
			{
				extensions.clear();
				vulkan_supports_swapchain = false;
				continue;
			}
			else if (result == VK_ERROR_LAYER_NOT_PRESENT && !layers.empty())
			{
				layers.clear();
				vulkan_debug = false;
				continue;
			}
			return PyErr_Format(PyExc_Exception, "unable to create vulkan instance: %d", result);
		}

		vulkan_debug_messages = {};

		if (vulkan_debug)
		{
			VkDebugUtilsMessengerEXT messenger;
			VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {};
			debug_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debug_messenger_create_info.messageSeverity = /*VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |*/
				VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			debug_messenger_create_info.pfnUserCallback = vulkan_debug_message_callback;
			PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(vulkan_instance, "vkCreateDebugUtilsMessengerEXT");
			if (func)
				func(vulkan_instance, &debug_messenger_create_info, NULL, &messenger); // no need to check for error, in the worst case we will get messages on stdout
		}
		break;
	}
	Py_RETURN_NONE;
}

static vulkan_Device* vulkan_Device_get_device(vulkan_Device * self)
{
	if (self->device)
	{
		return self;
	}

	vkGetPhysicalDeviceFeatures(self->physical_device, &self->features);

	uint32_t num_queue_families = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(self->physical_device, &num_queue_families, nullptr);

	std::vector<VkQueueFamilyProperties> queue_families(num_queue_families);
	vkGetPhysicalDeviceQueueFamilyProperties(self->physical_device, &num_queue_families, queue_families.data());

	for (uint32_t queue_family_index = 0; queue_family_index < num_queue_families; queue_family_index++)
	{
		if (queue_families[queue_family_index].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			VkDeviceQueueCreateInfo queue_create_info = {};
			queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_info.queueCount = 1;
			queue_create_info.queueFamilyIndex = queue_family_index;
			float priorities[] = { 1.0f };
			queue_create_info.pQueuePriorities = priorities;
			VkDeviceCreateInfo create_info = {};
			create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			create_info.pQueueCreateInfos = &queue_create_info;
			create_info.queueCreateInfoCount = 1;
			std::vector<const char*> extensions;
			if (vulkan_supports_swapchain)
				extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef __APPLE__
			extensions.push_back("VK_KHR_portability_subset");
#endif
			std::vector<const char*> layers;
			layers.push_back("VK_LAYER_KHRONOS_validation");
			create_info.enabledExtensionCount = (uint32_t)extensions.size();
			create_info.ppEnabledExtensionNames = extensions.data();
			create_info.enabledLayerCount = (uint32_t)layers.size();
			create_info.ppEnabledLayerNames = layers.data();

			VkDevice device;
			VkResult result = vkCreateDevice(self->physical_device, &create_info, nullptr, &device);
			if (result != VK_SUCCESS)
			{
				PyErr_Format(PyExc_Exception, "Unable to create vulkan device");
				return NULL;
			}

			VkQueue queue;
			vkGetDeviceQueue(device, queue_family_index, 0, &queue);

			VkCommandPool command_pool;
			VkCommandPoolCreateInfo command_pool_create_info = {};
			command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			command_pool_create_info.queueFamilyIndex = queue_family_index;

			result = vkCreateCommandPool(device, &command_pool_create_info, nullptr, &command_pool);
			if (result != VK_SUCCESS)
			{
				vkDestroyDevice(device, NULL);
				PyErr_Format(PyExc_Exception, "unable to create vulkan Command Pool");
				return NULL;
			}

			VkCommandBuffer command_buffer;
			VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
			command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			command_buffer_allocate_info.commandPool = command_pool;
			command_buffer_allocate_info.commandBufferCount = 1;

			result = vkAllocateCommandBuffers(device, &command_buffer_allocate_info, &command_buffer);
			if (result != VK_SUCCESS)
			{
				vkDestroyCommandPool(device, command_pool, NULL);
				vkDestroyDevice(device, NULL);
				PyErr_Format(PyExc_Exception, "unable to create vulkan Command Buffer");
				return NULL;
			}

			self->device = device;
			self->queue = queue;
			self->queue_family_index = queue_family_index;
			self->command_pool = command_pool;
			self->command_buffer = command_buffer;
			return self;
		}
	}

	PyErr_Format(PyExc_Exception, "unable to create vulkan device");
	return NULL;
}

static uint32_t vulkan_get_memory_type_index_by_flag(VkPhysicalDeviceMemoryProperties * mem_props, VkMemoryPropertyFlagBits flag)
{
	for (uint32_t i = 0; i < mem_props->memoryTypeCount; i++)
	{
		if (mem_props->memoryTypes[i].propertyFlags & flag)
		{
			return i;
		}
	}

	return 0;
}

static PyObject* vulkan_Device_create_buffer(vulkan_Device * self, PyObject * args)
{
	int heap;
	size_t size;
	uint32_t stride;
	int format;
	if (!PyArg_ParseTuple(args, "iKIi", &heap, &size, &stride, &format))
		return NULL;

	if (format > 0)
	{
		if (vulkan_formats.find(format) == vulkan_formats.end())
		{
			return PyErr_Format(Compushady_BufferError, "invalid pixel format");
		}
	}

	if (!size)
		return PyErr_Format(Compushady_BufferError, "zero size buffer");

	vulkan_Device* py_device = vulkan_Device_get_device(self);
	if (!py_device)
		return NULL;

	VkBufferCreateInfo buffer_create_info = {};
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
		VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

	VkMemoryPropertyFlagBits mem_flag = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	switch (heap)
	{
	case COMPUSHADY_HEAP_DEFAULT:

		break;
	case COMPUSHADY_HEAP_UPLOAD:
		mem_flag = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;
	case COMPUSHADY_HEAP_READBACK:
		mem_flag = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
		break;
	default:
		return PyErr_Format(Compushady_BufferError, "Invalid heap type: %d", heap);
	}

	vulkan_Resource* py_resource = (vulkan_Resource*)PyObject_New(vulkan_Resource, &vulkan_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Buffer");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	VkResult result = vkCreateBuffer(py_device->device, &buffer_create_info, NULL, &py_resource->buffer);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(Compushady_BufferError, "unable to create vulkan Buffer");
	}

	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements(py_device->device, py_resource->buffer, &requirements);

	VkMemoryAllocateInfo allocate_info = {};
	allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = vulkan_get_memory_type_index_by_flag(&self->mem_props, mem_flag);

	result = vkAllocateMemory(py_device->device, &allocate_info, NULL, &py_resource->memory);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(Compushady_BufferError, "unable to create vulkan Buffer memory");
	}

	result = vkBindBufferMemory(py_device->device, py_resource->buffer, py_resource->memory, 0);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(Compushady_BufferError, "unable to bind vulkan Buffer memory");
	}

	if (format > 0)
	{
		py_resource->format = vulkan_formats[format].first;
		VkBufferViewCreateInfo buffer_view_create_info = {};
		buffer_view_create_info.format = vulkan_formats[format].first;
		buffer_view_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
		buffer_view_create_info.buffer = py_resource->buffer;

		buffer_view_create_info.range = VK_WHOLE_SIZE;

		result = vkCreateBufferView(py_device->device, &buffer_view_create_info, NULL, &py_resource->buffer_view);
		if (result != VK_SUCCESS)
		{
			Py_DECREF(py_resource);
			return PyErr_Format(Compushady_BufferError, "unable to create vulkan Buffer View");
		}
	}

	py_resource->size = size;
	py_resource->stride = stride;
	py_resource->descriptor_buffer_info.buffer = py_resource->buffer;
	py_resource->descriptor_buffer_info.range = size;

	return (PyObject*)py_resource;
}

static bool vulkan_texture_set_layout(vulkan_Device * py_device, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
{

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	VkImageMemoryBarrier image_memory_barrier = {};
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.image = image;
	image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_memory_barrier.subresourceRange.levelCount = 1;
	image_memory_barrier.subresourceRange.layerCount = 1;
	image_memory_barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	image_memory_barrier.oldLayout = old_layout;
	image_memory_barrier.newLayout = new_layout;

	vkBeginCommandBuffer(py_device->command_buffer, &begin_info);
	vkCmdPipelineBarrier(py_device->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 1, &image_memory_barrier);
	vkEndCommandBuffer(py_device->command_buffer);

	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pCommandBuffers = &py_device->command_buffer;
	submit_info.commandBufferCount = 1;

	VkResult result = vkQueueSubmit(py_device->queue, 1, &submit_info, VK_NULL_HANDLE);
	if (result == VK_SUCCESS)
	{
		Py_BEGIN_ALLOW_THREADS;
		vkQueueWaitIdle(py_device->queue);
		Py_END_ALLOW_THREADS;
	}

	return result == VK_SUCCESS;
}
static PyObject* vulkan_Device_create_texture2d(vulkan_Device * self, PyObject * args)
{
	uint32_t width;
	uint32_t height;
	VkFormat format;
	if (!PyArg_ParseTuple(args, "IIi", &width, &height, &format))
		return NULL;

	if (vulkan_formats.find(format) == vulkan_formats.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	vulkan_Device* py_device = vulkan_Device_get_device(self);
	if (!py_device)
		return NULL;

	vulkan_Resource* py_resource = (vulkan_Resource*)PyObject_New(vulkan_Resource, &vulkan_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Texture2D");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->image = vulkan_create_image(py_device->device, VK_IMAGE_TYPE_2D, vulkan_formats[format].first, width, height, 1);
	if (!py_resource->image)
	{
		Py_DECREF(py_resource);
		return NULL;
	}

	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(py_device->device, py_resource->image, &requirements);

	VkMemoryAllocateInfo allocate_info = {};
	allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = vulkan_get_memory_type_index_by_flag(&self->mem_props, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkResult result = vkAllocateMemory(py_device->device, &allocate_info, NULL, &py_resource->memory);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to create vulkan Image memory");
	}

	result = vkBindImageMemory(py_device->device, py_resource->image, py_resource->memory, 0);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to bind vulkan Image memory");
	}

	VkImageViewCreateInfo image_view_create_info = {};
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image = py_resource->image;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.format = vulkan_formats[format].first;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.layerCount = 1;

	result = vkCreateImageView(py_device->device, &image_view_create_info, NULL, &py_resource->image_view);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to create vulkan Image View");
	}

	if (!vulkan_texture_set_layout(py_device, py_resource->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL))
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to set vulkan Image layout");
	}

	py_resource->image_extent.width = width;
	py_resource->image_extent.height = height;
	py_resource->image_extent.depth = 1;
	py_resource->descriptor_image_info.imageView = py_resource->image_view;
	py_resource->descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	py_resource->row_pitch = width * vulkan_formats[format].second;
	py_resource->size = py_resource->row_pitch * height; // always assume a packed configuration
	py_resource->format = image_view_create_info.format;

	return (PyObject*)py_resource;
}

static PyObject* vulkan_Device_create_texture2d_from_native(vulkan_Device * self, PyObject * args)
{
	unsigned long long texture_ptr;
	uint32_t width;
	uint32_t height;
	VkFormat format;
	if (!PyArg_ParseTuple(args, "KIIi", &texture_ptr, &width, &height, &format))
		return NULL;

	if (vulkan_formats.find(format) == vulkan_formats.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	vulkan_Device* py_device = vulkan_Device_get_device(self);
	if (!py_device)
		return NULL;

	vulkan_Resource* py_resource = (vulkan_Resource*)PyObject_New(vulkan_Resource, &vulkan_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Texture2D");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->image = (VkImage)texture_ptr;

	VkImageViewCreateInfo image_view_create_info = {};
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image = py_resource->image;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_create_info.format = vulkan_formats[format].first;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.layerCount = 1;

	VkResult result = vkCreateImageView(py_device->device, &image_view_create_info, NULL, &py_resource->image_view);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to create vulkan Image View");
	}

	py_resource->image_extent.width = width;
	py_resource->image_extent.height = height;
	py_resource->image_extent.depth = 1;
	py_resource->descriptor_image_info.imageView = py_resource->image_view;
	py_resource->descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	py_resource->row_pitch = width * vulkan_formats[format].second;
	py_resource->size = py_resource->row_pitch * height; // always assume a packed configuration
	py_resource->format = image_view_create_info.format;

	return (PyObject*)py_resource;
}

static PyObject* vulkan_Device_create_texture3d(vulkan_Device * self, PyObject * args)
{
	uint32_t width;
	uint32_t height;
	uint32_t depth;
	VkFormat format;
	if (!PyArg_ParseTuple(args, "IIIi", &width, &height, &depth, &format))
		return NULL;

	if (vulkan_formats.find(format) == vulkan_formats.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	vulkan_Device* py_device = vulkan_Device_get_device(self);
	if (!py_device)
		return NULL;

	vulkan_Resource* py_resource = (vulkan_Resource*)PyObject_New(vulkan_Resource, &vulkan_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Texture3D");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->image = vulkan_create_image(py_device->device, VK_IMAGE_TYPE_3D, vulkan_formats[format].first, width, height, depth);
	if (!py_resource->image)
	{
		Py_DECREF(py_resource);
		return NULL;
	}

	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(py_device->device, py_resource->image, &requirements);

	VkMemoryAllocateInfo allocate_info = {};
	allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = vulkan_get_memory_type_index_by_flag(&self->mem_props, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkResult result = vkAllocateMemory(py_device->device, &allocate_info, NULL, &py_resource->memory);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to create vulkan Image memory");
	}

	result = vkBindImageMemory(py_device->device, py_resource->image, py_resource->memory, 0);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to bind vulkan Image memory");
	}

	VkImageViewCreateInfo image_view_create_info = {};
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image = py_resource->image;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
	image_view_create_info.format = vulkan_formats[format].first;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.layerCount = 1;

	result = vkCreateImageView(py_device->device, &image_view_create_info, NULL, &py_resource->image_view);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to create vulkan Image View");
	}

	if (!vulkan_texture_set_layout(py_device, py_resource->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL))
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to set vulkan Image layout");
	}

	py_resource->image_extent.width = width;
	py_resource->image_extent.height = height;
	py_resource->image_extent.depth = depth;
	py_resource->descriptor_image_info.imageView = py_resource->image_view;
	py_resource->descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	py_resource->row_pitch = width * vulkan_formats[format].second;
	py_resource->size = py_resource->row_pitch * height * depth; // alway assume a packed configuration
	py_resource->format = image_view_create_info.format;

	return (PyObject*)py_resource;
}

static PyObject* vulkan_Device_create_texture1d(vulkan_Device * self, PyObject * args)
{
	uint32_t width;
	VkFormat format;
	if (!PyArg_ParseTuple(args, "Ii", &width, &format))
		return NULL;

	if (vulkan_formats.find(format) == vulkan_formats.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	vulkan_Device* py_device = vulkan_Device_get_device(self);
	if (!py_device)
		return NULL;

	vulkan_Resource* py_resource = (vulkan_Resource*)PyObject_New(vulkan_Resource, &vulkan_Resource_Type);
	if (!py_resource)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Texture1D");
	}
	COMPUSHADY_CLEAR(py_resource);
	py_resource->py_device = py_device;
	Py_INCREF(py_resource->py_device);

	py_resource->image = vulkan_create_image(py_device->device, VK_IMAGE_TYPE_1D, vulkan_formats[format].first, width, 1, 1);
	if (!py_resource->image)
	{
		Py_DECREF(py_resource);
		return NULL;
	}

	VkMemoryRequirements requirements;
	vkGetImageMemoryRequirements(py_device->device, py_resource->image, &requirements);

	VkMemoryAllocateInfo allocate_info = {};
	allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate_info.allocationSize = requirements.size;
	allocate_info.memoryTypeIndex = vulkan_get_memory_type_index_by_flag(&self->mem_props, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkResult result = vkAllocateMemory(py_device->device, &allocate_info, NULL, &py_resource->memory);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to create vulkan Image memory");
	}

	result = vkBindImageMemory(py_device->device, py_resource->image, py_resource->memory, 0);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to bind vulkan Image memory");
	}

	VkImageViewCreateInfo image_view_create_info = {};
	image_view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_create_info.image = py_resource->image;
	image_view_create_info.viewType = VK_IMAGE_VIEW_TYPE_1D;
	image_view_create_info.format = vulkan_formats[format].first;
	image_view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_create_info.subresourceRange.levelCount = 1;
	image_view_create_info.subresourceRange.layerCount = 1;

	result = vkCreateImageView(py_device->device, &image_view_create_info, NULL, &py_resource->image_view);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to create vulkan Image View");
	}

	if (!vulkan_texture_set_layout(py_device, py_resource->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL))
	{
		Py_DECREF(py_resource);
		return PyErr_Format(PyExc_MemoryError, "unable to set vulkan Image layout");
	}

	py_resource->image_extent.width = width;
	py_resource->image_extent.height = 1;
	py_resource->image_extent.depth = 1;
	py_resource->descriptor_image_info.imageView = py_resource->image_view;
	py_resource->descriptor_image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	py_resource->row_pitch = width * vulkan_formats[format].second;
	py_resource->size = py_resource->row_pitch; // alway assume a packed configuration
	py_resource->format = image_view_create_info.format;

	return (PyObject*)py_resource;
}

static PyObject* vulkan_Device_create_compute(vulkan_Device * self, PyObject * args, PyObject * kwds)
{
	const char* kwlist[] = { "shader", "cbv", "srv", "uav", NULL };
	Py_buffer view;
	PyObject* py_cbv = NULL;
	PyObject* py_srv = NULL;
	PyObject* py_uav = NULL;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "y*|OOO", (char**)kwlist,
		&view, &py_cbv, &py_srv, &py_uav))
		return NULL;

	vulkan_Device* py_device = vulkan_Device_get_device(self);
	if (!py_device)
		return NULL;

	std::vector<vulkan_Resource*> cbv;
	std::vector<vulkan_Resource*> srv;
	std::vector<vulkan_Resource*> uav;
	if (!compushady_check_descriptors(&vulkan_Resource_Type, py_cbv, cbv, py_srv, srv, py_uav, uav))
	{
		PyBuffer_Release(&view);
		return NULL;
	}

	std::vector<VkDescriptorSetLayoutBinding> layout_bindings;
	std::vector<VkDescriptorPoolSize> pool_sizes;
	std::unordered_map<VkDescriptorType, std::vector<vulkan_Resource*>> descriptors;
	std::vector<VkWriteDescriptorSet> write_descriptor_sets = {};

	VkShaderModuleCreateInfo shader_create_info = {};
	shader_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shader_create_info.codeSize = view.len;
	shader_create_info.pCode = (uint32_t*)(view.buf);

	uint32_t binding_offset = 0;
	for (vulkan_Resource* py_resource : cbv)
	{
		if (descriptors.find(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) == descriptors.end())
		{
			descriptors[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] = {};
		}
		descriptors[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER].push_back(py_resource);

		VkWriteDescriptorSet write_descriptor_set = {};
		write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptor_set.descriptorCount = 1;
		write_descriptor_set.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write_descriptor_set.dstBinding = binding_offset;
		write_descriptor_set.pBufferInfo = &py_resource->descriptor_buffer_info;
		write_descriptor_sets.push_back(write_descriptor_set);

		VkDescriptorSetLayoutBinding layout_binding = {};
		layout_binding.binding = binding_offset++;
		layout_binding.descriptorCount = 1;
		layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layout_bindings.push_back(layout_binding);
	}

	binding_offset = 1024;
	for (vulkan_Resource* py_resource : srv)
	{
		VkDescriptorType type = py_resource->buffer ? py_resource->buffer_view ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		if (descriptors.find(type) == descriptors.end())
		{
			descriptors[type] = {};
		}
		descriptors[type].push_back(py_resource);

		VkWriteDescriptorSet write_descriptor_set = {};
		write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptor_set.descriptorCount = 1;
		write_descriptor_set.descriptorType = type;
		write_descriptor_set.dstBinding = binding_offset;
		if (py_resource->buffer)
		{
			if (py_resource->buffer_view)
			{
				write_descriptor_set.pTexelBufferView = &py_resource->buffer_view;
			}
			else
			{
				write_descriptor_set.pBufferInfo = &py_resource->descriptor_buffer_info;
			}
		}
		else
		{
			write_descriptor_set.pImageInfo = &py_resource->descriptor_image_info;
		}
		write_descriptor_sets.push_back(write_descriptor_set);

		VkDescriptorSetLayoutBinding layout_binding = {};
		layout_binding.binding = binding_offset++;
		layout_binding.descriptorCount = 1;
		layout_binding.descriptorType = type;
		layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layout_bindings.push_back(layout_binding);
	}

	binding_offset = 2048;
	for (vulkan_Resource* py_resource : uav)
	{
		VkDescriptorType type = py_resource->buffer ? py_resource->buffer_view ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		if (descriptors.find(type) == descriptors.end())
		{
			descriptors[type] = {};
		}
		descriptors[type].push_back(py_resource);

		VkWriteDescriptorSet write_descriptor_set = {};
		write_descriptor_set.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptor_set.descriptorCount = 1;
		write_descriptor_set.descriptorType = type;
		write_descriptor_set.dstBinding = binding_offset;
		if (py_resource->buffer)
		{
			if (py_resource->buffer_view)
			{
				write_descriptor_set.pTexelBufferView = &py_resource->buffer_view;
			}
			else
			{
				write_descriptor_set.pBufferInfo = &py_resource->descriptor_buffer_info;
			}
		}
		else
		{
			write_descriptor_set.pImageInfo = &py_resource->descriptor_image_info;
			if (!py_device->features.shaderStorageImageReadWithoutFormat)
			{
				if (py_resource->format == VK_FORMAT_B8G8R8A8_UNORM || py_resource->format == VK_FORMAT_B8G8R8A8_SRGB)
				{
					uint32_t* patched_blob = vulkan_patch_spirv_unknown_uav(shader_create_info.pCode, shader_create_info.codeSize, binding_offset);
					if (patched_blob)
					{
						// first free old blob if required
						if (shader_create_info.pCode != view.buf)
							PyMem_Free((void*)shader_create_info.pCode);
						shader_create_info.pCode = patched_blob;
						shader_create_info.codeSize += 4 * 3;
					}
				}
			}
		}
		write_descriptor_sets.push_back(write_descriptor_set);

		VkDescriptorSetLayoutBinding layout_binding = {};
		layout_binding.binding = binding_offset++;
		layout_binding.descriptorCount = 1;
		layout_binding.descriptorType = type;
		layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		layout_bindings.push_back(layout_binding);
	}

	for (std::pair<VkDescriptorType, std::vector<vulkan_Resource*>> pair : descriptors)
	{
		VkDescriptorPoolSize pool_size = {};
		pool_size.descriptorCount = (uint32_t)pair.second.size();
		pool_size.type = pair.first;
		pool_sizes.push_back(pool_size);
	}

	const char* entry_point = vulkan_get_spirv_entry_point(shader_create_info.pCode, shader_create_info.codeSize);
	if (!entry_point)
	{
		if (shader_create_info.pCode != view.buf)
			PyMem_Free((void*)shader_create_info.pCode);
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_ValueError, "Invalid SPIR-V Shader, expected a GLCompute OpEntryPoint");
	}

	VkShaderModule shader_module;
	VkResult result = vkCreateShaderModule(py_device->device, &shader_create_info, nullptr, &shader_module);
	if (result != VK_SUCCESS)
	{
		if (shader_create_info.pCode != view.buf)
			PyMem_Free((void*)shader_create_info.pCode);
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_Exception, "Unable to create Shader Module");
	}

	if (shader_create_info.pCode != view.buf)
		PyMem_Free((void*)shader_create_info.pCode);
	PyBuffer_Release(&view);

	vulkan_Compute* py_compute = (vulkan_Compute*)PyObject_New(vulkan_Compute, &vulkan_Compute_Type);
	if (!py_compute)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Compute");
	}
	COMPUSHADY_CLEAR(py_compute);

	py_compute->py_cbv_list = PyList_New(0);
	py_compute->py_srv_list = PyList_New(0);
	py_compute->py_uav_list = PyList_New(0);

	for (vulkan_Resource* py_resource : cbv)
	{
		PyList_Append(py_compute->py_cbv_list, (PyObject*)py_resource);
	}

	for (vulkan_Resource* py_resource : srv)
	{
		PyList_Append(py_compute->py_srv_list, (PyObject*)py_resource);
	}

	for (vulkan_Resource* py_resource : uav)
	{
		PyList_Append(py_compute->py_uav_list, (PyObject*)py_resource);
	}

	py_compute->py_device = py_device;
	Py_INCREF(py_compute->py_device);
	py_compute->shader_module = shader_module;

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {};
	descriptor_set_layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptor_set_layout_create_info.bindingCount = (uint32_t)layout_bindings.size();
	descriptor_set_layout_create_info.pBindings = layout_bindings.data();

	result = vkCreateDescriptorSetLayout(py_device->device, &descriptor_set_layout_create_info, NULL, &py_compute->descriptor_set_layout);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_compute);
		return PyErr_Format(PyExc_Exception, "Unable to create Descriptor Set LayOut");
	}

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.poolSizeCount = (uint32_t)pool_sizes.size();
	pool_info.pPoolSizes = pool_sizes.data();
	pool_info.maxSets = 1;

	result = vkCreateDescriptorPool(py_device->device, &pool_info, NULL, &py_compute->descriptor_pool);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_compute);
		return PyErr_Format(PyExc_Exception, "Unable to create Descriptor Pool");
	}

	VkDescriptorSetAllocateInfo descriptor_set_alloc_info = {};
	descriptor_set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_alloc_info.descriptorPool = py_compute->descriptor_pool;
	descriptor_set_alloc_info.descriptorSetCount = 1;
	descriptor_set_alloc_info.pSetLayouts = &py_compute->descriptor_set_layout;

	result = vkAllocateDescriptorSets(py_device->device, &descriptor_set_alloc_info, &py_compute->descriptor_set);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_compute);
		return PyErr_Format(PyExc_Exception, "Unable to create Descriptor Set");
	}

	// update descriptors
	for (VkWriteDescriptorSet& write_descriptor_set : write_descriptor_sets)
	{
		write_descriptor_set.dstSet = py_compute->descriptor_set;
	}

	vkUpdateDescriptorSets(py_device->device, (uint32_t)write_descriptor_sets.size(), write_descriptor_sets.data(), 0, NULL);

	VkPipelineLayoutCreateInfo layout_create_info = {};
	layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout_create_info.pSetLayouts = &py_compute->descriptor_set_layout;
	layout_create_info.setLayoutCount = 1;

	result = vkCreatePipelineLayout(py_device->device, &layout_create_info, nullptr, &py_compute->pipeline_layout);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_compute);
		return PyErr_Format(PyExc_Exception, "Unable to create Pipeline Layout");
	}

	VkPipelineShaderStageCreateInfo stage_create_info = {};
	stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage_create_info.module = shader_module;
	stage_create_info.pName = entry_point;
	stage_create_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;

	VkComputePipelineCreateInfo pipeline_create_info = {};
	pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_create_info.stage = stage_create_info;
	pipeline_create_info.layout = py_compute->pipeline_layout;

	result = vkCreateComputePipelines(py_device->device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &py_compute->pipeline);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_compute);
		return PyErr_Format(PyExc_Exception, "Unable to create Compute Pipeline");
	}

	return (PyObject*)py_compute;
}

static PyObject* vulkan_Device_create_swapchain(vulkan_Device * self, PyObject * args)
{
	PyObject* py_window_handle;
	int format;
	uint32_t num_buffers;
	if (!PyArg_ParseTuple(args, "OiI", &py_window_handle, &format, &num_buffers))
		return NULL;

	if (vulkan_formats.find(format) == vulkan_formats.end())
	{
		return PyErr_Format(PyExc_ValueError, "invalid pixel format");
	}

	if (!vulkan_supports_swapchain)
	{
		return PyErr_Format(PyExc_Exception, "swapchain not supported");
	}

	vulkan_Device* py_device = vulkan_Device_get_device(self);
	if (!py_device)
		return NULL;

	vulkan_Swapchain* py_swapchain = (vulkan_Swapchain*)PyObject_New(vulkan_Swapchain, &vulkan_Swapchain_Type);
	if (!py_swapchain)
	{
		return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Swapchain");
	}
	COMPUSHADY_CLEAR(py_swapchain);

	py_swapchain->py_device = py_device;
	Py_INCREF(py_swapchain->py_device);
	py_swapchain->images = {};

#ifdef _WIN32
	if (!PyLong_Check(py_window_handle))
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_ValueError, "window handle must be an integer");
	}

	HWND window = (HWND)PyLong_AsUnsignedLongLong(py_window_handle);

	VkWin32SurfaceCreateInfoKHR surface_create_info = {};
	surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surface_create_info.hinstance = GetModuleHandle(NULL);
	surface_create_info.hwnd = window;

	VkResult result = vkCreateWin32SurfaceKHR(vulkan_instance, &surface_create_info, NULL, &py_swapchain->surface);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_Exception, "Unable to create win32 surface");
	}
#else
#ifdef __APPLE__
	if (!PyLong_Check(py_window_handle))
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_ValueError, "window handle must be an integer");
	}

	CAMetalLayer* metal_layer = (CAMetalLayer*)PyLong_AsUnsignedLongLong(py_window_handle);

	VkMetalSurfaceCreateInfoEXT metal_surface_create_info = {};
	metal_surface_create_info.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
	metal_surface_create_info.pLayer = metal_layer;

	VkResult result = vkCreateMetalSurfaceEXT(vulkan_instance, &metal_surface_create_info, NULL, &py_swapchain->surface);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_Exception, "Unable to create metal surface");
	}
#else
	if (!PyTuple_Check(py_window_handle))
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_ValueError, "window handle must be a tuple");
	}

	void* display;
	void* window;

	if (!PyArg_ParseTuple(py_window_handle, "KK", &display, &window))
	{
		Py_DECREF(py_swapchain);
		return NULL;
	}

	VkXlibSurfaceCreateInfoKHR surface_create_info = {};
	surface_create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
	surface_create_info.dpy = (Display*)display;
	surface_create_info.window = (Window)window;

	VkResult result = vkCreateXlibSurfaceKHR(vulkan_instance, &surface_create_info, NULL, &py_swapchain->surface);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_Exception, "Unable to create xlib surface");
	}
#endif
#endif

	VkBool32 supported;
	result = vkGetPhysicalDeviceSurfaceSupportKHR(self->physical_device, self->queue_family_index, py_swapchain->surface, &supported);
	if (result != VK_SUCCESS || !supported)
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_Exception, "swapchain not supported for this queue family");
	}

	VkSurfaceCapabilitiesKHR surface_capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(self->physical_device, py_swapchain->surface, &surface_capabilities);

	VkSwapchainCreateInfoKHR swapchain_create_info = {};
	swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchain_create_info.minImageCount = num_buffers;
	swapchain_create_info.imageFormat = vulkan_formats[format].first;
	swapchain_create_info.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	swapchain_create_info.imageExtent = surface_capabilities.currentExtent;
	swapchain_create_info.imageArrayLayers = 1;
	swapchain_create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchain_create_info.preTransform = surface_capabilities.currentTransform;
	swapchain_create_info.clipped = VK_TRUE;
	swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR; // VK_PRESENT_MODE_MAILBOX_KHR;
	swapchain_create_info.surface = py_swapchain->surface;

	result = vkCreateSwapchainKHR(py_device->device, &swapchain_create_info, NULL, &py_swapchain->swapchain);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_Exception, "Unable to create vulkan Swapchain");
	}

	uint32_t swapchain_images_num = 0;
	vkGetSwapchainImagesKHR(py_device->device, py_swapchain->swapchain, &swapchain_images_num, NULL);
	py_swapchain->images.resize(swapchain_images_num);
	vkGetSwapchainImagesKHR(py_device->device, py_swapchain->swapchain, &swapchain_images_num, py_swapchain->images.data());

	for (VkImage image : py_swapchain->images)
	{
		if (!vulkan_texture_set_layout(py_device, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR))
		{
			Py_DECREF(py_swapchain);
			return PyErr_Format(PyExc_Exception, "Unable to update vulkan Swapchain images layout");
		}
	}

	VkSemaphoreCreateInfo semaphore_create_info = {};
	semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	result = vkCreateSemaphore(py_device->device, &semaphore_create_info, NULL, &py_swapchain->copy_semaphore);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_Exception, "Unable to create vulkan Semaphore");
	}

	result = vkCreateSemaphore(py_device->device, &semaphore_create_info, NULL, &py_swapchain->present_semaphore);
	if (result != VK_SUCCESS)
	{
		Py_DECREF(py_swapchain);
		return PyErr_Format(PyExc_Exception, "Unable to create vulkan Semaphore");
	}

	py_swapchain->image_extent = swapchain_create_info.imageExtent;

	return (PyObject*)py_swapchain;
}

static PyObject* vulkan_Device_get_debug_messages(vulkan_Device * self, PyObject * args)
{
	PyObject* py_list = PyList_New(0);

	for (const std::string& message : vulkan_debug_messages)
	{
		PyList_Append(py_list, PyUnicode_FromString(message.c_str()));
	}
	vulkan_debug_messages = {};

	return py_list;
}

static PyMethodDef vulkan_Device_methods[] = {
	{"create_buffer", (PyCFunction)vulkan_Device_create_buffer, METH_VARARGS, "Creates a Buffer object"},
	{"create_texture1d", (PyCFunction)vulkan_Device_create_texture1d, METH_VARARGS, "Creates a Texture1D object"},
	{"create_texture2d", (PyCFunction)vulkan_Device_create_texture2d, METH_VARARGS, "Creates a Texture2D object"},
	{"create_texture3d", (PyCFunction)vulkan_Device_create_texture3d, METH_VARARGS, "Creates a Texture3D object"},
	/*{"create_buffer_from_native", (PyCFunction)d3d12_Device_create_buffer_from_native, METH_VARARGS, "Creates a Buffer object from a low level pointer"},*/
	{"create_texture2d_from_native", (PyCFunction)vulkan_Device_create_texture2d_from_native, METH_VARARGS, "Creates a Texture2D object from a low level pointer"},
	{"get_debug_messages", (PyCFunction)vulkan_Device_get_debug_messages, METH_VARARGS, "Get Device's debug messages"},
	{"create_compute", (PyCFunction)vulkan_Device_create_compute, METH_VARARGS | METH_KEYWORDS, "Creates a Compute object"},
	{"create_swapchain", (PyCFunction)vulkan_Device_create_swapchain, METH_VARARGS, "Creates a Swapchain object"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* vulkan_get_discovered_devices(PyObject * self)
{
	if (!vulkan_instance_check())
		return NULL;

	PyObject* py_list = PyList_New(0);

	uint32_t num_devices;
	vkEnumeratePhysicalDevices(vulkan_instance, &num_devices, nullptr);

	std::vector<VkPhysicalDevice> devices(num_devices);
	vkEnumeratePhysicalDevices(vulkan_instance, &num_devices, devices.data());

	for (auto device : devices)
	{
		VkPhysicalDeviceProperties prop;
		vkGetPhysicalDeviceProperties(device, &prop);

		vulkan_Device* py_device = (vulkan_Device*)PyObject_New(vulkan_Device, &vulkan_Device_Type);
		if (!py_device)
		{
			Py_DECREF(py_list);
			return PyErr_Format(PyExc_MemoryError, "unable to allocate vulkan Device");
		}
		COMPUSHADY_CLEAR(py_device);

		py_device->physical_device = device;
		py_device->name = PyUnicode_FromString(prop.deviceName);
		vkGetPhysicalDeviceMemoryProperties(device, &py_device->mem_props);

		for (size_t i = 0; i < py_device->mem_props.memoryHeapCount; i++)
		{
			if (py_device->mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
			{
				py_device->dedicated_video_memory += py_device->mem_props.memoryHeaps[i].size;
			}
			else
			{
				py_device->shared_system_memory += py_device->mem_props.memoryHeaps[i].size;
			}
		}
		py_device->vendor_id = prop.vendorID;
		py_device->device_id = prop.deviceID;
		py_device->is_hardware = prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU || prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
		py_device->is_discrete = prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
		PyList_Append(py_list, (PyObject*)py_device);
		Py_DECREF(py_device);
	}

	return py_list;
}

static PyMemberDef vulkan_Resource_members[] = {
	{"size", T_ULONGLONG, offsetof(vulkan_Resource, size), 0, "resource size"},
	{"width", T_UINT, offsetof(vulkan_Resource, image_extent) + offsetof(VkExtent3D, width), 0, "resource width"},
	{"height", T_UINT, offsetof(vulkan_Resource, image_extent) + offsetof(VkExtent3D, height), 0, "resource height"},
	{"depth", T_UINT, offsetof(vulkan_Resource, image_extent) + offsetof(VkExtent3D, depth), 0, "resource depth"},
	{"row_pitch", T_UINT, offsetof(vulkan_Resource, row_pitch), 0, "resource row pitch"},
	{NULL} /* Sentinel */
};

static PyMemberDef vulkan_Swapchain_members[] = {
	{"width", T_UINT, offsetof(vulkan_Swapchain, image_extent) + offsetof(VkExtent2D, width), 0, "swapchain width"},
	{"height", T_UINT, offsetof(vulkan_Swapchain, image_extent) + offsetof(VkExtent2D, height), 0, "swapchain height"},
	{NULL} /* Sentinel */
};

static PyObject* vulkan_Resource_upload(vulkan_Resource * self, PyObject * args)
{
	Py_buffer view;
	size_t offset = 0;
	if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
		return NULL;

	if ((size_t)offset + view.len > self->size)
	{
		size_t size = view.len;
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_ValueError, "supplied buffer is bigger than resource size: (offset %llu) %llu (expected no more than %llu)", offset, size, self->size);
	}

	char* mapped_data;
	VkResult result = vkMapMemory(self->py_device->device, self->memory, 0, self->size, 0, (void**)&mapped_data);
	if (result != VK_SUCCESS)
	{
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_Exception, "Unable to Map VkDeviceMemory");
	}

	memcpy(mapped_data + offset, view.buf, view.len);
	vkUnmapMemory(self->py_device->device, self->memory);
	PyBuffer_Release(&view);

	Py_RETURN_NONE;
}

static PyObject* vulkan_Resource_upload2d(vulkan_Resource * self, PyObject * args)
{
	Py_buffer view;
	uint32_t pitch;
	uint32_t width;
	uint32_t height;
	uint32_t bytes_per_pixel;
	if (!PyArg_ParseTuple(args, "y*IIII", &view, &pitch, &width, &height, &bytes_per_pixel))
		return NULL;

	char* mapped_data;
	VkResult result = vkMapMemory(self->py_device->device, self->memory, 0, self->size, 0, (void**)&mapped_data);
	if (result != VK_SUCCESS)
	{
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_Exception, "Unable to Map VkDeviceMemory");
	}

	size_t offset = 0;
	size_t remains = view.len;
	size_t resource_remains = self->size;
	for (uint32_t y = 0; y < height; y++)
	{
		size_t amount = Py_MIN(width * bytes_per_pixel, Py_MIN(remains, resource_remains));
		memcpy(mapped_data + (pitch * y), (char*)view.buf + offset, amount);
		remains -= amount;
		if (remains == 0)
			break;
		resource_remains -= amount;
		offset += amount;
	}

	memcpy(mapped_data + offset, view.buf, view.len);
	vkUnmapMemory(self->py_device->device, self->memory);
	PyBuffer_Release(&view);

	Py_RETURN_NONE;
}

static PyObject* vulkan_Resource_upload_chunked(vulkan_Resource * self, PyObject * args)
{
	Py_buffer view;
	uint32_t stride;
	Py_buffer filler;
	if (!PyArg_ParseTuple(args, "y*Iy*", &view, &stride, &filler))
		return NULL;

	size_t elements = view.len / stride;
	size_t additional_bytes = elements * filler.len;

	if (view.len + additional_bytes > self->size)
	{
		PyBuffer_Release(&view);
		PyBuffer_Release(&filler);
		return PyErr_Format(PyExc_ValueError, "supplied buffer is bigger than resource size: %llu (expected no more than %llu)", view.len + additional_bytes, self->size);
	}

	char* mapped_data;
	VkResult result = vkMapMemory(self->py_device->device, self->memory, 0, self->size, 0, (void**)&mapped_data);
	if (result != VK_SUCCESS)
	{
		PyBuffer_Release(&view);
		PyBuffer_Release(&filler);
		return PyErr_Format(PyExc_Exception, "Unable to Map VkDeviceMemory");
	}

	size_t offset = 0;
	for (uint32_t i = 0; i < elements; i++)
	{
		memcpy(mapped_data + offset, (char*)view.buf + (i * stride), stride);
		offset += stride;
		memcpy(mapped_data + offset, (char*)filler.buf, filler.len);
		offset += filler.len;
	}

	vkUnmapMemory(self->py_device->device, self->memory);
	PyBuffer_Release(&view);
	PyBuffer_Release(&filler);
	Py_RETURN_NONE;
}

static PyObject* vulkan_Resource_readback(vulkan_Resource * self, PyObject * args)
{
	size_t size;
	size_t offset;
	if (!PyArg_ParseTuple(args, "KK", &size, &offset))
		return NULL;

	if (size == 0)
		size = self->size - offset;

	if (offset + size > self->size)
	{
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: (offset %llu) %llu (expected no more than %llu)", offset, size, self->size);
	}

	char* mapped_data;
	VkResult result = vkMapMemory(self->py_device->device, self->memory, 0, self->size, 0, (void**)&mapped_data);
	if (result != VK_SUCCESS)
	{
		return PyErr_Format(PyExc_Exception, "Unable to Map VkDeviceMemory");
	}

	PyObject* py_bytes = PyBytes_FromStringAndSize(mapped_data + offset, size);
	vkUnmapMemory(self->py_device->device, self->memory);
	return py_bytes;
}

static PyObject* vulkan_Resource_readback_to_buffer(vulkan_Resource * self, PyObject * args)
{
	Py_buffer view;
	size_t offset = 0;
	if (!PyArg_ParseTuple(args, "y*K", &view, &offset))
		return NULL;

	if (offset > self->size)
	{
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: %llu (expected no more than %llu)", offset, self->size);
	}

	char* mapped_data;
	VkResult result = vkMapMemory(self->py_device->device, self->memory, 0, self->size, 0, (void**)&mapped_data);
	if (result != VK_SUCCESS)
	{
		PyBuffer_Release(&view);
		return PyErr_Format(PyExc_Exception, "Unable to Map VkDeviceMemory");
	}

	memcpy(view.buf, mapped_data + offset, Py_MIN((size_t)view.len, self->size - offset));

	vkUnmapMemory(self->py_device->device, self->memory);

	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

static PyObject* vulkan_Resource_readback2d(vulkan_Resource * self, PyObject * args)
{
	uint32_t pitch;
	uint32_t width;
	uint32_t height;
	uint32_t bytes_per_pixel;
	if (!PyArg_ParseTuple(args, "IIII", &pitch, &width, &height, &bytes_per_pixel))
		return NULL;

	if (pitch * height > self->size)
	{
		return PyErr_Format(PyExc_ValueError, "requested buffer out of bounds: %llu (expected no more than %llu)", pitch * height, self->size);
	}

	char* mapped_data;
	VkResult result = vkMapMemory(self->py_device->device, self->memory, 0, self->size, 0, (void**)&mapped_data);
	if (result != VK_SUCCESS)
	{
		return PyErr_Format(PyExc_Exception, "Unable to Map VkDeviceMemory");
	}

	char* data2d = (char*)PyMem_Malloc(width * height * bytes_per_pixel);
	if (!data2d)
	{
		vkUnmapMemory(self->py_device->device, self->memory);
		return PyErr_Format(PyExc_MemoryError, "Unable to allocate memory for 2d data");
	}

	for (uint32_t y = 0; y < height; y++)
	{
		memcpy(data2d + (width * bytes_per_pixel * y), mapped_data + (pitch * y), width * bytes_per_pixel);
	}

	PyObject* py_bytes = PyBytes_FromStringAndSize(data2d, width * height * bytes_per_pixel);

	PyMem_Free(data2d);
	vkUnmapMemory(self->py_device->device, self->memory);
	return py_bytes;
}

static PyObject* vulkan_Resource_copy_to(vulkan_Resource * self, PyObject * args)
{
	PyObject* py_destination;
	if (!PyArg_ParseTuple(args, "O", &py_destination))
		return NULL;

	int ret = PyObject_IsInstance(py_destination, (PyObject*)&vulkan_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}

	vulkan_Resource* dst_resource = (vulkan_Resource*)py_destination;
	size_t dst_size = ((vulkan_Resource*)py_destination)->size;

	if (self->size > dst_size)
	{
		return PyErr_Format(PyExc_ValueError, "Resource size is bigger than destination size: %llu (expected no more than %llu)", self->size, dst_size);
	}

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	vkBeginCommandBuffer(self->py_device->command_buffer, &begin_info);
	if (self->buffer && dst_resource->buffer)
	{
		VkBufferCopy buffer_copy = {};
		buffer_copy.size = self->size;
		vkCmdCopyBuffer(self->py_device->command_buffer, self->buffer, dst_resource->buffer, 1, &buffer_copy);
	}
	else if (self->buffer) // buffer to image
	{
		VkImageMemoryBarrier image_memory_barrier = {};
		image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_memory_barrier.image = dst_resource->image;
		image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_memory_barrier.subresourceRange.levelCount = 1;
		image_memory_barrier.subresourceRange.layerCount = 1;
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

		vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &image_memory_barrier);
		VkBufferImageCopy buffer_image_copy = {};
		buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer_image_copy.imageSubresource.layerCount = 1;
		buffer_image_copy.imageExtent = dst_resource->image_extent;
		vkCmdCopyBufferToImage(self->py_device->command_buffer, self->buffer, dst_resource->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &buffer_image_copy);
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &image_memory_barrier);
	}
	else if (dst_resource->buffer) // image to buffer
	{
		VkImageMemoryBarrier image_memory_barrier = {};
		image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_memory_barrier.image = self->image;
		image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_memory_barrier.subresourceRange.levelCount = 1;
		image_memory_barrier.subresourceRange.layerCount = 1;
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

		vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 1, &image_memory_barrier);
		VkBufferImageCopy buffer_image_copy = {};
		buffer_image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer_image_copy.imageSubresource.layerCount = 1;
		buffer_image_copy.imageExtent = self->image_extent;
		vkCmdCopyImageToBuffer(self->py_device->command_buffer, self->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_resource->buffer, 1, &buffer_image_copy);
		image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &image_memory_barrier);
	}
	else // image to image
	{
		VkImageMemoryBarrier image_memory_barrier[2] = {};
		image_memory_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_memory_barrier[0].image = self->image;
		image_memory_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_memory_barrier[0].subresourceRange.levelCount = 1;
		image_memory_barrier[0].subresourceRange.layerCount = 1;
		image_memory_barrier[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_memory_barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		image_memory_barrier[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		image_memory_barrier[1].image = dst_resource->image;
		image_memory_barrier[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_memory_barrier[1].subresourceRange.levelCount = 1;
		image_memory_barrier[1].subresourceRange.layerCount = 1;
		image_memory_barrier[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_memory_barrier[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 2, image_memory_barrier);
		VkImageCopy image_copy = {};
		image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_copy.srcSubresource.layerCount = 1;
		image_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		image_copy.dstSubresource.layerCount = 1;
		image_copy.extent = self->image_extent;
		vkCmdCopyImage(self->py_device->command_buffer, self->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_resource->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);
		image_memory_barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		image_memory_barrier[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		image_memory_barrier[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		image_memory_barrier[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
		vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, 0, 0, 0, 2, image_memory_barrier);
	}

	vkEndCommandBuffer(self->py_device->command_buffer);

	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pCommandBuffers = &self->py_device->command_buffer;
	submit_info.commandBufferCount = 1;

	VkResult result = vkQueueSubmit(self->py_device->queue, 1, &submit_info, VK_NULL_HANDLE);

	if (result == VK_SUCCESS)
	{
		Py_BEGIN_ALLOW_THREADS;
		vkQueueWaitIdle(self->py_device->queue);
		Py_END_ALLOW_THREADS;
		Py_RETURN_NONE;
	}

	return PyErr_Format(PyExc_Exception, "unable to submit to Queue");
}

static PyMethodDef vulkan_Resource_methods[] = {
	{"upload", (PyCFunction)vulkan_Resource_upload, METH_VARARGS, "Upload bytes to a GPU Resource"},
	{"upload2d", (PyCFunction)vulkan_Resource_upload2d, METH_VARARGS, "Upload bytes to a GPU Resource given pitch, width, height and pixel size"},
	{"upload_chunked", (PyCFunction)vulkan_Resource_upload_chunked, METH_VARARGS, "Upload bytes to a GPU Resource with the given stride and a filler"},
	{"readback", (PyCFunction)vulkan_Resource_readback, METH_VARARGS, "Readback bytes from a GPU Resource"},
	{"readback2d", (PyCFunction)vulkan_Resource_readback2d, METH_VARARGS, "Readback bytes from a GPU Resource given pitch, width, height and pixel size"},
	{"readback_to_buffer", (PyCFunction)vulkan_Resource_readback_to_buffer, METH_VARARGS, "Readback into a buffer from a GPU Resource"},
	{"copy_to", (PyCFunction)vulkan_Resource_copy_to, METH_VARARGS, "Copy resource content to another resource"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* vulkan_Swapchain_present(vulkan_Swapchain * self, PyObject * args)
{
	PyObject* py_resource;
	uint32_t x;
	uint32_t y;
	if (!PyArg_ParseTuple(args, "OII", &py_resource, &x, &y))
		return NULL;

	int ret = PyObject_IsInstance(py_resource, (PyObject*)&vulkan_Resource_Type);
	if (ret < 0)
	{
		return NULL;
	}
	else if (ret == 0)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Resource object");
	}
	vulkan_Resource* src_resource = (vulkan_Resource*)py_resource;
	if (!src_resource->image)
	{
		return PyErr_Format(PyExc_ValueError, "Expected a Texture object");
	}

	uint32_t index = 0;
	VkResult result = vkAcquireNextImageKHR(self->py_device->device, self->swapchain, UINT64_MAX, self->copy_semaphore, VK_NULL_HANDLE, &index);
	if (result != VK_SUCCESS)
	{
		return PyErr_Format(PyExc_Exception, "unable to acquire next image from Swapchain");
	}

	x = Py_MIN(x, self->image_extent.width - 1);
	y = Py_MIN(y, self->image_extent.height - 1);

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	VkImageMemoryBarrier image_memory_barrier[2] = {};
	image_memory_barrier[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier[0].image = self->images[index];
	image_memory_barrier[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_memory_barrier[0].subresourceRange.levelCount = 1;
	image_memory_barrier[0].subresourceRange.layerCount = 1;
	image_memory_barrier[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	image_memory_barrier[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier[1].image = src_resource->image;
	image_memory_barrier[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_memory_barrier[1].subresourceRange.levelCount = 1;
	image_memory_barrier[1].subresourceRange.layerCount = 1;
	image_memory_barrier[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_memory_barrier[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	VkImageCopy image_copy = {};
	image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy.srcSubresource.layerCount = 1;
	image_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy.dstSubresource.layerCount = 1;
	image_copy.extent.width = Py_MIN(src_resource->image_extent.width, self->image_extent.width - x);
	image_copy.extent.height = Py_MIN(src_resource->image_extent.height, self->image_extent.height - y);
	image_copy.extent.depth = 1;
	image_copy.dstOffset.x = x;
	image_copy.dstOffset.y = y;

	vkBeginCommandBuffer(self->py_device->command_buffer, &begin_info);
	vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, 0, 0, 0, 2, image_memory_barrier);
	vkCmdCopyImage(self->py_device->command_buffer, src_resource->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, self->images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);
	image_memory_barrier[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	image_memory_barrier[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image_memory_barrier[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
	vkCmdPipelineBarrier(self->py_device->command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 2, image_memory_barrier);
	vkEndCommandBuffer(self->py_device->command_buffer);

	VkPipelineStageFlags flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pCommandBuffers = &self->py_device->command_buffer;
	submit_info.commandBufferCount = 1;
	submit_info.pWaitSemaphores = &self->copy_semaphore;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitDstStageMask = &flags;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &self->present_semaphore;

	result = vkQueueSubmit(self->py_device->queue, 1, &submit_info, VK_NULL_HANDLE);

	if (result != VK_SUCCESS)
	{
		return PyErr_Format(PyExc_Exception, "unable to copy image to Swapchain: %d", result);
	}

	VkPresentInfoKHR present_info = {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.pSwapchains = &(self->swapchain);
	present_info.swapchainCount = 1;
	present_info.pImageIndices = &index;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &(self->present_semaphore);
	result = vkQueuePresentKHR(self->py_device->queue, &present_info);

	if (result == VK_SUCCESS)
	{
		Py_BEGIN_ALLOW_THREADS;
		vkQueueWaitIdle(self->py_device->queue);
		Py_END_ALLOW_THREADS;
		Py_RETURN_NONE;
	}

	return PyErr_Format(PyExc_Exception, "unable to present Swapchain: %d", result);
}

static PyMethodDef vulkan_Swapchain_methods[] = {
	{"present", (PyCFunction)vulkan_Swapchain_present, METH_VARARGS, "Blit a texture resource to the Swapchain and present it"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* vulkan_Compute_dispatch(vulkan_Compute * self, PyObject * args)
{
	uint32_t x, y, z;
	if (!PyArg_ParseTuple(args, "III", &x, &y, &z))
		return NULL;

	VkCommandBufferBeginInfo begin_info = {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	vkBeginCommandBuffer(self->py_device->command_buffer, &begin_info);
	vkCmdBindPipeline(self->py_device->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, self->pipeline);
	vkCmdBindDescriptorSets(self->py_device->command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, self->pipeline_layout, 0, 1, &self->descriptor_set, 0, nullptr);
	vkCmdDispatch(self->py_device->command_buffer, x, y, z);
	vkEndCommandBuffer(self->py_device->command_buffer);

	VkSubmitInfo submit_info = {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pCommandBuffers = &self->py_device->command_buffer;
	submit_info.commandBufferCount = 1;

	VkResult result = vkQueueSubmit(self->py_device->queue, 1, &submit_info, VK_NULL_HANDLE);

	if (result == VK_SUCCESS)
	{
		Py_BEGIN_ALLOW_THREADS;
		vkQueueWaitIdle(self->py_device->queue);
		Py_END_ALLOW_THREADS;
		Py_RETURN_NONE;
	}

	return PyErr_Format(PyExc_Exception, "unable to submit to Queue");
}

static PyMethodDef vulkan_Compute_methods[] = {
	{"dispatch", (PyCFunction)vulkan_Compute_dispatch, METH_VARARGS, "Execute a Compute Pipeline"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static PyObject* vulkan_enable_debug(PyObject * self)
{
	vulkan_debug = true;
	Py_RETURN_NONE;
}

static PyObject* vulkan_get_shader_binary_type(PyObject * self)
{
	return PyLong_FromLong(COMPUSHADY_SHADER_BINARY_TYPE_SPIRV);
}

static PyMethodDef compushady_backends_vulkan_methods[] = {
	{"get_discovered_devices", (PyCFunction)vulkan_get_discovered_devices, METH_NOARGS, "Returns the list of discovered GPU devices"},
	{"enable_debug", (PyCFunction)vulkan_enable_debug, METH_NOARGS, "Enable GPU debug mode"},
	{"get_shader_binary_type", (PyCFunction)vulkan_get_shader_binary_type, METH_NOARGS, "Returns the required shader binary type"},
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static struct PyModuleDef compushady_backends_vulkan_module = {
	PyModuleDef_HEAD_INIT,
	"vulkan",
	NULL,
	-1,
	compushady_backends_vulkan_methods };

PyMODINIT_FUNC
PyInit_vulkan(void)
{
	PyObject* m = compushady_backend_init(
		&compushady_backends_vulkan_module,
		&vulkan_Device_Type, vulkan_Device_members, vulkan_Device_methods,
		&vulkan_Resource_Type, vulkan_Resource_members, vulkan_Resource_methods,
		&vulkan_Swapchain_Type, vulkan_Swapchain_members, vulkan_Swapchain_methods,
		&vulkan_Compute_Type, NULL, vulkan_Compute_methods
	);

	if (m == NULL)
		return NULL;

	VK_FORMAT_FLOAT(R32G32B32A32, 4 * 4);
	VK_FORMAT(R32G32B32A32_UINT, 4 * 4);
	VK_FORMAT(R32G32B32A32_SINT, 4 * 4);
	VK_FORMAT_FLOAT(R32G32B32, 3 * 4);
	VK_FORMAT(R32G32B32_UINT, 3 * 4);
	VK_FORMAT(R32G32B32_SINT, 3 * 4);
	VK_FORMAT_FLOAT(R16G16B16A16, 4 * 2);
	VK_FORMAT(R16G16B16A16_UNORM, 4 * 2);
	VK_FORMAT(R16G16B16A16_UINT, 4 * 2);
	VK_FORMAT(R16G16B16A16_SNORM, 4 * 2);
	VK_FORMAT(R16G16B16A16_SINT, 4 * 2);
	VK_FORMAT_FLOAT(R32G32, 2 * 4);
	VK_FORMAT(R32G32_UINT, 2 * 4);
	VK_FORMAT(R32G32_SINT, 2 * 4);
	VK_FORMAT(R8G8B8A8_UNORM, 4);
	VK_FORMAT_SRGB(R8G8B8A8, 4);
	VK_FORMAT(R8G8B8A8_UINT, 4);
	VK_FORMAT(R8G8B8A8_SNORM, 4);
	VK_FORMAT(R8G8B8A8_SINT, 4);
	VK_FORMAT_FLOAT(R16G16, 2 * 2);
	VK_FORMAT(R16G16_UNORM, 2 * 2);
	VK_FORMAT(R16G16_UINT, 2 * 2);
	VK_FORMAT(R16G16_SNORM, 2 * 2);
	VK_FORMAT(R16G16_SINT, 2 * 2);
	VK_FORMAT_FLOAT(R32, 4);
	VK_FORMAT(R32_UINT, 4);
	VK_FORMAT(R32_SINT, 4);
	VK_FORMAT(R8G8_UNORM, 2);
	VK_FORMAT(R8G8_UINT, 2);
	VK_FORMAT(R8G8_SNORM, 2);
	VK_FORMAT(R8G8_SINT, 2);
	VK_FORMAT_FLOAT(R16, 2);
	VK_FORMAT(R16_UNORM, 2);
	VK_FORMAT(R16_UINT, 2);
	VK_FORMAT(R16_SNORM, 2);
	VK_FORMAT(R16_SINT, 2);
	VK_FORMAT(R8_UNORM, 1);
	VK_FORMAT(R8_UINT, 1);
	VK_FORMAT(R8_SNORM, 1);
	VK_FORMAT(R8_SINT, 1);
	VK_FORMAT(B8G8R8A8_UNORM, 4);
	VK_FORMAT_SRGB(B8G8R8A8, 4);

	return m;
}
