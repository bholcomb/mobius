#include <mobius/mobius_plugin.h>

#include <monstro.h>

#include <dlfcn.h>

#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

namespace {

static const char* MONSTRO_CONTEXT_TYPE = "monstro_context";

struct ContextObject {
    MonstroContext* handle = nullptr;
    bool closed = false;
};

struct MonstroApi {
    void* handle = nullptr;

    MonstroContext* (*context_create)(void) = nullptr;
    void (*context_destroy)(MonstroContext* ctx) = nullptr;
    MonstroDrawBuffer* (*context_get_draw_buffer)(MonstroContext* ctx) = nullptr;
    void (*context_get_draw_commands)(MonstroContext* ctx, const MonstroDrawCommand** out_commands, uint32_t* out_count) = nullptr;
    void (*draw_buffer_get_vertex_buffer)(MonstroDrawBuffer* buf, const MonstroVertex** out_data, size_t* out_byte_size) = nullptr;
    void (*draw_buffer_get_index_buffer)(MonstroDrawBuffer* buf, const uint32_t** out_data, uint32_t* out_count) = nullptr;

    void (*set_display_size)(MonstroContext* ctx, float width, float height) = nullptr;
    void (*get_display_size)(MonstroContext* ctx, float* out_width, float* out_height) = nullptr;
    void (*begin_frame)(MonstroContext* ctx, float delta_time) = nullptr;
    void (*end_frame)(MonstroContext* ctx) = nullptr;
    float (*get_delta_time)(MonstroContext* ctx) = nullptr;
    float (*get_time)(MonstroContext* ctx) = nullptr;
    void (*push_id)(MonstroContext* ctx, const char* name) = nullptr;
    void (*pop_id)(MonstroContext* ctx) = nullptr;
    uint64_t (*get_id)(MonstroContext* ctx, const char* name) = nullptr;

    void (*set_mouse_pos)(MonstroContext* ctx, float x, float y) = nullptr;
    void (*set_mouse_button)(MonstroContext* ctx, MonstroMouseButton button, bool down) = nullptr;
    void (*set_mouse_wheel)(MonstroContext* ctx, float wheel_x, float wheel_y) = nullptr;
    void (*set_key)(MonstroContext* ctx, MonstroKey key, bool down) = nullptr;
    void (*set_key_mods)(MonstroContext* ctx, MonstroModFlags mods) = nullptr;
    void (*add_input_text)(MonstroContext* ctx, const char* utf8_text) = nullptr;
    MonstroVec2 (*get_mouse_pos)(MonstroContext* ctx) = nullptr;
    bool (*want_capture_mouse)(MonstroContext* ctx) = nullptr;
    bool (*want_capture_keyboard)(MonstroContext* ctx) = nullptr;
    bool (*want_text_input)(MonstroContext* ctx) = nullptr;

    void (*set_next_window_pos)(MonstroContext* ctx, float x, float y, MonstroCondition cond) = nullptr;
    void (*set_next_window_size)(MonstroContext* ctx, float width, float height, MonstroCondition cond) = nullptr;
    bool (*begin_window)(MonstroContext* ctx, const char* name, bool* p_open, MonstroWindowFlags flags) = nullptr;
    void (*end_window)(MonstroContext* ctx) = nullptr;
    MonstroRect (*get_window_rect)(MonstroContext* ctx) = nullptr;
    MonstroVec2 (*get_window_size)(MonstroContext* ctx) = nullptr;
    bool (*begin_child)(MonstroContext* ctx, const char* str_id, float width, float height, MonstroWindowFlags flags) = nullptr;
    void (*end_child)(MonstroContext* ctx) = nullptr;

    bool (*button)(MonstroContext* ctx, const char* label, const char* tooltip) = nullptr;
    void (*label)(MonstroContext* ctx, const char* text) = nullptr;
    bool (*checkbox)(MonstroContext* ctx, const char* label, bool* value, const char* tooltip) = nullptr;
    bool (*slider_float)(MonstroContext* ctx, const char* label, float* value, float min, float max, const char* format, const char* tooltip) = nullptr;
    bool (*slider_int)(MonstroContext* ctx, const char* label, int* value, int min, int max, const char* format, const char* tooltip) = nullptr;
    void (*progress_bar)(MonstroContext* ctx, float fraction, float width, float height, const char* overlay) = nullptr;
};

static MonstroApi g_api;
static std::mutex g_api_mutex;

template <typename T>
static bool load_symbol(void* handle, const char* symbol_name, T& out, std::string& error) {
    dlerror();
    out = reinterpret_cast<T>(dlsym(handle, symbol_name));
    const char* sym_error = dlerror();
    if (!out || sym_error) {
        error = std::string("missing symbol '") + symbol_name + "': " +
                (sym_error ? sym_error : "not found");
        return false;
    }
    return true;
}

static std::string directory_name(const std::string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

static std::string plugin_directory() {
    Dl_info info {};
    if (dladdr(reinterpret_cast<void*>(&plugin_directory), &info) == 0 || !info.dli_fname) {
        return ".";
    }
    return directory_name(info.dli_fname);
}

static bool ensure_api_loaded(MobiusState* state) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (g_api.handle) return true;

    const std::string runtime_path = plugin_directory() + "/libmonstro.so";
    g_api.handle = dlopen(runtime_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!g_api.handle) {
        g_api.handle = dlopen("libmonstro.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!g_api.handle) {
        mobius_error(state, dlerror() ? dlerror() : "failed to load libmonstro.so");
        return false;
    }

    std::string error;
    bool ok =
        load_symbol(g_api.handle, "monstro_context_create", g_api.context_create, error) &&
        load_symbol(g_api.handle, "monstro_context_destroy", g_api.context_destroy, error) &&
        load_symbol(g_api.handle, "monstro_context_get_draw_buffer", g_api.context_get_draw_buffer, error) &&
        load_symbol(g_api.handle, "monstro_context_get_draw_commands", g_api.context_get_draw_commands, error) &&
        load_symbol(g_api.handle, "monstro_draw_buffer_get_vertex_buffer", g_api.draw_buffer_get_vertex_buffer, error) &&
        load_symbol(g_api.handle, "monstro_draw_buffer_get_index_buffer", g_api.draw_buffer_get_index_buffer, error) &&
        load_symbol(g_api.handle, "monstro_set_display_size", g_api.set_display_size, error) &&
        load_symbol(g_api.handle, "monstro_get_display_size", g_api.get_display_size, error) &&
        load_symbol(g_api.handle, "monstro_begin_frame", g_api.begin_frame, error) &&
        load_symbol(g_api.handle, "monstro_end_frame", g_api.end_frame, error) &&
        load_symbol(g_api.handle, "monstro_get_delta_time", g_api.get_delta_time, error) &&
        load_symbol(g_api.handle, "monstro_get_time", g_api.get_time, error) &&
        load_symbol(g_api.handle, "monstro_push_id", g_api.push_id, error) &&
        load_symbol(g_api.handle, "monstro_pop_id", g_api.pop_id, error) &&
        load_symbol(g_api.handle, "monstro_get_id", g_api.get_id, error) &&
        load_symbol(g_api.handle, "monstro_set_mouse_pos", g_api.set_mouse_pos, error) &&
        load_symbol(g_api.handle, "monstro_set_mouse_button", g_api.set_mouse_button, error) &&
        load_symbol(g_api.handle, "monstro_set_mouse_wheel", g_api.set_mouse_wheel, error) &&
        load_symbol(g_api.handle, "monstro_set_key", g_api.set_key, error) &&
        load_symbol(g_api.handle, "monstro_set_key_mods", g_api.set_key_mods, error) &&
        load_symbol(g_api.handle, "monstro_add_input_text", g_api.add_input_text, error) &&
        load_symbol(g_api.handle, "monstro_get_mouse_pos", g_api.get_mouse_pos, error) &&
        load_symbol(g_api.handle, "monstro_want_capture_mouse", g_api.want_capture_mouse, error) &&
        load_symbol(g_api.handle, "monstro_want_capture_keyboard", g_api.want_capture_keyboard, error) &&
        load_symbol(g_api.handle, "monstro_want_text_input", g_api.want_text_input, error) &&
        load_symbol(g_api.handle, "monstro_set_next_window_pos", g_api.set_next_window_pos, error) &&
        load_symbol(g_api.handle, "monstro_set_next_window_size", g_api.set_next_window_size, error) &&
        load_symbol(g_api.handle, "monstro_begin_window", g_api.begin_window, error) &&
        load_symbol(g_api.handle, "monstro_end_window", g_api.end_window, error) &&
        load_symbol(g_api.handle, "monstro_get_window_rect", g_api.get_window_rect, error) &&
        load_symbol(g_api.handle, "monstro_get_window_size", g_api.get_window_size, error) &&
        load_symbol(g_api.handle, "monstro_begin_child", g_api.begin_child, error) &&
        load_symbol(g_api.handle, "monstro_end_child", g_api.end_child, error) &&
        load_symbol(g_api.handle, "monstro_button", g_api.button, error) &&
        load_symbol(g_api.handle, "monstro_label", g_api.label, error) &&
        load_symbol(g_api.handle, "monstro_checkbox", g_api.checkbox, error) &&
        load_symbol(g_api.handle, "monstro_slider_float", g_api.slider_float, error) &&
        load_symbol(g_api.handle, "monstro_slider_int", g_api.slider_int, error) &&
        load_symbol(g_api.handle, "monstro_progress_bar", g_api.progress_bar, error);

    if (!ok) {
        dlclose(g_api.handle);
        g_api = MonstroApi{};
        mobius_error(state, error.c_str());
        return false;
    }

    return true;
}

static void unload_api() {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (g_api.handle) {
        dlclose(g_api.handle);
    }
    g_api = MonstroApi{};
}

static void push_float_field(MobiusState* state, int table_idx, const char* key, double value) {
    mobius_stack_pushFloat64(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_bool_field(MobiusState* state, int table_idx, const char* key, bool value) {
    mobius_stack_pushBool(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_uint64_field(MobiusState* state, int table_idx, const char* key, uint64_t value) {
    mobius_stack_pushUInt64(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_int_field(MobiusState* state, int table_idx, const char* key, int64_t value) {
    mobius_stack_pushInt64(state, value);
    mobius_stack_setTableField(state, table_idx, key);
}

static void push_vec2_table(MobiusState* state, MonstroVec2 value) {
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_float_field(state, tbl, "x", value.x);
    push_float_field(state, tbl, "y", value.y);
}

static void push_rect_table(MobiusState* state, MonstroRect value) {
    mobius_stack_pushNewTable(state, 4);
    int tbl = mobius_stack_size(state) - 1;
    push_float_field(state, tbl, "x", value.x);
    push_float_field(state, tbl, "y", value.y);
    push_float_field(state, tbl, "w", value.w);
    push_float_field(state, tbl, "h", value.h);
}

static int return_self(MobiusState* state, int arg_count) {
    mobius_stack_copy(state, 0);
    mobius_stack_pop(state, arg_count);
    return 1;
}

static ContextObject* get_context_object(MobiusState* state, int idx, const char* context) {
    const char* type_name = nullptr;
    void* ptr = mobius_stack_getUserdata(state, idx, &type_name);
    if (!ptr || !type_name || strcmp(type_name, MONSTRO_CONTEXT_TYPE) != 0) {
        mobius_error(state, context);
        return nullptr;
    }
    return static_cast<ContextObject*>(ptr);
}

static int ensure_context_open(MobiusState* state, ContextObject* ctx_obj, const char* context) {
    if (!ctx_obj || ctx_obj->closed || !ctx_obj->handle) {
        return mobius_error(state, context);
    }
    return 0;
}

static void context_object_destructor(void* ptr) {
    ContextObject* ctx_obj = static_cast<ContextObject*>(ptr);
    if (!ctx_obj) return;
    if (g_api.handle && !ctx_obj->closed && ctx_obj->handle) {
        g_api.context_destroy(ctx_obj->handle);
        ctx_obj->handle = nullptr;
        ctx_obj->closed = true;
    }
    delete ctx_obj;
}

static int monstro_create_context(MobiusState* state, int arg_count) {
    if (arg_count != 0) return mobius_error(state, "create_context() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;

    MonstroContext* handle = g_api.context_create();
    if (!handle) return mobius_error(state, "monstro_context_create() failed");

    ContextObject* ctx_obj = new (std::nothrow) ContextObject();
    if (!ctx_obj) {
        g_api.context_destroy(handle);
        return mobius_error(state, "failed to allocate monstro context userdata");
    }
    ctx_obj->handle = handle;
    mobius_stack_pushUserdata(state, ctx_obj, context_object_destructor,
                              MONSTRO_CONTEXT_TYPE, sizeof(ContextObject));
    return 1;
}

static int context_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:close() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:close() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (!ctx_obj->closed && ctx_obj->handle) {
        g_api.context_destroy(ctx_obj->handle);
        ctx_obj->handle = nullptr;
        ctx_obj->closed = true;
    }
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, true);
    return 1;
}

static int context_is_closed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_closed() expects no arguments");
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_closed() self is not a monstro context");
    if (!ctx_obj) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, ctx_obj->closed || !ctx_obj->handle);
    return 1;
}

static int context_set_display_size(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_display_size() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_display_size() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    float width = (float)mobius_stack_asFloat64(state, 1);
    float height = (float)mobius_stack_asFloat64(state, 2);
    g_api.set_display_size(ctx_obj->handle, width, height);
    return return_self(state, arg_count);
}

static int context_get_display_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:get_display_size() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:get_display_size() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    float width = 0.0f;
    float height = 0.0f;
    g_api.get_display_size(ctx_obj->handle, &width, &height);
    mobius_stack_pop(state, 1);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_float_field(state, tbl, "width", width);
    push_float_field(state, tbl, "height", height);
    return 1;
}

static int context_begin_frame(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:begin_frame() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:begin_frame() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    float delta_time = (float)mobius_stack_asFloat64(state, 1);
    g_api.begin_frame(ctx_obj->handle, delta_time);
    return return_self(state, arg_count);
}

static int context_end_frame(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:end_frame() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:end_frame() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.end_frame(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_delta_time(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:delta_time() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:delta_time() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_delta_time(ctx_obj->handle));
    return 1;
}

static int context_time(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:time() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:time() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_time(ctx_obj->handle));
    return 1;
}

static int context_push_id(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:push_id() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:push_id() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:push_id() name must be a string");
    g_api.push_id(ctx_obj->handle, mobius_stack_asString(state, 1));
    return return_self(state, arg_count);
}

static int context_pop_id(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:pop_id() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:pop_id() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.pop_id(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_get_id(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:get_id() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:get_id() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:get_id() name must be a string");
    uint64_t id = g_api.get_id(ctx_obj->handle, mobius_stack_asString(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushUInt64(state, id);
    return 1;
}

static int context_set_mouse_pos(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_mouse_pos() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_mouse_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_mouse_pos(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1), (float)mobius_stack_asFloat64(state, 2));
    return return_self(state, arg_count);
}

static int context_set_mouse_button(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_mouse_button() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_mouse_button() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    int button = (int)mobius_stack_asInt64(state, 1);
    bool down = mobius_stack_asBool(state, 2);
    g_api.set_mouse_button(ctx_obj->handle, (MonstroMouseButton)button, down);
    return return_self(state, arg_count);
}

static int context_set_mouse_wheel(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_mouse_wheel() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_mouse_wheel() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_mouse_wheel(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1), (float)mobius_stack_asFloat64(state, 2));
    return return_self(state, arg_count);
}

static int context_set_key(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_key() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_key() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_key(ctx_obj->handle, (MonstroKey)mobius_stack_asInt64(state, 1), mobius_stack_asBool(state, 2));
    return return_self(state, arg_count);
}

static int context_set_key_mods(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_key_mods() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_key_mods() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_key_mods(ctx_obj->handle, (MonstroModFlags)mobius_stack_asInt64(state, 1));
    return return_self(state, arg_count);
}

static int context_add_input_text(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:add_input_text() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:add_input_text() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:add_input_text() text must be a string");
    g_api.add_input_text(ctx_obj->handle, mobius_stack_asString(state, 1));
    return return_self(state, arg_count);
}

static int context_get_mouse_pos(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:get_mouse_pos() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:get_mouse_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 value = g_api.get_mouse_pos(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, value);
    return 1;
}

static int context_want_capture_mouse(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:want_capture_mouse() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:want_capture_mouse() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.want_capture_mouse(ctx_obj->handle));
    return 1;
}

static int context_want_capture_keyboard(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:want_capture_keyboard() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:want_capture_keyboard() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.want_capture_keyboard(ctx_obj->handle));
    return 1;
}

static int context_want_text_input(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:want_text_input() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:want_text_input() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.want_text_input(ctx_obj->handle));
    return 1;
}

static int context_set_next_window_pos(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "context:set_next_window_pos() expects 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_next_window_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_next_window_pos(ctx_obj->handle,
                              (float)mobius_stack_asFloat64(state, 1),
                              (float)mobius_stack_asFloat64(state, 2),
                              (MonstroCondition)mobius_stack_asInt64(state, 3));
    return return_self(state, arg_count);
}

static int context_set_next_window_size(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "context:set_next_window_size() expects 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_next_window_size() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_next_window_size(ctx_obj->handle,
                               (float)mobius_stack_asFloat64(state, 1),
                               (float)mobius_stack_asFloat64(state, 2),
                               (MonstroCondition)mobius_stack_asInt64(state, 3));
    return return_self(state, arg_count);
}

static int context_begin_window(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 4) return mobius_error(state, "context:begin_window() expects 1 to 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:begin_window() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:begin_window() name must be a string");

    bool open_value = true;
    bool* open_ptr = nullptr;
    MonstroWindowFlags flags = MONSTRO_WINDOW_DEFAULT;

    if (arg_count == 3) {
        if (mobius_stack_isBool(state, 2)) {
            open_value = mobius_stack_asBool(state, 2);
            open_ptr = &open_value;
        } else {
            flags = (MonstroWindowFlags)mobius_stack_asInt64(state, 2);
        }
    } else if (arg_count == 4) {
        if (!mobius_stack_isNil(state, 2)) {
            open_value = mobius_stack_asBool(state, 2);
            open_ptr = &open_value;
        }
        flags = (MonstroWindowFlags)mobius_stack_asInt64(state, 3);
    }

    bool shown = g_api.begin_window(ctx_obj->handle, mobius_stack_asString(state, 1), open_ptr, flags);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "shown", shown);
    if (open_ptr) push_bool_field(state, tbl, "open", open_value);
    return 1;
}

static int context_end_window(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:end_window() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:end_window() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.end_window(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_window_rect(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:window_rect() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:window_rect() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroRect rect = g_api.get_window_rect(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_rect_table(state, rect);
    return 1;
}

static int context_window_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:window_size() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:window_size() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 size = g_api.get_window_size(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, size);
    return 1;
}

static int context_begin_child(MobiusState* state, int arg_count) {
    if (arg_count < 4 || arg_count > 5) return mobius_error(state, "context:begin_child() expects 3 or 4 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:begin_child() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:begin_child() id must be a string");
    MonstroWindowFlags flags = (arg_count == 5) ? (MonstroWindowFlags)mobius_stack_asInt64(state, 4) : MONSTRO_WINDOW_NONE;
    bool shown = g_api.begin_child(ctx_obj->handle, mobius_stack_asString(state, 1),
                                   (float)mobius_stack_asFloat64(state, 2),
                                   (float)mobius_stack_asFloat64(state, 3), flags);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, shown);
    return 1;
}

static int context_end_child(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:end_child() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:end_child() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.end_child(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_button(MobiusState* state, int arg_count) {
    if (arg_count != 2 && arg_count != 3) return mobius_error(state, "context:button() expects 1 or 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:button() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:button() label must be a string");
    const char* tooltip = (arg_count == 3 && !mobius_stack_isNil(state, 2)) ? mobius_stack_asString(state, 2) : nullptr;
    bool pressed = g_api.button(ctx_obj->handle, mobius_stack_asString(state, 1), tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, pressed);
    return 1;
}

static int context_label(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:label() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:label() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:label() text must be a string");
    g_api.label(ctx_obj->handle, mobius_stack_asString(state, 1));
    return return_self(state, arg_count);
}

static int context_checkbox(MobiusState* state, int arg_count) {
    if (arg_count != 3 && arg_count != 4) return mobius_error(state, "context:checkbox() expects 2 or 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:checkbox() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:checkbox() label must be a string");
    bool value = mobius_stack_asBool(state, 2);
    const char* tooltip = (arg_count == 4 && !mobius_stack_isNil(state, 3)) ? mobius_stack_asString(state, 3) : nullptr;
    bool changed = g_api.checkbox(ctx_obj->handle, mobius_stack_asString(state, 1), &value, tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_bool_field(state, tbl, "value", value);
    return 1;
}

static int context_slider_float(MobiusState* state, int arg_count) {
    if (arg_count < 5 || arg_count > 7) return mobius_error(state, "context:slider_float() expects 4 to 6 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:slider_float() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:slider_float() label must be a string");
    float value = (float)mobius_stack_asFloat64(state, 2);
    float min = (float)mobius_stack_asFloat64(state, 3);
    float max = (float)mobius_stack_asFloat64(state, 4);
    const char* format = (arg_count >= 6 && !mobius_stack_isNil(state, 5)) ? mobius_stack_asString(state, 5) : "%.3f";
    const char* tooltip = (arg_count == 7 && !mobius_stack_isNil(state, 6)) ? mobius_stack_asString(state, 6) : nullptr;
    bool changed = g_api.slider_float(ctx_obj->handle, mobius_stack_asString(state, 1), &value, min, max, format, tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_float_field(state, tbl, "value", value);
    return 1;
}

static int context_slider_int(MobiusState* state, int arg_count) {
    if (arg_count < 5 || arg_count > 7) return mobius_error(state, "context:slider_int() expects 4 to 6 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:slider_int() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:slider_int() label must be a string");
    int value = (int)mobius_stack_asInt64(state, 2);
    int min = (int)mobius_stack_asInt64(state, 3);
    int max = (int)mobius_stack_asInt64(state, 4);
    const char* format = (arg_count >= 6 && !mobius_stack_isNil(state, 5)) ? mobius_stack_asString(state, 5) : "%d";
    const char* tooltip = (arg_count == 7 && !mobius_stack_isNil(state, 6)) ? mobius_stack_asString(state, 6) : nullptr;
    bool changed = g_api.slider_int(ctx_obj->handle, mobius_stack_asString(state, 1), &value, min, max, format, tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_int_field(state, tbl, "value", value);
    return 1;
}

static int context_progress_bar(MobiusState* state, int arg_count) {
    if (arg_count != 4 && arg_count != 5) return mobius_error(state, "context:progress_bar() expects 3 or 4 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:progress_bar() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    const char* overlay = (arg_count == 5 && !mobius_stack_isNil(state, 4)) ? mobius_stack_asString(state, 4) : nullptr;
    g_api.progress_bar(ctx_obj->handle,
                       (float)mobius_stack_asFloat64(state, 1),
                       (float)mobius_stack_asFloat64(state, 2),
                       (float)mobius_stack_asFloat64(state, 3),
                       overlay);
    return return_self(state, arg_count);
}

static int context_draw_data(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:draw_data() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:draw_data() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;

    MonstroDrawBuffer* draw_buffer = g_api.context_get_draw_buffer(ctx_obj->handle);
    const MonstroVertex* vertex_data = nullptr;
    size_t vertex_bytes = 0;
    const uint32_t* index_data = nullptr;
    uint32_t index_count = 0;
    const MonstroDrawCommand* commands = nullptr;
    uint32_t command_count = 0;

    if (draw_buffer) {
        g_api.draw_buffer_get_vertex_buffer(draw_buffer, &vertex_data, &vertex_bytes);
        g_api.draw_buffer_get_index_buffer(draw_buffer, &index_data, &index_count);
    }
    g_api.context_get_draw_commands(ctx_obj->handle, &commands, &command_count);

    mobius_stack_pop(state, 1);
    mobius_stack_pushNewTable(state, 6);
    int tbl = mobius_stack_size(state) - 1;

    mobius_stack_pushBufferCopy(state, vertex_data, vertex_bytes);
    mobius_stack_setTableField(state, tbl, "vertex_buffer");
    push_int_field(state, tbl, "vertex_stride", (int64_t)sizeof(MonstroVertex));
    push_int_field(state, tbl, "vertex_count", (int64_t)(vertex_bytes / sizeof(MonstroVertex)));

    mobius_stack_pushBufferCopy(state, index_data, (size_t)index_count * sizeof(uint32_t));
    mobius_stack_setTableField(state, tbl, "index_buffer");
    push_int_field(state, tbl, "index_count", index_count);

    mobius_stack_pushNewArray(state, command_count);
    int arr = mobius_stack_size(state) - 1;
    for (uint32_t i = 0; i < command_count; i++) {
        mobius_stack_pushNewTable(state, 7);
        int cmd_tbl = mobius_stack_size(state) - 1;
        mobius_stack_pushNewTable(state, 4);
        int clip_tbl = mobius_stack_size(state) - 1;
        push_float_field(state, clip_tbl, "x1", commands[i].clip_rect[0]);
        push_float_field(state, clip_tbl, "y1", commands[i].clip_rect[1]);
        push_float_field(state, clip_tbl, "x2", commands[i].clip_rect[2]);
        push_float_field(state, clip_tbl, "y2", commands[i].clip_rect[3]);
        mobius_stack_setTableField(state, cmd_tbl, "clip_rect");
        push_int_field(state, cmd_tbl, "layer", commands[i].layer);
        push_uint64_field(state, cmd_tbl, "order", commands[i].order);
        push_uint64_field(state, cmd_tbl, "texture_id", commands[i].texture_id);
        push_int_field(state, cmd_tbl, "index_offset", commands[i].index_offset);
        push_int_field(state, cmd_tbl, "index_count", commands[i].index_count);
        push_bool_field(state, cmd_tbl, "has_custom_draw", commands[i].custom_draw != nullptr);
        mobius_stack_arrayPush(state, arr);
    }
    mobius_stack_setTableField(state, tbl, "commands");

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

static const NamedIntConstant MONSTRO_INT_CONSTANTS[] = {
    {"WINDOW_NONE", MONSTRO_WINDOW_NONE},
    {"WINDOW_BACKGROUND", MONSTRO_WINDOW_BACKGROUND},
    {"WINDOW_TITLE_BAR", MONSTRO_WINDOW_TITLE_BAR},
    {"WINDOW_MENU_BAR", MONSTRO_WINDOW_MENU_BAR},
    {"WINDOW_BORDERS", MONSTRO_WINDOW_BORDERS},
    {"WINDOW_SCROLLBARS", MONSTRO_WINDOW_SCROLLBARS},
    {"WINDOW_MOVABLE", MONSTRO_WINDOW_MOVABLE},
    {"WINDOW_COLLAPSABLE", MONSTRO_WINDOW_COLLAPSABLE},
    {"WINDOW_RESIZABLE", MONSTRO_WINDOW_RESIZABLE},
    {"WINDOW_INPUTS", MONSTRO_WINDOW_INPUTS},
    {"WINDOW_AUTO_RESIZE", MONSTRO_WINDOW_AUTO_RESIZE},
    {"WINDOW_ROOT", MONSTRO_WINDOW_ROOT},
    {"WINDOW_CHILD", MONSTRO_WINDOW_CHILD},
    {"WINDOW_POPUP", MONSTRO_WINDOW_POPUP},
    {"WINDOW_MODAL", MONSTRO_WINDOW_MODAL},
    {"WINDOW_DEFAULT", MONSTRO_WINDOW_DEFAULT},
    {"WINDOW_ROOT_DEFAULT", MONSTRO_WINDOW_ROOT_DEFAULT},
    {"COND_NONE", MONSTRO_COND_NONE},
    {"COND_ALWAYS", MONSTRO_COND_ALWAYS},
    {"COND_ONCE", MONSTRO_COND_ONCE},
    {"COND_FIRST_USE_EVER", MONSTRO_COND_FIRST_USE_EVER},
    {"COND_APPEARING", MONSTRO_COND_APPEARING},
    {"LAYOUT_HORIZONTAL", MONSTRO_LAYOUT_HORIZONTAL},
    {"LAYOUT_VERTICAL", MONSTRO_LAYOUT_VERTICAL},
    {"MOUSE_BUTTON_LEFT", MONSTRO_MOUSE_BUTTON_LEFT},
    {"MOUSE_BUTTON_RIGHT", MONSTRO_MOUSE_BUTTON_RIGHT},
    {"MOUSE_BUTTON_MIDDLE", MONSTRO_MOUSE_BUTTON_MIDDLE},
    {"MOD_NONE", MONSTRO_MOD_NONE},
    {"MOD_CTRL", MONSTRO_MOD_CTRL},
    {"MOD_SHIFT", MONSTRO_MOD_SHIFT},
    {"MOD_ALT", MONSTRO_MOD_ALT},
    {"MOD_SUPER", MONSTRO_MOD_SUPER},
};

static int monstro_post_init(MobiusState* state) {
    const int module_idx = 0;

    mobius_stack_pushNewTable(state, 24);
    int context_proto = mobius_stack_size(state) - 1;
    copy_module_function(state, module_idx, "__context_close", context_proto, "close");
    copy_module_function(state, module_idx, "__context_close", context_proto, "destroy");
    copy_module_function(state, module_idx, "__context_is_closed", context_proto, "is_closed");
    copy_module_function(state, module_idx, "__context_set_display_size", context_proto, "set_display_size");
    copy_module_function(state, module_idx, "__context_get_display_size", context_proto, "get_display_size");
    copy_module_function(state, module_idx, "__context_begin_frame", context_proto, "begin_frame");
    copy_module_function(state, module_idx, "__context_end_frame", context_proto, "end_frame");
    copy_module_function(state, module_idx, "__context_delta_time", context_proto, "delta_time");
    copy_module_function(state, module_idx, "__context_time", context_proto, "time");
    copy_module_function(state, module_idx, "__context_push_id", context_proto, "push_id");
    copy_module_function(state, module_idx, "__context_pop_id", context_proto, "pop_id");
    copy_module_function(state, module_idx, "__context_get_id", context_proto, "get_id");
    copy_module_function(state, module_idx, "__context_set_mouse_pos", context_proto, "set_mouse_pos");
    copy_module_function(state, module_idx, "__context_set_mouse_button", context_proto, "set_mouse_button");
    copy_module_function(state, module_idx, "__context_set_mouse_wheel", context_proto, "set_mouse_wheel");
    copy_module_function(state, module_idx, "__context_set_key", context_proto, "set_key");
    copy_module_function(state, module_idx, "__context_set_key_mods", context_proto, "set_key_mods");
    copy_module_function(state, module_idx, "__context_add_input_text", context_proto, "add_input_text");
    copy_module_function(state, module_idx, "__context_get_mouse_pos", context_proto, "get_mouse_pos");
    copy_module_function(state, module_idx, "__context_want_capture_mouse", context_proto, "want_capture_mouse");
    copy_module_function(state, module_idx, "__context_want_capture_keyboard", context_proto, "want_capture_keyboard");
    copy_module_function(state, module_idx, "__context_want_text_input", context_proto, "want_text_input");
    copy_module_function(state, module_idx, "__context_set_next_window_pos", context_proto, "set_next_window_pos");
    copy_module_function(state, module_idx, "__context_set_next_window_size", context_proto, "set_next_window_size");
    copy_module_function(state, module_idx, "__context_begin_window", context_proto, "begin_window");
    copy_module_function(state, module_idx, "__context_end_window", context_proto, "end_window");
    copy_module_function(state, module_idx, "__context_window_rect", context_proto, "window_rect");
    copy_module_function(state, module_idx, "__context_window_size", context_proto, "window_size");
    copy_module_function(state, module_idx, "__context_begin_child", context_proto, "begin_child");
    copy_module_function(state, module_idx, "__context_end_child", context_proto, "end_child");
    copy_module_function(state, module_idx, "__context_button", context_proto, "button");
    copy_module_function(state, module_idx, "__context_label", context_proto, "label");
    copy_module_function(state, module_idx, "__context_checkbox", context_proto, "checkbox");
    copy_module_function(state, module_idx, "__context_slider_float", context_proto, "slider_float");
    copy_module_function(state, module_idx, "__context_slider_int", context_proto, "slider_int");
    copy_module_function(state, module_idx, "__context_progress_bar", context_proto, "progress_bar");
    copy_module_function(state, module_idx, "__context_draw_data", context_proto, "draw_data");
    mobius_set_userdata_type_metatable(state, MONSTRO_CONTEXT_TYPE);

    const char* hidden_keys[] = {
        "__context_close", "__context_is_closed", "__context_set_display_size",
        "__context_get_display_size", "__context_begin_frame", "__context_end_frame",
        "__context_delta_time", "__context_time", "__context_push_id",
        "__context_pop_id", "__context_get_id", "__context_set_mouse_pos",
        "__context_set_mouse_button", "__context_set_mouse_wheel", "__context_set_key",
        "__context_set_key_mods", "__context_add_input_text", "__context_get_mouse_pos",
        "__context_want_capture_mouse", "__context_want_capture_keyboard",
        "__context_want_text_input", "__context_set_next_window_pos",
        "__context_set_next_window_size", "__context_begin_window",
        "__context_end_window", "__context_window_rect", "__context_window_size",
        "__context_begin_child", "__context_end_child", "__context_button",
        "__context_label", "__context_checkbox", "__context_slider_float",
        "__context_slider_int", "__context_progress_bar", "__context_draw_data"
    };
    for (const char* key : hidden_keys) {
        mobius_stack_pushNil(state);
        mobius_stack_setTableField(state, module_idx, key);
    }

    for (const NamedIntConstant& constant : MONSTRO_INT_CONSTANTS) {
        mobius_stack_pushInt64(state, constant.value);
        mobius_stack_setTableField(state, module_idx, constant.name);
    }

    return 0;
}

static void cleanup_monstro_plugin(void) {
    unload_api();
}

static MobiusPluginFunction monstro_functions[] = {
    {"create_context", monstro_create_context, 0, MOBIUS_VAL_USERDATA, "Create a Monstro UI context"},
    {"__context_close", context_close, 1, MOBIUS_VAL_BOOL, "Internal context close method"},
    {"__context_is_closed", context_is_closed, 1, MOBIUS_VAL_BOOL, "Internal context closed-state method"},
    {"__context_set_display_size", context_set_display_size, 3, MOBIUS_VAL_USERDATA, "Internal set_display_size method"},
    {"__context_get_display_size", context_get_display_size, 1, MOBIUS_VAL_TABLE, "Internal get_display_size method"},
    {"__context_begin_frame", context_begin_frame, 2, MOBIUS_VAL_USERDATA, "Internal begin_frame method"},
    {"__context_end_frame", context_end_frame, 1, MOBIUS_VAL_USERDATA, "Internal end_frame method"},
    {"__context_delta_time", context_delta_time, 1, MOBIUS_VAL_FLOAT64, "Internal delta_time method"},
    {"__context_time", context_time, 1, MOBIUS_VAL_FLOAT64, "Internal time method"},
    {"__context_push_id", context_push_id, 2, MOBIUS_VAL_USERDATA, "Internal push_id method"},
    {"__context_pop_id", context_pop_id, 1, MOBIUS_VAL_USERDATA, "Internal pop_id method"},
    {"__context_get_id", context_get_id, 2, MOBIUS_VAL_UINT64, "Internal get_id method"},
    {"__context_set_mouse_pos", context_set_mouse_pos, 3, MOBIUS_VAL_USERDATA, "Internal set_mouse_pos method"},
    {"__context_set_mouse_button", context_set_mouse_button, 3, MOBIUS_VAL_USERDATA, "Internal set_mouse_button method"},
    {"__context_set_mouse_wheel", context_set_mouse_wheel, 3, MOBIUS_VAL_USERDATA, "Internal set_mouse_wheel method"},
    {"__context_set_key", context_set_key, 3, MOBIUS_VAL_USERDATA, "Internal set_key method"},
    {"__context_set_key_mods", context_set_key_mods, 2, MOBIUS_VAL_USERDATA, "Internal set_key_mods method"},
    {"__context_add_input_text", context_add_input_text, 2, MOBIUS_VAL_USERDATA, "Internal add_input_text method"},
    {"__context_get_mouse_pos", context_get_mouse_pos, 1, MOBIUS_VAL_TABLE, "Internal get_mouse_pos method"},
    {"__context_want_capture_mouse", context_want_capture_mouse, 1, MOBIUS_VAL_BOOL, "Internal want_capture_mouse method"},
    {"__context_want_capture_keyboard", context_want_capture_keyboard, 1, MOBIUS_VAL_BOOL, "Internal want_capture_keyboard method"},
    {"__context_want_text_input", context_want_text_input, 1, MOBIUS_VAL_BOOL, "Internal want_text_input method"},
    {"__context_set_next_window_pos", context_set_next_window_pos, 4, MOBIUS_VAL_USERDATA, "Internal set_next_window_pos method"},
    {"__context_set_next_window_size", context_set_next_window_size, 4, MOBIUS_VAL_USERDATA, "Internal set_next_window_size method"},
    {"__context_begin_window", context_begin_window, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal begin_window method"},
    {"__context_end_window", context_end_window, 1, MOBIUS_VAL_USERDATA, "Internal end_window method"},
    {"__context_window_rect", context_window_rect, 1, MOBIUS_VAL_TABLE, "Internal window_rect method"},
    {"__context_window_size", context_window_size, 1, MOBIUS_VAL_TABLE, "Internal window_size method"},
    {"__context_begin_child", context_begin_child, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal begin_child method"},
    {"__context_end_child", context_end_child, 1, MOBIUS_VAL_USERDATA, "Internal end_child method"},
    {"__context_button", context_button, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal button method"},
    {"__context_label", context_label, 2, MOBIUS_VAL_USERDATA, "Internal label method"},
    {"__context_checkbox", context_checkbox, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal checkbox method"},
    {"__context_slider_float", context_slider_float, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal slider_float method"},
    {"__context_slider_int", context_slider_int, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal slider_int method"},
    {"__context_progress_bar", context_progress_bar, SIZE_MAX, MOBIUS_VAL_USERDATA, "Internal progress_bar method"},
    {"__context_draw_data", context_draw_data, 1, MOBIUS_VAL_TABLE, "Internal draw_data method"},
};

static MobiusPlugin monstro_plugin = {
    .metadata = {
        .name = "monstro",
        .version = "0.1.0",
        .description = "Monstro UI context and widget bindings for Mobius",
        .author = "Mobius Team",
        .api_version = MOBIUS_PLUGIN_API_VERSION,
        .license = "MIT"
    },
    .functions = monstro_functions,
    .function_count = sizeof(monstro_functions) / sizeof(monstro_functions[0]),
    .init_plugin = nullptr,
    .cleanup_plugin = cleanup_monstro_plugin,
    .post_init = monstro_post_init,
};

} // namespace

extern "C" MOBIUS_PLUGIN_EXPORT MobiusPlugin* mobius_plugin_info(void) {
    return &monstro_plugin;
}
