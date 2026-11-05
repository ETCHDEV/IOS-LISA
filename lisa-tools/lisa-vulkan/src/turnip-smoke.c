/*
 * Lisa (2026-10-31): the first Vulkan on the phone. Turnip (Mesa's Vulkan driver for Adreno),
 * linked directly (no loader), on the LisaGPU kernel driver through redwood-drm: an instance, the
 * device, a 64x64 image cleared by the GPU and copied into a buffer, a buffer filled by the GPU,
 * one submission with a fence; the pixels and the words checked. Every line to the kernel log.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <vulkan/vulkan.h>

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void
say(const char *fmt, ...)
{
	char line[300];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	(void)sysctlbyname("kern.lisa_say", NULL, NULL, line, strlen(line));
	printf("%s\n", line);
	fflush(stdout);
}

extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
static PFN_vkGetInstanceProcAddr gipa;
#define I(name) PFN_##name name = (PFN_##name)gipa(inst, #name)
#define D(name) PFN_##name name = (PFN_##name)vkGetDeviceProcAddr(dev, #name)
static void
forward_log(void)
{
	FILE *f = fopen("/private/var/tmp/lisa-turnip.log", "r");
	char line[280];
	if (!f) {
		say("TURNIP TEST: (no Turnip log)");
		return;
	}
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = 0;
		if (line[0])
			say("TURNIP LOG: %s", line);
	}
	fclose(f);
}
#define CHECK(what, r) do { VkResult _r = (r); if (_r != VK_SUCCESS) { say("TURNIP TEST: FAIL %s: VkResult %d", what, _r); forward_log(); pause(); return 1; } \
	say("TURNIP TEST: ok %s", what); } while (0)

static uint32_t
memtype(VkPhysicalDeviceMemoryProperties *mp, uint32_t bits, VkMemoryPropertyFlags want)
{
	for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
		if ((bits & (1u << i)) && (mp->memoryTypes[i].propertyFlags & want) == want)
			return i;
	return ~0u;
}

int
main(void)
{
	sleep(20);   /* after the window server has its GPU client */
	/* Turnip's own words (vk_errorf, mesa_log) to a file, forwarded below on failure */
	unlink("/private/var/tmp/lisa-turnip.log");
	setenv("MESA_LOG_FILE", "/private/var/tmp/lisa-turnip.log", 1);
	setenv("TU_DEBUG", "startup", 1);
	setenv("MESA_DEBUG", "1", 1);
	say("TURNIP TEST: start");
	gipa = (PFN_vkGetInstanceProcAddr)vk_icdGetInstanceProcAddr;
	VkInstance inst = VK_NULL_HANDLE;
	PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1,
				  .pApplicationName = "lisa-turnip-smoke" };
	VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
	CHECK("vkCreateInstance", vkCreateInstance(&ici, NULL, &inst));
	I(vkEnumeratePhysicalDevices); I(vkGetPhysicalDeviceProperties); I(vkGetPhysicalDeviceMemoryProperties);
	I(vkCreateDevice); I(vkGetDeviceProcAddr); I(vkGetPhysicalDeviceQueueFamilyProperties);
	uint32_t n = 1;
	VkPhysicalDevice pd;
	CHECK("vkEnumeratePhysicalDevices", vkEnumeratePhysicalDevices(inst, &n, &pd));
	if (n == 0) { say("TURNIP TEST: FAIL no physical device"); return 1; }
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(pd, &props);
	say("TURNIP TEST: device '%s', API %u.%u.%u, driver %#x, vendor %#x device %#x", props.deviceName,
	    VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion),
	    props.driverVersion, props.vendorID, props.deviceID);
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		say("TURNIP TEST: memory type %u flags %#x heap %u", i, mp.memoryTypes[i].propertyFlags, mp.memoryTypes[i].heapIndex);
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0,
					.queueCount = 1, .pQueuePriorities = &prio };
	VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
	VkDevice dev;
	CHECK("vkCreateDevice", vkCreateDevice(pd, &dci, NULL, &dev));
	D(vkGetDeviceQueue); D(vkCreateImage); D(vkGetImageMemoryRequirements); D(vkAllocateMemory); D(vkBindImageMemory);
	D(vkCreateBuffer); D(vkGetBufferMemoryRequirements); D(vkBindBufferMemory); D(vkMapMemory);
	D(vkCreateCommandPool); D(vkAllocateCommandBuffers); D(vkBeginCommandBuffer); D(vkEndCommandBuffer);
	D(vkCmdPipelineBarrier); D(vkCmdClearColorImage); D(vkCmdCopyImageToBuffer); D(vkCmdFillBuffer);
	D(vkCreateFence); D(vkQueueSubmit); D(vkWaitForFences); D(vkInvalidateMappedMemoryRanges);
	VkQueue q;
	vkGetDeviceQueue(dev, 0, 0, &q);

	const uint32_t W = 64, H = 64;
	VkImageCreateInfo imci = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
		.format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
	VkImage img;
	CHECK("vkCreateImage", vkCreateImage(dev, &imci, NULL, &img));
	VkMemoryRequirements mr;
	vkGetImageMemoryRequirements(dev, img, &mr);
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
		.memoryTypeIndex = memtype(&mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
	VkDeviceMemory imem;
	CHECK("vkAllocateMemory (image)", vkAllocateMemory(dev, &mai, NULL, &imem));
	CHECK("vkBindImageMemory", vkBindImageMemory(dev, img, imem, 0));

	VkBuffer buf[2];
	VkDeviceMemory bmem[2];
	void *map[2];
	for (int k = 0; k < 2; k++) {
		VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 4,
			.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
		CHECK("vkCreateBuffer", vkCreateBuffer(dev, &bci, NULL, &buf[k]));
		vkGetBufferMemoryRequirements(dev, buf[k], &mr);
		mai.allocationSize = mr.size;
		mai.memoryTypeIndex = memtype(&mp, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		CHECK("vkAllocateMemory (buffer)", vkAllocateMemory(dev, &mai, NULL, &bmem[k]));
		CHECK("vkBindBufferMemory", vkBindBufferMemory(dev, buf[k], bmem[k], 0));
		CHECK("vkMapMemory", vkMapMemory(dev, bmem[k], 0, VK_WHOLE_SIZE, 0, &map[k]));
		memset(map[k], 0x11, W * H * 4);
	}

	VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
	VkCommandPool pool;
	CHECK("vkCreateCommandPool", vkCreateCommandPool(dev, &pci, NULL, &pool));
	VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
	VkCommandBuffer cb;
	CHECK("vkAllocateCommandBuffers", vkAllocateCommandBuffers(dev, &cai, &cb));
	VkCommandBufferBeginInfo cbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	CHECK("vkBeginCommandBuffer", vkBeginCommandBuffer(cb, &cbi));
	VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageMemoryBarrier b1 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img, .subresourceRange = range };
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b1);
	VkClearColorValue color = { .float32 = { 0.25f, 0.5f, 0.75f, 1.0f } };
	vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);
	VkImageMemoryBarrier b2 = b1;
	b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	b2.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	b2.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b2);
	VkBufferImageCopy copy = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, .imageExtent = { W, H, 1 } };
	vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf[0], 1, &copy);
	vkCmdFillBuffer(cb, buf[1], 0, VK_WHOLE_SIZE, 0xdeadbeef);
	VkMemoryBarrier hb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT };
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
	CHECK("vkEndCommandBuffer", vkEndCommandBuffer(cb));
	VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence;
	CHECK("vkCreateFence", vkCreateFence(dev, &fci, NULL, &fence));
	VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
	CHECK("vkQueueSubmit", vkQueueSubmit(q, 1, &si, fence));
	CHECK("vkWaitForFences", vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull));

	const uint8_t *px = map[0];
	const uint32_t *w = map[1];
	unsigned badpx = 0, badw = 0;
	for (uint32_t i = 0; i < W * H; i++) {
		if (px[4 * i] != 0x40 || px[4 * i + 1] != 0x80 || px[4 * i + 2] != 0xbf || px[4 * i + 3] != 0xff)
			badpx++;
		if (w[i] != 0xdeadbeef)
			badw++;
	}
	say("TURNIP TEST: pixel 0 %02x%02x%02x%02x, last %02x%02x%02x%02x; %u of %u pixels wrong; fill word %08x, %u of %u wrong",
	    px[0], px[1], px[2], px[3], px[4 * (W * H - 1)], px[4 * (W * H - 1) + 1], px[4 * (W * H - 1) + 2], px[4 * (W * H - 1) + 3],
	    badpx, W * H, w[0], badw, W * H);
	say(badpx == 0 && badw == 0 ? "TURNIP TEST: PASS -- Vulkan on the Adreno through Turnip" : "TURNIP TEST: FAIL -- wrong results");
	for (;;)
		pause();   /* (launchd's RunAtLoad job: stay, do not respawn) */
}
