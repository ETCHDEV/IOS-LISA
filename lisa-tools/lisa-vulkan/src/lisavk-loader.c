/* Lisa: the Vulkan "loader" metal2vulkan's runtime opens (ash::Entry::load_from): the one export it
 * looks up, vkGetInstanceProcAddr, which is Turnip's own (it exports the ICD form). */
#include <vulkan/vulkan.h>
extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);
__attribute__((visibility("default"))) PFN_vkVoidFunction
vkGetInstanceProcAddr(VkInstance instance, const char *name)
{
	return vk_icdGetInstanceProcAddr(instance, name);
}
