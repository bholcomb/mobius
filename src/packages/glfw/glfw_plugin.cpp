#include <mobius/mobius_plugin.h>

#include "data/enum.h"
#include "data/value.h"
#include "state/mobius_state.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

static const char* GLFW_WINDOW_TYPE = "glfw_window";
static const char* GLFW_MONITOR_TYPE = "glfw_monitor";

struct WindowObject {
    MobiusState* state = nullptr;
    GLFWwindow* handle = nullptr;
    UserdataObject* userdata_handle = nullptr;
    bool destroyed = false;

    MobiusValueRef on_key_ref = 0;
    MobiusValueRef on_char_ref = 0;
    MobiusValueRef on_cursor_pos_ref = 0;
    MobiusValueRef on_mouse_button_ref = 0;
    MobiusValueRef on_scroll_ref = 0;
    MobiusValueRef on_resize_ref = 0;
    MobiusValueRef on_framebuffer_resize_ref = 0;
    MobiusValueRef on_close_ref = 0;
    MobiusValueRef on_focus_ref = 0;
    MobiusValueRef on_cursor_enter_ref = 0;
    MobiusValueRef on_iconify_ref = 0;
    MobiusValueRef on_maximize_ref = 0;
};

struct MonitorObject {
    MobiusState* state = nullptr;
    GLFWmonitor* handle = nullptr;
};

struct PendingCallbackEvent {
    MobiusState* state = nullptr;
    MobiusValueRef function_ref = 0;
    std::array<MobiusValueRef, 5> args = {};
    size_t arg_count = 0;
};

static std::mutex g_glfw_mutex;
static std::unordered_set<MobiusState*> g_initialized_states;
static std::unordered_set<WindowObject*> g_live_windows;
static std::vector<PendingCallbackEvent> g_pending_events;
using InstalledEnumMap = std::unordered_map<std::string, EnumDefinition*>;
static std::unordered_map<MobiusState*, InstalledEnumMap> g_installed_enums;

static bool state_is_initialized(MobiusState* state) {
    std::lock_guard<std::mutex> lock(g_glfw_mutex);
    return g_initialized_states.find(state) != g_initialized_states.end();
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

static void push_value_field(MobiusState* state, int table_idx, const char* key, const Value& value) {
    state->npush(value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_float_field(MobiusState* state, int table_idx, const char* key, double value) {
    mobius_stack_pushFloat64(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void release_installed_enum_map(InstalledEnumMap& enums) {
    for (auto& entry : enums) {
        if (entry.second) entry.second->release();
    }
    enums.clear();
}

static EnumDefinition* retain_installed_enum(MobiusState* state, const char* enum_name) {
    if (!state || !enum_name || enum_name[0] == '\0') return nullptr;

    std::lock_guard<std::mutex> lock(g_glfw_mutex);
    auto state_it = g_installed_enums.find(state);
    if (state_it == g_installed_enums.end()) return nullptr;

    auto enum_it = state_it->second.find(enum_name);
    if (enum_it == state_it->second.end() || !enum_it->second) return nullptr;
    static_cast<RefCounted*>(enum_it->second)->retain();
    return enum_it->second;
}

static Value make_retained_enum_value(EnumDefinition* definition, int64_t value) {
    if (!definition) return make_int64_value(value);

    Value enum_value;
    enum_value.type = VAL_ENUM;
    enum_value.flags = 0;
    enum_value.aux = (int32_t)value;
    enum_value.as.enum_def = definition;
    static_cast<RefCounted*>(definition)->retain();
    return enum_value;
}

static Value make_installed_enum_value(MobiusState* state, const char* enum_name, int64_t value) {
    EnumDefinition* definition = retain_installed_enum(state, enum_name);
    if (!definition) return make_int64_value(value);

    Value enum_value = make_retained_enum_value(definition, value);
    definition->release();
    return enum_value;
}

static void push_error_table(MobiusState* state, int code, const char* description) {
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_value_field(state, tbl, "code", make_installed_enum_value(state, "Error", code));
    push_string_field(state, tbl, "description", description);
}

static void push_size_table(MobiusState* state, int width, int height) {
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "width", width);
    push_int_field(state, tbl, "height", height);
}

static void push_pos_table(MobiusState* state, int x, int y) {
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "x", x);
    push_int_field(state, tbl, "y", y);
}

static void push_rect_table(MobiusState* state, int x, int y, int width, int height) {
    mobius_stack_pushNewTable(state, 4);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "x", x);
    push_int_field(state, tbl, "y", y);
    push_int_field(state, tbl, "width", width);
    push_int_field(state, tbl, "height", height);
}

static void push_cursor_pos_table(MobiusState* state, double x, double y) {
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_float_field(state, tbl, "x", x);
    push_float_field(state, tbl, "y", y);
}

static void push_content_scale_table(MobiusState* state, float xscale, float yscale) {
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_float_field(state, tbl, "x", xscale);
    push_float_field(state, tbl, "y", yscale);
}

static void push_frame_size_table(MobiusState* state, int left, int top, int right, int bottom) {
    mobius_stack_pushNewTable(state, 4);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "left", left);
    push_int_field(state, tbl, "top", top);
    push_int_field(state, tbl, "right", right);
    push_int_field(state, tbl, "bottom", bottom);
}

static void push_physical_size_table(MobiusState* state, int width_mm, int height_mm) {
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "width_mm", width_mm);
    push_int_field(state, tbl, "height_mm", height_mm);
}

static void push_video_mode_table(MobiusState* state, const GLFWvidmode* mode) {
    if (!mode) {
        mobius_stack_pushNil(state);
        return;
    }
    mobius_stack_pushNewTable(state, 6);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "width", mode->width);
    push_int_field(state, tbl, "height", mode->height);
    push_int_field(state, tbl, "red_bits", mode->redBits);
    push_int_field(state, tbl, "green_bits", mode->greenBits);
    push_int_field(state, tbl, "blue_bits", mode->blueBits);
    push_int_field(state, tbl, "refresh_rate", mode->refreshRate);
}

static WindowObject* get_window_object(MobiusState* state, int idx, const char* context) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, GLFW_WINDOW_TYPE) != 0) {
        mobius_error(state, context);
        return nullptr;
    }
    return static_cast<WindowObject*>(ptr);
}

static MonitorObject* get_monitor_object(MobiusState* state, int idx, const char* context) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, GLFW_MONITOR_TYPE) != 0) {
        mobius_error(state, context);
        return nullptr;
    }
    return static_cast<MonitorObject*>(ptr);
}

static int ensure_window_alive(MobiusState* state, WindowObject* window, const char* context) {
    if (!window || window->destroyed || !window->handle) {
        return mobius_error(state, context);
    }
    return 0;
}

static int return_self(MobiusState* state, int arg_count) {
    mobius_stack_copy(state, 0);
    mobius_stack_pop(state, arg_count);
    return 1;
}

static void release_value_ref(MobiusState* state, MobiusValueRef& ref) {
    if (ref != 0) {
        mobius_unref_value(state, ref);
        ref = 0;
    }
}

static void clear_window_callbacks(WindowObject* window) {
    if (!window || !window->state) return;
    release_value_ref(window->state, window->on_key_ref);
    release_value_ref(window->state, window->on_char_ref);
    release_value_ref(window->state, window->on_cursor_pos_ref);
    release_value_ref(window->state, window->on_mouse_button_ref);
    release_value_ref(window->state, window->on_scroll_ref);
    release_value_ref(window->state, window->on_resize_ref);
    release_value_ref(window->state, window->on_framebuffer_resize_ref);
    release_value_ref(window->state, window->on_close_ref);
    release_value_ref(window->state, window->on_focus_ref);
    release_value_ref(window->state, window->on_cursor_enter_ref);
    release_value_ref(window->state, window->on_iconify_ref);
    release_value_ref(window->state, window->on_maximize_ref);
}

static void free_event_refs(PendingCallbackEvent& event) {
    if (!event.state) return;
    if (event.function_ref) {
        mobius_unref_value(event.state, event.function_ref);
        event.function_ref = 0;
    }
    for (size_t i = 0; i < event.arg_count; i++) {
        if (event.args[i]) mobius_unref_value(event.state, event.args[i]);
        event.args[i] = 0;
    }
    event.arg_count = 0;
}

static void clear_pending_events_for_state(MobiusState* state) {
    std::lock_guard<std::mutex> lock(g_glfw_mutex);
    std::vector<PendingCallbackEvent> kept;
    kept.reserve(g_pending_events.size());
    for (PendingCallbackEvent& event : g_pending_events) {
        if (event.state == state) {
            free_event_refs(event);
        } else {
            kept.push_back(std::move(event));
        }
    }
    g_pending_events.swap(kept);
}

static void clear_pending_events_for_state_locked(MobiusState* state) {
    std::vector<PendingCallbackEvent> kept;
    kept.reserve(g_pending_events.size());
    for (PendingCallbackEvent& event : g_pending_events) {
        if (event.state == state) {
            free_event_refs(event);
        } else {
            kept.push_back(std::move(event));
        }
    }
    g_pending_events.swap(kept);
}

static void clear_all_pending_events() {
    std::lock_guard<std::mutex> lock(g_glfw_mutex);
    for (PendingCallbackEvent& event : g_pending_events) {
        free_event_refs(event);
    }
    g_pending_events.clear();
}

static void invalidate_window_for_terminate(WindowObject* window) {
    if (!window) return;
    window->handle = nullptr;
    window->destroyed = true;
    clear_window_callbacks(window);
}

static void destroy_window(WindowObject* window) {
    if (!window) return;

    GLFWwindow* handle = window->handle;
    window->handle = nullptr;
    window->destroyed = true;

    {
        std::lock_guard<std::mutex> lock(g_glfw_mutex);
        g_live_windows.erase(window);
    }

    clear_window_callbacks(window);

    if (handle) {
        glfwSetWindowUserPointer(handle, nullptr);
        glfwSetKeyCallback(handle, nullptr);
        glfwSetCharCallback(handle, nullptr);
        glfwSetCursorPosCallback(handle, nullptr);
        glfwSetMouseButtonCallback(handle, nullptr);
        glfwSetScrollCallback(handle, nullptr);
        glfwSetWindowSizeCallback(handle, nullptr);
        glfwSetFramebufferSizeCallback(handle, nullptr);
        glfwSetWindowCloseCallback(handle, nullptr);
        glfwSetWindowFocusCallback(handle, nullptr);
        glfwSetCursorEnterCallback(handle, nullptr);
        glfwSetWindowIconifyCallback(handle, nullptr);
        glfwSetWindowMaximizeCallback(handle, nullptr);
        glfwDestroyWindow(handle);
    }
}

static void window_object_destructor(void* ptr) {
    WindowObject* window = static_cast<WindowObject*>(ptr);
    if (!window) return;
    destroy_window(window);
    delete window;
}

static void monitor_object_destructor(void* ptr) {
    MonitorObject* monitor = static_cast<MonitorObject*>(ptr);
    delete monitor;
}

static Value make_window_value(WindowObject* window) {
    Value value;
    value.type = VAL_USERDATA;
    value.flags = 0;
    value.aux = 0;
    value.as.userdata = window ? window->userdata_handle : nullptr;
    return value;
}

static int push_window_for_handle(MobiusState* state, GLFWwindow* handle) {
    if (!handle) {
        mobius_stack_pushNil(state);
        return 1;
    }
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window || !window->userdata_handle) {
        mobius_stack_pushNil(state);
        return 1;
    }
    state->npush(make_window_value(window));
    return 1;
}

static MobiusValueRef duplicate_ref(MobiusState* state, MobiusValueRef ref) {
    Value value;
    if (!state || ref == 0 || !state->copyValueRef(ref, &value)) return 0;
    return state->createValueRef(value);
}

static const Value* get_native_arg_value(MobiusState* state, int idx, int arg_count) {
    NativeCallContext* ctx = state ? state->nativeContext() : nullptr;
    if (!ctx || idx < 0 || idx >= arg_count) return nullptr;
    int abs = ctx->base + idx;
    if (abs < ctx->base || abs >= ctx->top) return nullptr;
    return &ctx->registers[abs];
}

static EnumDefinition* get_native_enum_definition_arg(MobiusState* state, int idx, int arg_count) {
    const Value* value = get_native_arg_value(state, idx, arg_count);
    if (!value || value->type != VAL_ENUM || !value->as.enum_def || value->aux != -1) return nullptr;
    return value->as.enum_def;
}

static void queue_callback_event(WindowObject* window, MobiusValueRef callback_ref,
                                 std::initializer_list<Value> args) {
    if (!window || !window->state || !window->handle || callback_ref == 0) return;

    PendingCallbackEvent event;
    event.state = window->state;
    event.function_ref = duplicate_ref(window->state, callback_ref);
    if (!event.function_ref) return;

    for (const Value& arg : args) {
        if (event.arg_count >= event.args.size()) break;
        MobiusValueRef arg_ref = window->state->createValueRef(arg);
        if (!arg_ref) {
            free_event_refs(event);
            return;
        }
        event.args[event.arg_count++] = arg_ref;
    }

    std::lock_guard<std::mutex> lock(g_glfw_mutex);
    g_pending_events.push_back(std::move(event));
}

static int dispatch_pending_events(MobiusState* state) {
    std::vector<PendingCallbackEvent> local_events;
    {
        std::lock_guard<std::mutex> lock(g_glfw_mutex);
        std::vector<PendingCallbackEvent> kept;
        kept.reserve(g_pending_events.size());
        for (PendingCallbackEvent& event : g_pending_events) {
            if (event.state == state) local_events.push_back(std::move(event));
            else kept.push_back(std::move(event));
        }
        g_pending_events.swap(kept);
    }

    int rc = 0;
    for (PendingCallbackEvent& event : local_events) {
        if (rc == 0) {
            int call_rc = mobius_call_ref(state, event.function_ref, event.args.data(), event.arg_count, 1);
            if (call_rc < 0) rc = -1;
            else if (call_rc > 0) mobius_stack_pop(state, call_rc);
        }
        free_event_refs(event);
    }
    return rc;
}

static void glfw_key_callback(GLFWwindow* handle, int key, int scancode, int action, int mods) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_key_ref,
                         {make_window_value(window),
                          make_installed_enum_value(window->state, "Key", key),
                          make_int64_value(scancode),
                          make_installed_enum_value(window->state, "Action", action),
                          make_installed_enum_value(window->state, "Modifier", mods)});
}

static void glfw_char_callback(GLFWwindow* handle, unsigned int codepoint) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_char_ref,
                         {make_window_value(window), make_uint64_value(codepoint)});
}

static void glfw_cursor_pos_callback(GLFWwindow* handle, double x, double y) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_cursor_pos_ref,
                         {make_window_value(window), make_float_value(x), make_float_value(y)});
}

static void glfw_mouse_button_callback(GLFWwindow* handle, int button, int action, int mods) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_mouse_button_ref,
                         {make_window_value(window),
                          make_installed_enum_value(window->state, "MouseButton", button),
                          make_installed_enum_value(window->state, "Action", action),
                          make_installed_enum_value(window->state, "Modifier", mods)});
}

static void glfw_scroll_callback(GLFWwindow* handle, double xoffset, double yoffset) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_scroll_ref,
                         {make_window_value(window), make_float_value(xoffset), make_float_value(yoffset)});
}

static void glfw_window_size_callback(GLFWwindow* handle, int width, int height) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_resize_ref,
                         {make_window_value(window), make_int64_value(width), make_int64_value(height)});
}

static void glfw_framebuffer_size_callback(GLFWwindow* handle, int width, int height) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_framebuffer_resize_ref,
                         {make_window_value(window), make_int64_value(width), make_int64_value(height)});
}

static void glfw_window_close_callback(GLFWwindow* handle) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_close_ref, {make_window_value(window)});
}

static void glfw_window_focus_callback(GLFWwindow* handle, int focused) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_focus_ref,
                         {make_window_value(window), make_bool_value(focused == GLFW_TRUE)});
}

static void glfw_cursor_enter_callback(GLFWwindow* handle, int entered) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_cursor_enter_ref,
                         {make_window_value(window), make_bool_value(entered == GLFW_TRUE)});
}

static void glfw_window_iconify_callback(GLFWwindow* handle, int iconified) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_iconify_ref,
                         {make_window_value(window), make_bool_value(iconified == GLFW_TRUE)});
}

static void glfw_window_maximize_callback(GLFWwindow* handle, int maximized) {
    WindowObject* window = static_cast<WindowObject*>(glfwGetWindowUserPointer(handle));
    if (!window) return;
    queue_callback_event(window, window->on_maximize_ref,
                         {make_window_value(window), make_bool_value(maximized == GLFW_TRUE)});
}

static int set_callback_ref(MobiusState* state, MobiusValueRef& slot, int idx, const char* context) {
    if (mobius_stack_isNil(state, idx)) {
        release_value_ref(state, slot);
        return 0;
    }
    if (!mobius_stack_isFunction(state, idx)) {
        return mobius_error(state, context);
    }

    MobiusValueRef next = mobius_ref_value(state, idx);
    if (!next) return mobius_error(state, "failed to root callback function");
    release_value_ref(state, slot);
    slot = next;
    return 0;
}

static int glfw_version(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "version() expects no arguments");

    int major = 0;
    int minor = 0;
    int revision = 0;
    glfwGetVersion(&major, &minor, &revision);

    mobius_stack_pushNewTable(state, 3);
    int tbl = mobius_stack_size(state) - 1;
    push_int_field(state, tbl, "major", major);
    push_int_field(state, tbl, "minor", minor);
    push_int_field(state, tbl, "revision", revision);
    return 1;
}

static int glfw_version_string(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "version_string() expects no arguments");
    mobius_stack_pushString(state, glfwGetVersionString());
    return 1;
}

static int glfw_vulkan_supported(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "vulkan_supported() expects no arguments");
    mobius_stack_pushBool(state, glfwVulkanSupported() == GLFW_TRUE);
    return 1;
}

static int glfw_init(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "init() expects no arguments");

    std::lock_guard<std::mutex> lock(g_glfw_mutex);
    if (g_initialized_states.find(state) != g_initialized_states.end()) {
        mobius_stack_pushBool(state, true);
        return 1;
    }

    if (g_initialized_states.empty()) {
        if (glfwInit() != GLFW_TRUE) {
            mobius_stack_pushBool(state, false);
            return 1;
        }
    }

    g_initialized_states.insert(state);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_terminate(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "terminate() expects no arguments");

    bool should_terminate = false;
    {
        std::lock_guard<std::mutex> lock(g_glfw_mutex);
        auto it = g_initialized_states.find(state);
        if (it == g_initialized_states.end()) {
            mobius_stack_pushBool(state, false);
            return 1;
        }
        g_initialized_states.erase(it);
        clear_pending_events_for_state_locked(state);
        should_terminate = g_initialized_states.empty();
        if (should_terminate) {
            for (WindowObject* window : g_live_windows) invalidate_window_for_terminate(window);
            g_live_windows.clear();
        }
    }

    if (should_terminate) {
        clear_all_pending_events();
        glfwTerminate();
    }

    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_initialized(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "initialized() expects no arguments");
    mobius_stack_pushBool(state, state_is_initialized(state));
    return 1;
}

static int glfw_last_error(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "last_error() expects no arguments");
    const char* description = nullptr;
    int code = glfwGetError(&description);
    push_error_table(state, code, description);
    return 1;
}

static int glfw_default_window_hints(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "default_window_hints() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before default_window_hints()");
    glfwDefaultWindowHints();
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_window_hint(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window_hint() expects 2 arguments (hint, value)");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before window_hint()");
    int hint = (int)mobius_stack_asInt64(state, 0);
    int value = (int)mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);
    glfwWindowHint(hint, value);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_required_instance_extensions(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "required_instance_extensions() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before required_instance_extensions()");
    uint32_t count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    if (!extensions) {
        mobius_stack_pushNil(state);
        return 1;
    }
    mobius_stack_pushNewArray(state, count);
    int arr = mobius_stack_size(state) - 1;
    for (uint32_t i = 0; i < count; i++) {
        mobius_stack_pushString(state, extensions[i]);
        mobius_stack_arrayPush(state, arr);
    }
    return 1;
}

static int glfw_wait_events_timeout(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "wait_events_timeout() expects 1 argument (seconds)");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before wait_events_timeout()");
    double timeout = mobius_stack_asFloat64(state, 0);
    mobius_stack_pop(state, 1);
    if (timeout < 0.0) return mobius_error(state, "wait_events_timeout() timeout must be >= 0");
    glfwWaitEventsTimeout(timeout);
    if (dispatch_pending_events(state) < 0) return -1;
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_get_time(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "get_time() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before get_time()");
    mobius_stack_pushFloat64(state, glfwGetTime());
    return 1;
}

static int glfw_set_time(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "set_time() expects 1 argument");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before set_time()");
    double value = mobius_stack_asFloat64(state, 0);
    mobius_stack_pop(state, 1);
    if (value < 0.0) return mobius_error(state, "set_time() time must be >= 0");
    glfwSetTime(value);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_get_timer_value(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "timer_value() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before timer_value()");
    mobius_stack_pushUInt64(state, glfwGetTimerValue());
    return 1;
}

static int glfw_get_timer_frequency(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "timer_frequency() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before timer_frequency()");
    mobius_stack_pushUInt64(state, glfwGetTimerFrequency());
    return 1;
}

static int glfw_swap_interval(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "swap_interval() expects 1 argument");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before swap_interval()");
    int interval = (int)mobius_stack_asInt64(state, 0);
    mobius_stack_pop(state, 1);
    glfwSwapInterval(interval);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_extension_supported(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "extension_supported() expects 1 argument");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before extension_supported()");
    if (!mobius_stack_isString(state, 0)) return mobius_error(state, "extension_supported() extension must be a string");
    const char* extension = mobius_stack_asString(state, 0);
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, glfwExtensionSupported(extension) == GLFW_TRUE);
    return 1;
}

static int glfw_raw_mouse_motion_supported(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "raw_mouse_motion_supported() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before raw_mouse_motion_supported()");
    mobius_stack_pushBool(state, glfwRawMouseMotionSupported() == GLFW_TRUE);
    return 1;
}

static int glfw_get_key_name(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "get_key_name() expects 2 arguments (key, scancode)");
    int key = (int)mobius_stack_asInt64(state, 0);
    int scancode = (int)mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);
    const char* name = glfwGetKeyName(key, scancode);
    if (name) mobius_stack_pushString(state, name);
    else mobius_stack_pushNil(state);
    return 1;
}

static int glfw_get_key_scancode(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "get_key_scancode() expects 1 argument");
    int key = (int)mobius_stack_asInt64(state, 0);
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, glfwGetKeyScancode(key));
    return 1;
}

static int glfw_install_enum(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "__install_enum() expects 2 arguments (name, enum_def)");
    if (!mobius_stack_isString(state, 0)) return mobius_error(state, "__install_enum() name must be a string");

    const char* enum_name = mobius_stack_asString(state, 0);
    EnumDefinition* definition = get_native_enum_definition_arg(state, 1, arg_count);
    if (!definition) return mobius_error(state, "__install_enum() enum_def must be an enum definition");

    static_cast<RefCounted*>(definition)->retain();
    {
        std::lock_guard<std::mutex> lock(g_glfw_mutex);
        EnumDefinition*& slot = g_installed_enums[state][enum_name];
        if (slot) slot->release();
        slot = definition;
    }

    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_get_clipboard_string(MobiusState* state, int arg_count) {
    if (arg_count != 0 && arg_count != 1) return mobius_error(state, "get_clipboard_string() expects 0 or 1 arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before get_clipboard_string()");
    GLFWwindow* window = nullptr;
    if (arg_count == 1 && !mobius_stack_isNil(state, 0)) {
        WindowObject* window_obj = get_window_object(state, 0, "get_clipboard_string() window must be a GLFW window");
        if (!window_obj) return -1;
        window = window_obj->handle;
    }
    mobius_stack_pop(state, arg_count);
    const char* text = glfwGetClipboardString(window);
    if (text) mobius_stack_pushString(state, text);
    else mobius_stack_pushNil(state);
    return 1;
}

static int glfw_set_clipboard_string(MobiusState* state, int arg_count) {
    if (arg_count != 1 && arg_count != 2) return mobius_error(state, "set_clipboard_string() expects 1 or 2 arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before set_clipboard_string()");
    if (!mobius_stack_isString(state, 0)) return mobius_error(state, "set_clipboard_string() text must be a string");
    const char* text = mobius_stack_asString(state, 0);
    GLFWwindow* window = nullptr;
    if (arg_count == 2 && !mobius_stack_isNil(state, 1)) {
        WindowObject* window_obj = get_window_object(state, 1, "set_clipboard_string() window must be a GLFW window");
        if (!window_obj) return -1;
        window = window_obj->handle;
    }
    mobius_stack_pop(state, arg_count);
    glfwSetClipboardString(window, text);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_make_context_current(MobiusState* state, int arg_count) {
    if (arg_count != 0 && arg_count != 1) return mobius_error(state, "make_context_current() expects 0 or 1 arguments");
    GLFWwindow* window = nullptr;
    if (arg_count == 1 && !mobius_stack_isNil(state, 0)) {
        WindowObject* window_obj = get_window_object(state, 0, "make_context_current() window must be a GLFW window");
        if (!window_obj) return -1;
        if (ensure_window_alive(state, window_obj, "window has been destroyed") < 0) return -1;
        window = window_obj->handle;
    }
    mobius_stack_pop(state, arg_count);
    glfwMakeContextCurrent(window);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_current_context(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "current_context() expects no arguments");
    return push_window_for_handle(state, glfwGetCurrentContext());
}

static int push_monitor_userdata(MobiusState* state, GLFWmonitor* handle) {
    if (!handle) {
        mobius_stack_pushNil(state);
        return 1;
    }
    MonitorObject* monitor = new (std::nothrow) MonitorObject();
    if (!monitor) return mobius_error(state, "failed to allocate monitor object");
    monitor->state = state;
    monitor->handle = handle;
    mobius_stack_pushUserdata(state, monitor, monitor_object_destructor, GLFW_MONITOR_TYPE, sizeof(MonitorObject));
    return 1;
}

static int glfw_primary_monitor(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "primary_monitor() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before primary_monitor()");
    return push_monitor_userdata(state, glfwGetPrimaryMonitor());
}

static int glfw_monitors(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "monitors() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before monitors()");
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    mobius_stack_pushNewArray(state, (size_t)(count > 0 ? count : 0));
    int arr = mobius_stack_size(state) - 1;
    for (int i = 0; i < count; i++) {
        push_monitor_userdata(state, monitors[i]);
        mobius_stack_arrayPush(state, arr);
    }
    return 1;
}

static int glfw_create_window(MobiusState* state, int arg_count) {
    if (arg_count != 3 && arg_count != 4 && arg_count != 5) {
        return mobius_error(state, "create_window() expects 3 to 5 arguments (width, height, title [, monitor [, share_window]])");
    }
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before create_window()");
    if (!mobius_stack_isString(state, 2)) return mobius_error(state, "create_window() title must be a string");

    int width = (int)mobius_stack_asInt64(state, 0);
    int height = (int)mobius_stack_asInt64(state, 1);
    const char* title = mobius_stack_asString(state, 2);
    GLFWmonitor* monitor = nullptr;
    if (arg_count >= 4 && !mobius_stack_isNil(state, 3)) {
        MonitorObject* monitor_obj = get_monitor_object(state, 3, "create_window() monitor must be a GLFW monitor");
        if (!monitor_obj) return -1;
        monitor = monitor_obj->handle;
    }
    GLFWwindow* share_window = nullptr;
    if (arg_count == 5 && !mobius_stack_isNil(state, 4)) {
        WindowObject* share_obj = get_window_object(state, 4, "create_window() share_window must be a GLFW window");
        if (!share_obj) return -1;
        if (ensure_window_alive(state, share_obj, "share window has been destroyed") < 0) return -1;
        share_window = share_obj->handle;
    }
    if (width <= 0 || height <= 0) return mobius_error(state, "create_window() width and height must be > 0");

    GLFWwindow* handle = glfwCreateWindow(width, height, title, monitor, share_window);
    mobius_stack_pop(state, arg_count);
    if (!handle) {
        mobius_stack_pushNil(state);
        return 1;
    }

    WindowObject* window = new (std::nothrow) WindowObject();
    if (!window) {
        glfwDestroyWindow(handle);
        return mobius_error(state, "failed to allocate GLFW window object");
    }
    window->state = state;
    window->handle = handle;
    window->destroyed = false;

    mobius_stack_pushUserdata(state, window, window_object_destructor, GLFW_WINDOW_TYPE, sizeof(WindowObject));
    Value self_value = state->npeek(0);
    if (self_value.type == VAL_USERDATA) {
        window->userdata_handle = self_value.as.userdata;
    }
    glfwSetWindowUserPointer(handle, window);

    {
        std::lock_guard<std::mutex> lock(g_glfw_mutex);
        g_live_windows.insert(window);
    }
    return 1;
}

static int glfw_poll_events(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "poll_events() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before poll_events()");
    glfwPollEvents();
    if (dispatch_pending_events(state) < 0) return -1;
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_wait_events(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "wait_events() expects no arguments");
    if (!state_is_initialized(state)) return mobius_error(state, "glfw.init() must succeed before wait_events()");
    glfwWaitEvents();
    if (dispatch_pending_events(state) < 0) return -1;
    mobius_stack_pushBool(state, true);
    return 1;
}

static int glfw_post_empty_event(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "post_empty_event() expects no arguments");
    glfwPostEmptyEvent();
    mobius_stack_pushBool(state, true);
    return 1;
}

static int monitor_name(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "monitor:name() expects 0 arguments");
    MonitorObject* monitor = get_monitor_object(state, 0, "monitor:name() self is not a monitor");
    if (!monitor || !monitor->handle) return -1;
    mobius_stack_pop(state, 1);
    const char* name = glfwGetMonitorName(monitor->handle);
    if (name) mobius_stack_pushString(state, name);
    else mobius_stack_pushNil(state);
    return 1;
}

static int monitor_video_mode(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "monitor:video_mode() expects 0 arguments");
    MonitorObject* monitor = get_monitor_object(state, 0, "monitor:video_mode() self is not a monitor");
    if (!monitor || !monitor->handle) return -1;
    mobius_stack_pop(state, 1);
    push_video_mode_table(state, glfwGetVideoMode(monitor->handle));
    return 1;
}

static int monitor_video_modes(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "monitor:video_modes() expects 0 arguments");
    MonitorObject* monitor = get_monitor_object(state, 0, "monitor:video_modes() self is not a monitor");
    if (!monitor || !monitor->handle) return -1;
    mobius_stack_pop(state, 1);
    int count = 0;
    const GLFWvidmode* modes = glfwGetVideoModes(monitor->handle, &count);
    mobius_stack_pushNewArray(state, count > 0 ? (size_t)count : 0);
    int arr = mobius_stack_size(state) - 1;
    for (int i = 0; i < count; i++) {
        push_video_mode_table(state, &modes[i]);
        mobius_stack_arrayPush(state, arr);
    }
    return 1;
}

static int monitor_get_pos(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "monitor:get_pos() expects 0 arguments");
    MonitorObject* monitor = get_monitor_object(state, 0, "monitor:get_pos() self is not a monitor");
    if (!monitor || !monitor->handle) return -1;
    int x = 0;
    int y = 0;
    glfwGetMonitorPos(monitor->handle, &x, &y);
    mobius_stack_pop(state, 1);
    push_pos_table(state, x, y);
    return 1;
}

static int monitor_get_workarea(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "monitor:get_workarea() expects 0 arguments");
    MonitorObject* monitor = get_monitor_object(state, 0, "monitor:get_workarea() self is not a monitor");
    if (!monitor || !monitor->handle) return -1;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    glfwGetMonitorWorkarea(monitor->handle, &x, &y, &width, &height);
    mobius_stack_pop(state, 1);
    push_rect_table(state, x, y, width, height);
    return 1;
}

static int monitor_get_physical_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "monitor:get_physical_size() expects 0 arguments");
    MonitorObject* monitor = get_monitor_object(state, 0, "monitor:get_physical_size() self is not a monitor");
    if (!monitor || !monitor->handle) return -1;
    int width_mm = 0;
    int height_mm = 0;
    glfwGetMonitorPhysicalSize(monitor->handle, &width_mm, &height_mm);
    mobius_stack_pop(state, 1);
    push_physical_size_table(state, width_mm, height_mm);
    return 1;
}

static int monitor_get_content_scale(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "monitor:get_content_scale() expects 0 arguments");
    MonitorObject* monitor = get_monitor_object(state, 0, "monitor:get_content_scale() self is not a monitor");
    if (!monitor || !monitor->handle) return -1;
    float xscale = 0.0f;
    float yscale = 0.0f;
    glfwGetMonitorContentScale(monitor->handle, &xscale, &yscale);
    mobius_stack_pop(state, 1);
    push_content_scale_table(state, xscale, yscale);
    return 1;
}

static int window_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:close() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:close() self is not a window");
    if (!window) return -1;
    mobius_stack_pop(state, 1);
    destroy_window(window);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int window_is_closed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:is_closed() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:is_closed() self is not a window");
    if (!window) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, window->destroyed || !window->handle);
    return 1;
}

static int window_should_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:should_close() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:should_close() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, glfwWindowShouldClose(window->handle) == GLFW_TRUE);
    return 1;
}

static int window_set_should_close(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:set_should_close() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:set_should_close() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    bool value = mobius_stack_asBool(state, 1);
    glfwSetWindowShouldClose(window->handle, value ? GLFW_TRUE : GLFW_FALSE);
    return return_self(state, arg_count);
}

static int window_get_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_size() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_size() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int width = 0;
    int height = 0;
    glfwGetWindowSize(window->handle, &width, &height);
    mobius_stack_pop(state, 1);
    push_size_table(state, width, height);
    return 1;
}

static int window_set_size(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "window:set_size() expects 2 arguments");
    WindowObject* window = get_window_object(state, 0, "window:set_size() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int width = (int)mobius_stack_asInt64(state, 1);
    int height = (int)mobius_stack_asInt64(state, 2);
    if (width <= 0 || height <= 0) return mobius_error(state, "window:set_size() width and height must be > 0");
    glfwSetWindowSize(window->handle, width, height);
    return return_self(state, arg_count);
}

static int window_get_framebuffer_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_framebuffer_size() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_framebuffer_size() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window->handle, &width, &height);
    mobius_stack_pop(state, 1);
    push_size_table(state, width, height);
    return 1;
}

static int window_get_pos(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_pos() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_pos() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int x = 0;
    int y = 0;
    glfwGetWindowPos(window->handle, &x, &y);
    mobius_stack_pop(state, 1);
    push_pos_table(state, x, y);
    return 1;
}

static int window_set_pos(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "window:set_pos() expects 2 arguments");
    WindowObject* window = get_window_object(state, 0, "window:set_pos() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int x = (int)mobius_stack_asInt64(state, 1);
    int y = (int)mobius_stack_asInt64(state, 2);
    glfwSetWindowPos(window->handle, x, y);
    return return_self(state, arg_count);
}

static int window_set_title(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:set_title() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:set_title() self is not a window");
    if (!window) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "window:set_title() title must be a string");
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwSetWindowTitle(window->handle, mobius_stack_asString(state, 1));
    return return_self(state, arg_count);
}

static int window_show(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:show() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:show() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwShowWindow(window->handle);
    return return_self(state, arg_count);
}

static int window_hide(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:hide() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:hide() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwHideWindow(window->handle);
    return return_self(state, arg_count);
}

static int window_focus(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:focus() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:focus() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwFocusWindow(window->handle);
    return return_self(state, arg_count);
}

static int window_get_key(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:get_key() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:get_key() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int key = (int)mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);
    state->npush(make_installed_enum_value(state, "Action", glfwGetKey(window->handle, key)));
    return 1;
}

static int window_get_mouse_button(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:get_mouse_button() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:get_mouse_button() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int button = (int)mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);
    state->npush(make_installed_enum_value(state, "Action", glfwGetMouseButton(window->handle, button)));
    return 1;
}

static int window_get_cursor_pos(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_cursor_pos() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_cursor_pos() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window->handle, &x, &y);
    mobius_stack_pop(state, 1);
    push_cursor_pos_table(state, x, y);
    return 1;
}

static int window_set_cursor_pos(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "window:set_cursor_pos() expects 2 arguments");
    WindowObject* window = get_window_object(state, 0, "window:set_cursor_pos() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    double x = mobius_stack_asFloat64(state, 1);
    double y = mobius_stack_asFloat64(state, 2);
    glfwSetCursorPos(window->handle, x, y);
    return return_self(state, arg_count);
}

static int window_get_frame_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_frame_size() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_frame_size() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    glfwGetWindowFrameSize(window->handle, &left, &top, &right, &bottom);
    mobius_stack_pop(state, 1);
    push_frame_size_table(state, left, top, right, bottom);
    return 1;
}

static int window_get_content_scale(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_content_scale() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_content_scale() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    float xscale = 0.0f;
    float yscale = 0.0f;
    glfwGetWindowContentScale(window->handle, &xscale, &yscale);
    mobius_stack_pop(state, 1);
    push_content_scale_table(state, xscale, yscale);
    return 1;
}

static int window_get_opacity(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_opacity() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_opacity() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, glfwGetWindowOpacity(window->handle));
    return 1;
}

static int window_set_opacity(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:set_opacity() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:set_opacity() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    double opacity = mobius_stack_asFloat64(state, 1);
    if (opacity < 0.0 || opacity > 1.0) return mobius_error(state, "window:set_opacity() opacity must be between 0 and 1");
    glfwSetWindowOpacity(window->handle, (float)opacity);
    return return_self(state, arg_count);
}

static int window_iconify(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:iconify() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:iconify() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwIconifyWindow(window->handle);
    return return_self(state, arg_count);
}

static int window_restore(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:restore() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:restore() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwRestoreWindow(window->handle);
    return return_self(state, arg_count);
}

static int window_maximize(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:maximize() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:maximize() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwMaximizeWindow(window->handle);
    return return_self(state, arg_count);
}

static int window_request_attention(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:request_attention() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:request_attention() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwRequestWindowAttention(window->handle);
    return return_self(state, arg_count);
}

static int window_get_monitor(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:get_monitor() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:get_monitor() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    return push_monitor_userdata(state, glfwGetWindowMonitor(window->handle));
}

static int window_set_monitor(MobiusState* state, int arg_count) {
    if (arg_count != 6 && arg_count != 7) {
        return mobius_error(state, "window:set_monitor() expects 5 or 6 arguments");
    }
    WindowObject* window = get_window_object(state, 0, "window:set_monitor() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    GLFWmonitor* monitor = nullptr;
    if (!mobius_stack_isNil(state, 1)) {
        MonitorObject* monitor_obj = get_monitor_object(state, 1, "window:set_monitor() monitor must be a GLFW monitor or nil");
        if (!monitor_obj) return -1;
        monitor = monitor_obj->handle;
    }
    int x = (int)mobius_stack_asInt64(state, 2);
    int y = (int)mobius_stack_asInt64(state, 3);
    int width = (int)mobius_stack_asInt64(state, 4);
    int height = (int)mobius_stack_asInt64(state, 5);
    int refresh = (arg_count == 7) ? (int)mobius_stack_asInt64(state, 6) : GLFW_DONT_CARE;
    glfwSetWindowMonitor(window->handle, monitor, x, y, width, height, refresh);
    return return_self(state, arg_count);
}

static int window_get_attrib(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:get_attrib() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:get_attrib() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int attrib = (int)mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);
    mobius_stack_pushInt64(state, glfwGetWindowAttrib(window->handle, attrib));
    return 1;
}

static int window_set_attrib(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "window:set_attrib() expects 2 arguments");
    WindowObject* window = get_window_object(state, 0, "window:set_attrib() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int attrib = (int)mobius_stack_asInt64(state, 1);
    int value = (int)mobius_stack_asInt64(state, 2);
    glfwSetWindowAttrib(window->handle, attrib, value);
    return return_self(state, arg_count);
}

static int window_get_input_mode(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:get_input_mode() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:get_input_mode() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int mode = (int)mobius_stack_asInt64(state, 1);
    mobius_stack_pop(state, 2);
    int value = glfwGetInputMode(window->handle, mode);
    if (mode == GLFW_CURSOR) state->npush(make_installed_enum_value(state, "CursorMode", value));
    else mobius_stack_pushInt64(state, value);
    return 1;
}

static int window_set_input_mode(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "window:set_input_mode() expects 2 arguments");
    WindowObject* window = get_window_object(state, 0, "window:set_input_mode() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int mode = (int)mobius_stack_asInt64(state, 1);
    int value = (int)mobius_stack_asInt64(state, 2);
    glfwSetInputMode(window->handle, mode, value);
    return return_self(state, arg_count);
}

static int window_set_size_limits(MobiusState* state, int arg_count) {
    if (arg_count != 5) return mobius_error(state, "window:set_size_limits() expects 4 arguments");
    WindowObject* window = get_window_object(state, 0, "window:set_size_limits() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int min_width = (int)mobius_stack_asInt64(state, 1);
    int min_height = (int)mobius_stack_asInt64(state, 2);
    int max_width = (int)mobius_stack_asInt64(state, 3);
    int max_height = (int)mobius_stack_asInt64(state, 4);
    glfwSetWindowSizeLimits(window->handle, min_width, min_height, max_width, max_height);
    return return_self(state, arg_count);
}

static int window_set_aspect_ratio(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "window:set_aspect_ratio() expects 2 arguments");
    WindowObject* window = get_window_object(state, 0, "window:set_aspect_ratio() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    int numer = (int)mobius_stack_asInt64(state, 1);
    int denom = (int)mobius_stack_asInt64(state, 2);
    glfwSetWindowAspectRatio(window->handle, numer, denom);
    return return_self(state, arg_count);
}

static int window_swap_buffers(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "window:swap_buffers() expects 0 arguments");
    WindowObject* window = get_window_object(state, 0, "window:swap_buffers() self is not a window");
    if (!window) return -1;
    if (ensure_window_alive(state, window, "window has been destroyed") < 0) return -1;
    glfwSwapBuffers(window->handle);
    return return_self(state, arg_count);
}

static int window_on_key(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_key() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_key() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_key_ref, 1, "window:on_key() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetKeyCallback(window->handle, window->on_key_ref ? glfw_key_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_char(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_char() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_char() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_char_ref, 1, "window:on_char() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetCharCallback(window->handle, window->on_char_ref ? glfw_char_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_cursor_pos(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_cursor_pos() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_cursor_pos() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_cursor_pos_ref, 1, "window:on_cursor_pos() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetCursorPosCallback(window->handle, window->on_cursor_pos_ref ? glfw_cursor_pos_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_mouse_button(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_mouse_button() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_mouse_button() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_mouse_button_ref, 1, "window:on_mouse_button() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetMouseButtonCallback(window->handle, window->on_mouse_button_ref ? glfw_mouse_button_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_scroll(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_scroll() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_scroll() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_scroll_ref, 1, "window:on_scroll() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetScrollCallback(window->handle, window->on_scroll_ref ? glfw_scroll_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_resize(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_resize() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_resize() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_resize_ref, 1, "window:on_resize() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetWindowSizeCallback(window->handle, window->on_resize_ref ? glfw_window_size_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_framebuffer_resize(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_framebuffer_resize() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_framebuffer_resize() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_framebuffer_resize_ref, 1, "window:on_framebuffer_resize() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetFramebufferSizeCallback(window->handle, window->on_framebuffer_resize_ref ? glfw_framebuffer_size_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_close(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_close() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_close() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_close_ref, 1, "window:on_close() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetWindowCloseCallback(window->handle, window->on_close_ref ? glfw_window_close_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_focus(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_focus() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_focus() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_focus_ref, 1, "window:on_focus() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetWindowFocusCallback(window->handle, window->on_focus_ref ? glfw_window_focus_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_cursor_enter(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_cursor_enter() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_cursor_enter() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_cursor_enter_ref, 1, "window:on_cursor_enter() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetCursorEnterCallback(window->handle, window->on_cursor_enter_ref ? glfw_cursor_enter_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_iconify(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_iconify() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_iconify() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_iconify_ref, 1, "window:on_iconify() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetWindowIconifyCallback(window->handle, window->on_iconify_ref ? glfw_window_iconify_callback : nullptr);
    return return_self(state, arg_count);
}

static int window_on_maximize(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "window:on_maximize() expects 1 argument");
    WindowObject* window = get_window_object(state, 0, "window:on_maximize() self is not a window");
    if (!window) return -1;
    if (set_callback_ref(state, window->on_maximize_ref, 1, "window:on_maximize() callback must be a function or nil") < 0) return -1;
    if (window->handle) glfwSetWindowMaximizeCallback(window->handle, window->on_maximize_ref ? glfw_window_maximize_callback : nullptr);
    return return_self(state, arg_count);
}

static void copy_module_function(MobiusState* state, int module_idx, const char* from_key, int target_idx, const char* to_key) {
    mobius_stack_getTableField(state, module_idx, from_key);
    mobius_stack_setTableField(state, target_idx, to_key);
}

static int glfw_post_init(MobiusState* state) {
    const int module_idx = 0;

    mobius_stack_pushNewTable(state, 36);
    int window_proto = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__window_close", window_proto, "close");
    copy_module_function(state, module_idx, "__window_close", window_proto, "destroy");
    copy_module_function(state, module_idx, "__window_is_closed", window_proto, "is_closed");
    copy_module_function(state, module_idx, "__window_should_close", window_proto, "should_close");
    copy_module_function(state, module_idx, "__window_set_should_close", window_proto, "set_should_close");
    copy_module_function(state, module_idx, "__window_get_size", window_proto, "get_size");
    copy_module_function(state, module_idx, "__window_set_size", window_proto, "set_size");
    copy_module_function(state, module_idx, "__window_set_size_limits", window_proto, "set_size_limits");
    copy_module_function(state, module_idx, "__window_set_aspect_ratio", window_proto, "set_aspect_ratio");
    copy_module_function(state, module_idx, "__window_get_framebuffer_size", window_proto, "get_framebuffer_size");
    copy_module_function(state, module_idx, "__window_get_frame_size", window_proto, "get_frame_size");
    copy_module_function(state, module_idx, "__window_get_content_scale", window_proto, "get_content_scale");
    copy_module_function(state, module_idx, "__window_get_pos", window_proto, "get_pos");
    copy_module_function(state, module_idx, "__window_set_pos", window_proto, "set_pos");
    copy_module_function(state, module_idx, "__window_set_title", window_proto, "set_title");
    copy_module_function(state, module_idx, "__window_show", window_proto, "show");
    copy_module_function(state, module_idx, "__window_hide", window_proto, "hide");
    copy_module_function(state, module_idx, "__window_focus", window_proto, "focus");
    copy_module_function(state, module_idx, "__window_iconify", window_proto, "iconify");
    copy_module_function(state, module_idx, "__window_restore", window_proto, "restore");
    copy_module_function(state, module_idx, "__window_maximize", window_proto, "maximize");
    copy_module_function(state, module_idx, "__window_request_attention", window_proto, "request_attention");
    copy_module_function(state, module_idx, "__window_get_monitor", window_proto, "get_monitor");
    copy_module_function(state, module_idx, "__window_set_monitor", window_proto, "set_monitor");
    copy_module_function(state, module_idx, "__window_get_key", window_proto, "get_key");
    copy_module_function(state, module_idx, "__window_get_mouse_button", window_proto, "get_mouse_button");
    copy_module_function(state, module_idx, "__window_get_cursor_pos", window_proto, "get_cursor_pos");
    copy_module_function(state, module_idx, "__window_set_cursor_pos", window_proto, "set_cursor_pos");
    copy_module_function(state, module_idx, "__window_get_opacity", window_proto, "get_opacity");
    copy_module_function(state, module_idx, "__window_set_opacity", window_proto, "set_opacity");
    copy_module_function(state, module_idx, "__window_get_attrib", window_proto, "get_attrib");
    copy_module_function(state, module_idx, "__window_set_attrib", window_proto, "set_attrib");
    copy_module_function(state, module_idx, "__window_get_input_mode", window_proto, "get_input_mode");
    copy_module_function(state, module_idx, "__window_set_input_mode", window_proto, "set_input_mode");
    copy_module_function(state, module_idx, "__window_swap_buffers", window_proto, "swap_buffers");
    copy_module_function(state, module_idx, "__window_on_key", window_proto, "on_key");
    copy_module_function(state, module_idx, "__window_on_char", window_proto, "on_char");
    copy_module_function(state, module_idx, "__window_on_cursor_pos", window_proto, "on_cursor_pos");
    copy_module_function(state, module_idx, "__window_on_cursor_enter", window_proto, "on_cursor_enter");
    copy_module_function(state, module_idx, "__window_on_mouse_button", window_proto, "on_mouse_button");
    copy_module_function(state, module_idx, "__window_on_scroll", window_proto, "on_scroll");
    copy_module_function(state, module_idx, "__window_on_resize", window_proto, "on_resize");
    copy_module_function(state, module_idx, "__window_on_framebuffer_resize", window_proto, "on_framebuffer_resize");
    copy_module_function(state, module_idx, "__window_on_close", window_proto, "on_close");
    copy_module_function(state, module_idx, "__window_on_focus", window_proto, "on_focus");
    copy_module_function(state, module_idx, "__window_on_iconify", window_proto, "on_iconify");
    copy_module_function(state, module_idx, "__window_on_maximize", window_proto, "on_maximize");
    mobius_set_userdata_type_metatable(state, GLFW_WINDOW_TYPE);

    mobius_stack_pushNewTable(state, 6);
    int monitor_proto = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__monitor_name", monitor_proto, "name");
    copy_module_function(state, module_idx, "__monitor_video_mode", monitor_proto, "video_mode");
    copy_module_function(state, module_idx, "__monitor_video_modes", monitor_proto, "video_modes");
    copy_module_function(state, module_idx, "__monitor_get_pos", monitor_proto, "get_pos");
    copy_module_function(state, module_idx, "__monitor_get_workarea", monitor_proto, "get_workarea");
    copy_module_function(state, module_idx, "__monitor_get_physical_size", monitor_proto, "get_physical_size");
    copy_module_function(state, module_idx, "__monitor_get_content_scale", monitor_proto, "get_content_scale");
    mobius_set_userdata_type_metatable(state, GLFW_MONITOR_TYPE);

    const char* hidden_keys[] = {
        "__window_close", "__window_is_closed", "__window_should_close", "__window_set_should_close",
        "__window_get_size", "__window_set_size", "__window_set_size_limits", "__window_set_aspect_ratio",
        "__window_get_framebuffer_size", "__window_get_frame_size", "__window_get_content_scale",
        "__window_get_pos", "__window_set_pos", "__window_set_title", "__window_show",
        "__window_hide", "__window_focus", "__window_iconify", "__window_restore",
        "__window_maximize", "__window_request_attention", "__window_get_monitor", "__window_set_monitor",
        "__window_get_key", "__window_get_mouse_button", "__window_get_cursor_pos", "__window_set_cursor_pos",
        "__window_get_opacity", "__window_set_opacity", "__window_get_attrib", "__window_set_attrib",
        "__window_get_input_mode", "__window_set_input_mode", "__window_swap_buffers",
        "__window_on_key", "__window_on_char", "__window_on_cursor_pos", "__window_on_cursor_enter",
        "__window_on_mouse_button", "__window_on_scroll", "__window_on_resize",
        "__window_on_framebuffer_resize", "__window_on_close", "__window_on_focus",
        "__window_on_iconify", "__window_on_maximize", "__monitor_name", "__monitor_video_mode",
        "__monitor_video_modes", "__monitor_get_pos", "__monitor_get_workarea",
        "__monitor_get_physical_size", "__monitor_get_content_scale"
    };
    for (const char* key : hidden_keys) {
        mobius_stack_pushNil(state);
        mobius_stack_setTableField(state, module_idx, key);
    }

    return 0;
}

static void cleanup_glfw_plugin(void) {
    {
        std::lock_guard<std::mutex> lock(g_glfw_mutex);
        for (WindowObject* window : g_live_windows) invalidate_window_for_terminate(window);
        g_live_windows.clear();
        g_initialized_states.clear();
        for (auto& entry : g_installed_enums) release_installed_enum_map(entry.second);
        g_installed_enums.clear();
    }
    clear_all_pending_events();
    glfwTerminate();
}

static MobiusPluginFunction glfw_functions[] = {
    {"init", glfw_init, 0, MOBIUS_VAL_BOOL, "Initialize GLFW for this Mobius state"},
    {"terminate", glfw_terminate, 0, MOBIUS_VAL_BOOL, "Terminate GLFW for this Mobius state"},
    {"initialized", glfw_initialized, 0, MOBIUS_VAL_BOOL, "Return whether GLFW has been initialized for this state"},
    {"last_error", glfw_last_error, 0, MOBIUS_VAL_TABLE, "Return the most recent GLFW error"},
    {"version", glfw_version, 0, MOBIUS_VAL_TABLE, "Return the compiled GLFW version"},
    {"version_string", glfw_version_string, 0, MOBIUS_VAL_STRING, "Return the GLFW version string"},
    {"vulkan_supported", glfw_vulkan_supported, 0, MOBIUS_VAL_BOOL, "Return whether the current system supports Vulkan through GLFW"},
    {"default_window_hints", glfw_default_window_hints, 0, MOBIUS_VAL_BOOL, "Reset GLFW window hints to defaults"},
    {"window_hint", glfw_window_hint, 2, MOBIUS_VAL_BOOL, "Set a GLFW window hint"},
    {"required_instance_extensions", glfw_required_instance_extensions, 0, MOBIUS_VAL_ARRAY, "Return Vulkan instance extensions required by GLFW"},
    {"wait_events_timeout", glfw_wait_events_timeout, 1, MOBIUS_VAL_BOOL, "Wait for GLFW events with a timeout and dispatch callbacks"},
    {"get_time", glfw_get_time, 0, MOBIUS_VAL_FLOAT64, "Return the GLFW timer value in seconds"},
    {"set_time", glfw_set_time, 1, MOBIUS_VAL_BOOL, "Set the GLFW timer value in seconds"},
    {"timer_value", glfw_get_timer_value, 0, MOBIUS_VAL_UINT64, "Return the raw GLFW timer counter"},
    {"timer_frequency", glfw_get_timer_frequency, 0, MOBIUS_VAL_UINT64, "Return the GLFW timer frequency"},
    {"swap_interval", glfw_swap_interval, 1, MOBIUS_VAL_BOOL, "Set the current context swap interval"},
    {"extension_supported", glfw_extension_supported, 1, MOBIUS_VAL_BOOL, "Return whether an OpenGL or WGL/GLX/EGL extension is supported"},
    {"raw_mouse_motion_supported", glfw_raw_mouse_motion_supported, 0, MOBIUS_VAL_BOOL, "Return whether raw mouse motion is supported"},
    {"get_key_name", glfw_get_key_name, 2, MOBIUS_VAL_STRING, "Return the localized name of a keyboard key"},
    {"get_key_scancode", glfw_get_key_scancode, 1, MOBIUS_VAL_INT64, "Return the platform scancode for a GLFW key"},
    {"__install_enum", glfw_install_enum, 2, MOBIUS_VAL_BOOL, "Internal enum installer"},
    {"get_clipboard_string", glfw_get_clipboard_string, SIZE_MAX, MOBIUS_VAL_STRING, "Return the current clipboard string"},
    {"set_clipboard_string", glfw_set_clipboard_string, SIZE_MAX, MOBIUS_VAL_BOOL, "Set the current clipboard string"},
    {"make_context_current", glfw_make_context_current, SIZE_MAX, MOBIUS_VAL_BOOL, "Bind a window context as current or clear the current context"},
    {"current_context", glfw_current_context, 0, MOBIUS_VAL_USERDATA, "Return the current GLFW context window"},
    {"primary_monitor", glfw_primary_monitor, 0, MOBIUS_VAL_USERDATA, "Return the primary monitor"},
    {"monitors", glfw_monitors, 0, MOBIUS_VAL_ARRAY, "Return all detected monitors"},
    {"create_window", glfw_create_window, SIZE_MAX, MOBIUS_VAL_USERDATA, "Create a GLFW window"},
    {"poll_events", glfw_poll_events, 0, MOBIUS_VAL_BOOL, "Poll GLFW events and dispatch callbacks"},
    {"wait_events", glfw_wait_events, 0, MOBIUS_VAL_BOOL, "Wait for GLFW events and dispatch callbacks"},
    {"post_empty_event", glfw_post_empty_event, 0, MOBIUS_VAL_BOOL, "Post an empty GLFW event"},
    {"__window_close", window_close, 1, MOBIUS_VAL_BOOL, "Internal window close method"},
    {"__window_is_closed", window_is_closed, 1, MOBIUS_VAL_BOOL, "Internal window closed-state method"},
    {"__window_should_close", window_should_close, 1, MOBIUS_VAL_BOOL, "Internal window should_close method"},
    {"__window_set_should_close", window_set_should_close, 2, MOBIUS_VAL_USERDATA, "Internal window set_should_close method"},
    {"__window_get_size", window_get_size, 1, MOBIUS_VAL_TABLE, "Internal window get_size method"},
    {"__window_set_size", window_set_size, 3, MOBIUS_VAL_USERDATA, "Internal window set_size method"},
    {"__window_set_size_limits", window_set_size_limits, 5, MOBIUS_VAL_USERDATA, "Internal window set_size_limits method"},
    {"__window_set_aspect_ratio", window_set_aspect_ratio, 3, MOBIUS_VAL_USERDATA, "Internal window set_aspect_ratio method"},
    {"__window_get_framebuffer_size", window_get_framebuffer_size, 1, MOBIUS_VAL_TABLE, "Internal window get_framebuffer_size method"},
    {"__window_get_frame_size", window_get_frame_size, 1, MOBIUS_VAL_TABLE, "Internal window get_frame_size method"},
    {"__window_get_content_scale", window_get_content_scale, 1, MOBIUS_VAL_TABLE, "Internal window get_content_scale method"},
    {"__window_get_pos", window_get_pos, 1, MOBIUS_VAL_TABLE, "Internal window get_pos method"},
    {"__window_set_pos", window_set_pos, 3, MOBIUS_VAL_USERDATA, "Internal window set_pos method"},
    {"__window_set_title", window_set_title, 2, MOBIUS_VAL_USERDATA, "Internal window set_title method"},
    {"__window_show", window_show, 1, MOBIUS_VAL_USERDATA, "Internal window show method"},
    {"__window_hide", window_hide, 1, MOBIUS_VAL_USERDATA, "Internal window hide method"},
    {"__window_focus", window_focus, 1, MOBIUS_VAL_USERDATA, "Internal window focus method"},
    {"__window_iconify", window_iconify, 1, MOBIUS_VAL_USERDATA, "Internal window iconify method"},
    {"__window_restore", window_restore, 1, MOBIUS_VAL_USERDATA, "Internal window restore method"},
    {"__window_maximize", window_maximize, 1, MOBIUS_VAL_USERDATA, "Internal window maximize method"},
    {"__window_request_attention", window_request_attention, 1, MOBIUS_VAL_USERDATA, "Internal window request_attention method"},
    {"__window_get_monitor", window_get_monitor, 1, MOBIUS_VAL_USERDATA, "Internal window get_monitor method"},
    {"__window_set_monitor", window_set_monitor, SIZE_MAX, MOBIUS_VAL_USERDATA, "Internal window set_monitor method"},
    {"__window_get_key", window_get_key, 2, MOBIUS_VAL_INT64, "Internal window get_key method"},
    {"__window_get_mouse_button", window_get_mouse_button, 2, MOBIUS_VAL_INT64, "Internal window get_mouse_button method"},
    {"__window_get_cursor_pos", window_get_cursor_pos, 1, MOBIUS_VAL_TABLE, "Internal window get_cursor_pos method"},
    {"__window_set_cursor_pos", window_set_cursor_pos, 3, MOBIUS_VAL_USERDATA, "Internal window set_cursor_pos method"},
    {"__window_get_opacity", window_get_opacity, 1, MOBIUS_VAL_FLOAT64, "Internal window get_opacity method"},
    {"__window_set_opacity", window_set_opacity, 2, MOBIUS_VAL_USERDATA, "Internal window set_opacity method"},
    {"__window_get_attrib", window_get_attrib, 2, MOBIUS_VAL_INT64, "Internal window get_attrib method"},
    {"__window_set_attrib", window_set_attrib, 3, MOBIUS_VAL_USERDATA, "Internal window set_attrib method"},
    {"__window_get_input_mode", window_get_input_mode, 2, MOBIUS_VAL_INT64, "Internal window get_input_mode method"},
    {"__window_set_input_mode", window_set_input_mode, 3, MOBIUS_VAL_USERDATA, "Internal window set_input_mode method"},
    {"__window_swap_buffers", window_swap_buffers, 1, MOBIUS_VAL_USERDATA, "Internal window swap_buffers method"},
    {"__window_on_key", window_on_key, 2, MOBIUS_VAL_USERDATA, "Internal window on_key setter"},
    {"__window_on_char", window_on_char, 2, MOBIUS_VAL_USERDATA, "Internal window on_char setter"},
    {"__window_on_cursor_pos", window_on_cursor_pos, 2, MOBIUS_VAL_USERDATA, "Internal window on_cursor_pos setter"},
    {"__window_on_cursor_enter", window_on_cursor_enter, 2, MOBIUS_VAL_USERDATA, "Internal window on_cursor_enter setter"},
    {"__window_on_mouse_button", window_on_mouse_button, 2, MOBIUS_VAL_USERDATA, "Internal window on_mouse_button setter"},
    {"__window_on_scroll", window_on_scroll, 2, MOBIUS_VAL_USERDATA, "Internal window on_scroll setter"},
    {"__window_on_resize", window_on_resize, 2, MOBIUS_VAL_USERDATA, "Internal window on_resize setter"},
    {"__window_on_framebuffer_resize", window_on_framebuffer_resize, 2, MOBIUS_VAL_USERDATA, "Internal window on_framebuffer_resize setter"},
    {"__window_on_close", window_on_close, 2, MOBIUS_VAL_USERDATA, "Internal window on_close setter"},
    {"__window_on_focus", window_on_focus, 2, MOBIUS_VAL_USERDATA, "Internal window on_focus setter"},
    {"__window_on_iconify", window_on_iconify, 2, MOBIUS_VAL_USERDATA, "Internal window on_iconify setter"},
    {"__window_on_maximize", window_on_maximize, 2, MOBIUS_VAL_USERDATA, "Internal window on_maximize setter"},
    {"__monitor_name", monitor_name, 1, MOBIUS_VAL_STRING, "Internal monitor name method"},
    {"__monitor_video_mode", monitor_video_mode, 1, MOBIUS_VAL_TABLE, "Internal monitor video_mode method"},
    {"__monitor_video_modes", monitor_video_modes, 1, MOBIUS_VAL_ARRAY, "Internal monitor video_modes method"},
    {"__monitor_get_pos", monitor_get_pos, 1, MOBIUS_VAL_TABLE, "Internal monitor get_pos method"},
    {"__monitor_get_workarea", monitor_get_workarea, 1, MOBIUS_VAL_TABLE, "Internal monitor get_workarea method"},
    {"__monitor_get_physical_size", monitor_get_physical_size, 1, MOBIUS_VAL_TABLE, "Internal monitor get_physical_size method"},
    {"__monitor_get_content_scale", monitor_get_content_scale, 1, MOBIUS_VAL_TABLE, "Internal monitor get_content_scale method"},
};

static MobiusPlugin glfw_plugin = {
    .metadata = {
        .name = "glfw",
        .version = "0.4.0",
        .description = "GLFW windowing and input bindings for Mobius",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = glfw_functions,
    .function_count = sizeof(glfw_functions) / sizeof(glfw_functions[0]),
    .init_plugin = nullptr,
    .cleanup_plugin = cleanup_glfw_plugin,
    .post_init = glfw_post_init,
};

} // namespace

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &glfw_plugin;
}
