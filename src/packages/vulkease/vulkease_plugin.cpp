#include <mobius/mobius_plugin.h>

#include <vulkease.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

static const char* VULKEASE_CONTEXT_TYPE = "vulkease_context";
static const char* VULKEASE_DEVICE_TYPE = "vulkease_device";

struct DeviceObject;

struct ContextObject {
    VEContext* handle = nullptr;
    bool closed = false;
    std::mutex mutex;
    std::unordered_set<DeviceObject*> devices;
};

struct DeviceObject {
    ContextObject* owner = nullptr;
    VEDevice* handle = nullptr;
    bool closed = false;
};

struct VulkEaseApi {
    uint32_t (*getVersion)(void) = veGetVersion;
    const char* (*resultToString)(VEResult result) = veResultToString;
    VEResult (*createContext)(const char* application_name, const char* const* extensions,
                              uint32_t extension_count, VEContext** out_context) = veCreateContext;
    VEResult (*destroyContext)(VEContext* context) = veDestroyContext;
    VEResult (*enumeratePhysicalDevices)(VEContext* context, uint32_t* count) = veEnumeratePhysicalDevices;
    VEResult (*getPhysicalDeviceInfo)(VEContext* context, uint32_t index,
                                      VkPhysicalDevice* physical_device, char* device_name,
                                      VkPhysicalDeviceType* device_type) = veGetPhysicalDeviceInfo;
    VEResult (*createDevice)(VEContext* context, VkPhysicalDevice preferred_device,
                             const char* const* extensions, uint32_t extension_count,
                             VEDevice** out_device) = veCreateDevice;
    VEResult (*destroyDevice)(VEDevice* device) = veDestroyDevice;
    bool (*isInstanceExtensionAvailable)(const char* extension_name) = veIsInstanceExtensionAvailable;
    VEResult (*deviceWaitIdle)(VEDevice* device) = veDeviceWaitIdle;
    uint32_t (*getVulkanVersion)(VEDevice* device) = veGetVulkanVersion;
    bool (*isMeshShaderSupported)(VEDevice* device) = veIsMeshShaderSupported;
};

static const VulkEaseApi g_api{};

static bool ensure_api_loaded(MobiusState* state) {
    (void)state;
    return true;
}

static void unload_api() {
}

static void push_string_field(MobiusState* state, int table_idx, const char* key, const char* value) {
    if (value) mobius_stack_pushString(state, value);
    else mobius_stack_pushNil(state);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_int_field(MobiusState* state, int table_idx, const char* key, int64_t value) {
    mobius_stack_pushInt64(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_bool_field(MobiusState* state, int table_idx, const char* key, bool value) {
    mobius_stack_pushBool(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static const char* physical_device_type_name(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual_gpu";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
        case VK_PHYSICAL_DEVICE_TYPE_OTHER:
        default: return "other";
    }
}

static void push_version_table(MobiusState* state, uint32_t version) {
    mobius_stack_pushNewTable(state, 4);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "raw", version);
    push_int_field(state, tbl, "major", (version >> 22) & 0x3ff);
    push_int_field(state, tbl, "minor", (version >> 12) & 0x3ff);
    push_int_field(state, tbl, "patch", version & 0xfff);
}

static int return_self(MobiusState* state, int arg_count) {
    mobius_stack_copy(state, 0);
    mobius_stack_pop(state, arg_count);
    return 1;
}

static ContextObject* get_context_object(MobiusState* state, int idx, const char* context) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, VULKEASE_CONTEXT_TYPE) != 0) {
        mobius_error(state, context);
        return nullptr;
    }
    return static_cast<ContextObject*>(ptr);
}

static DeviceObject* get_device_object(MobiusState* state, int idx, const char* context) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, VULKEASE_DEVICE_TYPE) != 0) {
        mobius_error(state, context);
        return nullptr;
    }
    return static_cast<DeviceObject*>(ptr);
}

static int ensure_context_open(MobiusState* state, ContextObject* ctx, const char* context) {
    if (!ctx || ctx->closed || !ctx->handle) {
        return mobius_error(state, context);
    }
    return 0;
}

static int ensure_device_open(MobiusState* state, DeviceObject* device, const char* context) {
    if (!device || device->closed || !device->handle) {
        return mobius_error(state, context);
    }
    return 0;
}

static int result_error(MobiusState* state, VEResult result, const char* prefix) {
    std::string message = prefix ? std::string(prefix) : "vulkease call failed";
    message += ": ";
    message += g_api.resultToString ? g_api.resultToString(result) : "unknown error";
    return mobius_error(state, message.c_str());
}

static int close_device_locked(DeviceObject* device_obj) {
    if (!device_obj || device_obj->closed) return 0;
    if (device_obj->owner) {
        device_obj->owner->devices.erase(device_obj);
    }
    VEDevice* handle = device_obj->handle;
    device_obj->handle = nullptr;
    device_obj->closed = true;
    device_obj->owner = nullptr;
    if (!handle) return 0;
    return g_api.destroyDevice(handle);
}

static int close_context_locked(ContextObject* ctx_obj) {
    if (!ctx_obj || ctx_obj->closed) return 0;

    std::vector<DeviceObject*> devices(ctx_obj->devices.begin(), ctx_obj->devices.end());
    for (DeviceObject* device_obj : devices) {
        if (device_obj) close_device_locked(device_obj);
    }
    ctx_obj->devices.clear();

    VEContext* handle = ctx_obj->handle;
    ctx_obj->handle = nullptr;
    ctx_obj->closed = true;
    if (!handle) return 0;
    return g_api.destroyContext(handle);
}

static void context_object_destructor(void* ptr) {
    ContextObject* ctx_obj = static_cast<ContextObject*>(ptr);
    if (!ctx_obj) return;
    std::lock_guard<std::mutex> lock(ctx_obj->mutex);
    close_context_locked(ctx_obj);
    delete ctx_obj;
}

static void device_object_destructor(void* ptr) {
    DeviceObject* device_obj = static_cast<DeviceObject*>(ptr);
    if (!device_obj) return;
    if (device_obj->owner) {
        std::lock_guard<std::mutex> lock(device_obj->owner->mutex);
        close_device_locked(device_obj);
    } else if (!device_obj->closed && device_obj->handle) {
        g_api.destroyDevice(device_obj->handle);
        device_obj->handle = nullptr;
        device_obj->closed = true;
    }
    delete device_obj;
}

static bool collect_string_array(MobiusState* state, int idx,
                                 std::vector<std::string>& storage,
                                 std::vector<const char*>& out,
                                 const char* context) {
    if (mobius_stack_isNil(state, idx)) return true;
    if (!mobius_stack_isArray(state, idx)) {
        mobius_error(state, context);
        return false;
    }

    size_t len = mobius_stack_getArrayLength(state, idx);
    storage.reserve(len);
    out.reserve(len);
    for (size_t i = 0; i < len; i++) {
        mobius_stack_getArrayElement(state, idx, i);
        if (!mobius_stack_isString(state, -1)) {
            mobius_stack_pop(state, 1);
            mobius_error(state, context);
            return false;
        }
        const char* value = mobius_stack_asString(state, -1);
        storage.emplace_back(value ? value : "");
        mobius_stack_pop(state, 1);
    }
    for (const std::string& value : storage) {
        out.push_back(value.c_str());
    }
    return true;
}

static int vulkease_version(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "version() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    push_version_table(state, g_api.getVersion());
    return 1;
}

static int vulkease_result_string(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "result_string() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    VEResult result = (VEResult)mobius_stack_asInt64(state, 0);
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, g_api.resultToString(result));
    return 1;
}

static int vulkease_instance_extension_available(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "instance_extension_available() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    if (!mobius_stack_isString(state, 0)) return mobius_error(state, "instance_extension_available() extension name must be a string");
    const char* extension_name = mobius_stack_asString(state, 0);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.isInstanceExtensionAvailable(extension_name));
    return 1;
}

static int vulkease_create_context(MobiusState* state, int arg_count) {
    if (arg_count > 2) return mobius_error(state, "create_context() expects at most 2 arguments");
    if (!ensure_api_loaded(state)) return -1;

    const char* application_name = nullptr;
    if (arg_count >= 1 && !mobius_stack_isNil(state, 0)) {
        if (!mobius_stack_isString(state, 0)) return mobius_error(state, "create_context() application_name must be a string or nil");
        application_name = mobius_stack_asString(state, 0);
    }

    std::vector<std::string> extension_storage;
    std::vector<const char*> extensions;
    if (arg_count >= 2 && !collect_string_array(state, 1, extension_storage, extensions,
                                                "create_context() extensions must be an array of strings or nil")) {
        return -1;
    }

    VEContext* handle = nullptr;
    VEResult rc = g_api.createContext(application_name,
                                      extensions.empty() ? nullptr : extensions.data(),
                                      (uint32_t)extensions.size(), &handle);
    mobius_stack_pop(state, arg_count);
    if (rc != VE_SUCCESS) return result_error(state, rc, "veCreateContext");

    ContextObject* ctx_obj = new (std::nothrow) ContextObject();
    if (!ctx_obj) {
        g_api.destroyContext(handle);
        return mobius_error(state, "failed to allocate vulkease context userdata");
    }
    ctx_obj->handle = handle;
    mobius_stack_pushUserdata(state, ctx_obj, context_object_destructor,
                              VULKEASE_CONTEXT_TYPE, sizeof(ContextObject));
    return 1;
}

static int context_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:close() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:close() self is not a vulkease context");
    if (!ctx_obj) return -1;
    std::lock_guard<std::mutex> lock(ctx_obj->mutex);
    int rc = close_context_locked(ctx_obj);
    mobius_stack_pop(state, 1);
    if (rc != VE_SUCCESS && rc != 0) return result_error(state, (VEResult)rc, "veDestroyContext");
    mobius_stack_pushBool(state, true);
    return 1;
}

static int context_is_closed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_closed() expects no arguments");
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_closed() self is not a vulkease context");
    if (!ctx_obj) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, ctx_obj->closed || !ctx_obj->handle);
    return 1;
}

static int context_physical_devices(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:physical_devices() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:physical_devices() self is not a vulkease context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "vulkease context has been destroyed") < 0) return -1;

    uint32_t count = 0;
    VEResult rc = g_api.enumeratePhysicalDevices(ctx_obj->handle, &count);
    if (rc != VE_SUCCESS) return result_error(state, rc, "veEnumeratePhysicalDevices");

    mobius_stack_pop(state, 1);
    mobius_stack_pushNewArray(state, count);
    int arr = mobius_stack_size(state) - 1;
    for (uint32_t i = 0; i < count; i++) {
        char name[256] = {};
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkPhysicalDeviceType device_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
        rc = g_api.getPhysicalDeviceInfo(ctx_obj->handle, i, &physical_device, name, &device_type);
        if (rc != VE_SUCCESS) return result_error(state, rc, "veGetPhysicalDeviceInfo");

        mobius_stack_pushNewTable(state, 3);
        int tbl = mobius_stack_size(state) - 1;
        push_int_field(state, tbl, "index", (int64_t)i);
        push_string_field(state, tbl, "name", name);
        push_string_field(state, tbl, "type", physical_device_type_name(device_type));
        mobius_stack_arrayPush(state, arr);
    }
    return 1;
}

static int context_create_device(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 3) {
        return mobius_error(state, "context:create_device() expects up to 2 arguments");
    }
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:create_device() self is not a vulkease context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "vulkease context has been destroyed") < 0) return -1;

    VkPhysicalDevice preferred_device = VK_NULL_HANDLE;
    if (arg_count >= 2 && !mobius_stack_isNil(state, 1)) {
        if (!mobius_stack_isInteger(state, 1)) return mobius_error(state, "context:create_device() preferred index must be an integer or nil");
        int64_t preferred_index = mobius_stack_asInt64(state, 1);
        if (preferred_index < 0) return mobius_error(state, "context:create_device() preferred index must be >= 0");
        uint32_t count = 0;
        VEResult count_rc = g_api.enumeratePhysicalDevices(ctx_obj->handle, &count);
        if (count_rc != VE_SUCCESS) return result_error(state, count_rc, "veEnumeratePhysicalDevices");
        if ((uint32_t)preferred_index >= count) return mobius_error(state, "context:create_device() preferred index is out of range");
        char ignored_name[256] = {};
        VkPhysicalDeviceType ignored_type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
        VEResult info_rc = g_api.getPhysicalDeviceInfo(ctx_obj->handle, (uint32_t)preferred_index,
                                                       &preferred_device, ignored_name, &ignored_type);
        if (info_rc != VE_SUCCESS) return result_error(state, info_rc, "veGetPhysicalDeviceInfo");
    }

    std::vector<std::string> extension_storage;
    std::vector<const char*> extensions;
    if (arg_count >= 3 && !collect_string_array(state, 2, extension_storage, extensions,
                                                "context:create_device() extensions must be an array of strings or nil")) {
        return -1;
    }

    VEDevice* handle = nullptr;
    VEResult rc = g_api.createDevice(ctx_obj->handle, preferred_device,
                                     extensions.empty() ? nullptr : extensions.data(),
                                     (uint32_t)extensions.size(), &handle);
    if (rc != VE_SUCCESS) return result_error(state, rc, "veCreateDevice");

    DeviceObject* device_obj = new (std::nothrow) DeviceObject();
    if (!device_obj) {
        g_api.destroyDevice(handle);
        return mobius_error(state, "failed to allocate vulkease device userdata");
    }

    device_obj->owner = ctx_obj;
    device_obj->handle = handle;
    {
        std::lock_guard<std::mutex> lock(ctx_obj->mutex);
        ctx_obj->devices.insert(device_obj);
    }
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushUserdata(state, device_obj, device_object_destructor,
                              VULKEASE_DEVICE_TYPE, sizeof(DeviceObject));
    return 1;
}

static int device_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "device:close() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    DeviceObject* device_obj = get_device_object(state, 0, "device:close() self is not a vulkease device");
    if (!device_obj) return -1;

    int rc = 0;
    if (device_obj->owner) {
        std::lock_guard<std::mutex> lock(device_obj->owner->mutex);
        rc = close_device_locked(device_obj);
    } else {
        rc = close_device_locked(device_obj);
    }
    mobius_stack_pop(state, 1);
    if (rc != VE_SUCCESS && rc != 0) return result_error(state, (VEResult)rc, "veDestroyDevice");
    mobius_stack_pushBool(state, true);
    return 1;
}

static int device_is_closed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "device:is_closed() expects no arguments");
    DeviceObject* device_obj = get_device_object(state, 0, "device:is_closed() self is not a vulkease device");
    if (!device_obj) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, device_obj->closed || !device_obj->handle);
    return 1;
}

static int device_wait_idle(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "device:wait_idle() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    DeviceObject* device_obj = get_device_object(state, 0, "device:wait_idle() self is not a vulkease device");
    if (!device_obj) return -1;
    if (ensure_device_open(state, device_obj, "vulkease device has been destroyed") < 0) return -1;
    VEResult rc = g_api.deviceWaitIdle(device_obj->handle);
    if (rc != VE_SUCCESS) return result_error(state, rc, "veDeviceWaitIdle");
    return return_self(state, arg_count);
}

static int device_vulkan_version(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "device:vulkan_version() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    DeviceObject* device_obj = get_device_object(state, 0, "device:vulkan_version() self is not a vulkease device");
    if (!device_obj) return -1;
    if (ensure_device_open(state, device_obj, "vulkease device has been destroyed") < 0) return -1;
    uint32_t version = g_api.getVulkanVersion(device_obj->handle);
    mobius_stack_pop(state, 1);
    push_version_table(state, version);
    return 1;
}

static int device_mesh_shader_supported(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "device:mesh_shader_supported() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    DeviceObject* device_obj = get_device_object(state, 0, "device:mesh_shader_supported() self is not a vulkease device");
    if (!device_obj) return -1;
    if (ensure_device_open(state, device_obj, "vulkease device has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.isMeshShaderSupported(device_obj->handle));
    return 1;
}

static void copy_module_function(MobiusState* state, int module_idx, const char* from_key, int target_idx, const char* to_key) {
    mobius_stack_getTableField(state, module_idx, from_key);
    mobius_stack_setTableField(state, target_idx, to_key);
}

struct NamedIntConstant {
    const char* name;
    int64_t value;
};

static const NamedIntConstant VULKEASE_INT_CONSTANTS[] = {
    {"SUCCESS", VE_SUCCESS},
    {"ERROR_OUT_OF_MEMORY", VE_ERROR_OUT_OF_MEMORY},
    {"ERROR_DEVICE_LOST", VE_ERROR_DEVICE_LOST},
    {"ERROR_INVALID_PARAMETER", VE_ERROR_INVALID_PARAMETER},
    {"ERROR_FEATURE_NOT_SUPPORTED", VE_ERROR_FEATURE_NOT_SUPPORTED},
    {"ERROR_UNSUPPORTED", VE_ERROR_UNSUPPORTED},
    {"ERROR_SHADER_COMPILATION_FAILED", VE_ERROR_SHADER_COMPILATION_FAILED},
    {"ERROR_SWAPCHAIN_OUT_OF_DATE", VE_ERROR_SWAPCHAIN_OUT_OF_DATE},
    {"ERROR_TRANSFER_FAILED", VE_ERROR_TRANSFER_FAILED},
    {"ERROR_NOT_FOUND", VE_ERROR_NOT_FOUND},
    {"ERROR_NOT_INITIALIZED", VE_ERROR_NOT_INITIALIZED},
    {"ERROR_TIMEOUT", VE_ERROR_TIMEOUT},
    {"ERROR_UNKNOWN", VE_ERROR_UNKNOWN},
};

static int vulkease_post_init(MobiusState* state) {
    const int module_idx = 0;

    mobius_stack_pushNewTable(state, 5);
    int context_proto = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__context_close", context_proto, "close");
    copy_module_function(state, module_idx, "__context_close", context_proto, "destroy");
    copy_module_function(state, module_idx, "__context_is_closed", context_proto, "is_closed");
    copy_module_function(state, module_idx, "__context_physical_devices", context_proto, "physical_devices");
    copy_module_function(state, module_idx, "__context_create_device", context_proto, "create_device");
    mobius_set_userdata_type_metatable(state, VULKEASE_CONTEXT_TYPE);

    mobius_stack_pushNewTable(state, 5);
    int device_proto = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__device_close", device_proto, "close");
    copy_module_function(state, module_idx, "__device_close", device_proto, "destroy");
    copy_module_function(state, module_idx, "__device_is_closed", device_proto, "is_closed");
    copy_module_function(state, module_idx, "__device_wait_idle", device_proto, "wait_idle");
    copy_module_function(state, module_idx, "__device_vulkan_version", device_proto, "vulkan_version");
    copy_module_function(state, module_idx, "__device_mesh_shader_supported", device_proto, "mesh_shader_supported");
    mobius_set_userdata_type_metatable(state, VULKEASE_DEVICE_TYPE);

    const char* hidden_keys[] = {
        "__context_close", "__context_is_closed", "__context_physical_devices",
        "__context_create_device", "__device_close", "__device_is_closed",
        "__device_wait_idle", "__device_vulkan_version",
        "__device_mesh_shader_supported"
    };
    for (const char* key : hidden_keys) {
        mobius_stack_pushNil(state);
        mobius_stack_setTableField(state, module_idx, key);
    }

    for (const NamedIntConstant& constant : VULKEASE_INT_CONSTANTS) {
        mobius_stack_pushInt64(state, constant.value);
        mobius_stack_setTableField(state, module_idx, constant.name);
    }
    return 0;
}

static void cleanup_vulkease_plugin(void) {
    unload_api();
}

static MobiusPluginFunction vulkease_functions[] = {
    {"version", vulkease_version, 0, MOBIUS_VAL_TABLE, "Return the packaged VulkEase version"},
    {"result_string", vulkease_result_string, 1, MOBIUS_VAL_STRING, "Return a string for a VulkEase result code"},
    {"instance_extension_available", vulkease_instance_extension_available, 1, MOBIUS_VAL_BOOL, "Return whether a Vulkan instance extension is available"},
    {"create_context", vulkease_create_context, SIZE_MAX, MOBIUS_VAL_USERDATA, "Create a VulkEase context"},
    {"__context_close", context_close, 1, MOBIUS_VAL_BOOL, "Internal context close method"},
    {"__context_is_closed", context_is_closed, 1, MOBIUS_VAL_BOOL, "Internal context closed-state method"},
    {"__context_physical_devices", context_physical_devices, 1, MOBIUS_VAL_ARRAY, "Internal physical_devices method"},
    {"__context_create_device", context_create_device, SIZE_MAX, MOBIUS_VAL_USERDATA, "Internal create_device method"},
    {"__device_close", device_close, 1, MOBIUS_VAL_BOOL, "Internal device close method"},
    {"__device_is_closed", device_is_closed, 1, MOBIUS_VAL_BOOL, "Internal device closed-state method"},
    {"__device_wait_idle", device_wait_idle, 1, MOBIUS_VAL_USERDATA, "Internal device wait_idle method"},
    {"__device_vulkan_version", device_vulkan_version, 1, MOBIUS_VAL_TABLE, "Internal device vulkan_version method"},
    {"__device_mesh_shader_supported", device_mesh_shader_supported, 1, MOBIUS_VAL_BOOL, "Internal device mesh_shader_supported method"},
};

static MobiusPlugin vulkease_plugin = {
    .metadata = {
        .name = "vulkease",
        .version = "0.1.0",
        .description = "VulkEase graphics bootstrap bindings for Mobius",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = vulkease_functions,
    .function_count = sizeof(vulkease_functions) / sizeof(vulkease_functions[0]),
    .init_plugin = nullptr,
    .cleanup_plugin = cleanup_vulkease_plugin,
    .post_init = vulkease_post_init,
};

} // namespace

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &vulkease_plugin;
}
