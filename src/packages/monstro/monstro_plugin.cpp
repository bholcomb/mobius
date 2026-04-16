#include <mobius/mobius_plugin.h>

#include <monstro.h>

#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace {

static const char* MONSTRO_CONTEXT_TYPE = "monstro_context";

struct ContextObject {
    MonstroContext* handle = nullptr;
    bool closed = false;
};

struct MonstroApi {
    MonstroContext* (*context_create)(void) = monstro_context_create;
    void (*context_destroy)(MonstroContext* ctx) = monstro_context_destroy;
    MonstroDrawBuffer* (*context_get_draw_buffer)(MonstroContext* ctx) = monstro_context_get_draw_buffer;
    void (*context_get_draw_commands)(MonstroContext* ctx, const MonstroDrawCommand** out_commands, uint32_t* out_count) = monstro_context_get_draw_commands;
    void (*draw_buffer_get_vertex_buffer)(MonstroDrawBuffer* buf, const MonstroVertex** out_data, size_t* out_byte_size) = monstro_draw_buffer_get_vertex_buffer;
    void (*draw_buffer_get_index_buffer)(MonstroDrawBuffer* buf, const uint32_t** out_data, uint32_t* out_count) = monstro_draw_buffer_get_index_buffer;

    void (*set_display_size)(MonstroContext* ctx, float width, float height) = monstro_set_display_size;
    void (*get_display_size)(MonstroContext* ctx, float* out_width, float* out_height) = monstro_get_display_size;
    void (*begin_frame)(MonstroContext* ctx, float delta_time) = monstro_begin_frame;
    void (*end_frame)(MonstroContext* ctx) = monstro_end_frame;
    float (*get_delta_time)(MonstroContext* ctx) = monstro_get_delta_time;
    float (*get_time)(MonstroContext* ctx) = monstro_get_time;
    void (*push_id)(MonstroContext* ctx, const char* name) = monstro_push_id;
    void (*pop_id)(MonstroContext* ctx) = monstro_pop_id;
    uint64_t (*get_id)(MonstroContext* ctx, const char* name) = monstro_get_id;

    void (*set_mouse_pos)(MonstroContext* ctx, float x, float y) = monstro_set_mouse_pos;
    void (*set_mouse_button)(MonstroContext* ctx, MonstroMouseButton button, bool down) = monstro_set_mouse_button;
    void (*set_mouse_wheel)(MonstroContext* ctx, float wheel_x, float wheel_y) = monstro_set_mouse_wheel;
    void (*set_double_click_distance)(MonstroContext* ctx, float distance) = monstro_set_double_click_distance;
    void (*set_mouse_drag_threshold)(MonstroContext* ctx, float threshold) = monstro_set_mouse_drag_threshold;
    void (*set_key_repeat_delay)(MonstroContext* ctx, float delay) = monstro_set_key_repeat_delay;
    void (*set_key_repeat_rate)(MonstroContext* ctx, float rate) = monstro_set_key_repeat_rate;
    float (*get_double_click_distance)(MonstroContext* ctx) = monstro_get_double_click_distance;
    float (*get_mouse_drag_threshold)(MonstroContext* ctx) = monstro_get_mouse_drag_threshold;
    float (*get_key_repeat_delay)(MonstroContext* ctx) = monstro_get_key_repeat_delay;
    float (*get_key_repeat_rate)(MonstroContext* ctx) = monstro_get_key_repeat_rate;
    void (*set_key)(MonstroContext* ctx, MonstroKey key, bool down) = monstro_set_key;
    void (*set_key_mods)(MonstroContext* ctx, MonstroModFlags mods) = monstro_set_key_mods;
    void (*add_input_text)(MonstroContext* ctx, const char* utf8_text) = monstro_add_input_text;
    MonstroVec2 (*get_mouse_pos)(MonstroContext* ctx) = monstro_get_mouse_pos;
    MonstroVec2 (*get_mouse_delta)(MonstroContext* ctx) = monstro_get_mouse_delta;
    bool (*is_mouse_down)(MonstroContext* ctx, MonstroMouseButton button) = monstro_is_mouse_down;
    bool (*is_mouse_clicked)(MonstroContext* ctx, MonstroMouseButton button, bool repeat) = monstro_is_mouse_clicked;
    bool (*is_mouse_released)(MonstroContext* ctx, MonstroMouseButton button) = monstro_is_mouse_released;
    bool (*is_mouse_double_clicked)(MonstroContext* ctx, MonstroMouseButton button) = monstro_is_mouse_double_clicked;
    bool (*is_mouse_dragging)(MonstroContext* ctx, MonstroMouseButton button) = monstro_is_mouse_dragging;
    MonstroVec2 (*get_mouse_drag_delta)(MonstroContext* ctx, MonstroMouseButton button) = monstro_get_mouse_drag_delta;
    float (*get_mouse_wheel)(MonstroContext* ctx) = monstro_get_mouse_wheel;
    float (*get_mouse_wheel_h)(MonstroContext* ctx) = monstro_get_mouse_wheel_h;
    bool (*is_key_down)(MonstroContext* ctx, MonstroKey key) = monstro_is_key_down;
    bool (*is_key_pressed)(MonstroContext* ctx, MonstroKey key, bool repeat) = monstro_is_key_pressed;
    bool (*is_key_released)(MonstroContext* ctx, MonstroKey key) = monstro_is_key_released;
    MonstroModFlags (*get_key_mods)(MonstroContext* ctx) = monstro_get_key_mods;
    const char* (*get_input_text)(MonstroContext* ctx) = monstro_get_input_text;
    bool (*want_capture_mouse)(MonstroContext* ctx) = monstro_want_capture_mouse;
    bool (*want_capture_keyboard)(MonstroContext* ctx) = monstro_want_capture_keyboard;
    bool (*want_text_input)(MonstroContext* ctx) = monstro_want_text_input;
    void (*set_mouse_cursor)(MonstroContext* ctx, MonstroMouseCursor cursor) = monstro_set_mouse_cursor;
    MonstroMouseCursor (*get_mouse_cursor)(MonstroContext* ctx) = monstro_get_mouse_cursor;

    void (*set_next_window_pos)(MonstroContext* ctx, float x, float y, MonstroCondition cond) = monstro_set_next_window_pos;
    void (*set_next_window_size)(MonstroContext* ctx, float width, float height, MonstroCondition cond) = monstro_set_next_window_size;
    void (*set_next_window_focus)(MonstroContext* ctx) = monstro_set_next_window_focus;
    bool (*begin_window)(MonstroContext* ctx, const char* name, bool* p_open, MonstroWindowFlags flags) = monstro_begin_window;
    void (*end_window)(MonstroContext* ctx) = monstro_end_window;
    MonstroRect (*get_window_rect)(MonstroContext* ctx) = monstro_get_window_rect;
    MonstroVec2 (*get_window_pos)(MonstroContext* ctx) = monstro_get_window_pos;
    MonstroVec2 (*get_window_size)(MonstroContext* ctx) = monstro_get_window_size;
    void (*set_window_pos)(MonstroContext* ctx, float x, float y, MonstroCondition cond) = monstro_set_window_pos;
    void (*set_window_size)(MonstroContext* ctx, float width, float height, MonstroCondition cond) = monstro_set_window_size;
    void (*set_window_focus)(MonstroContext* ctx) = monstro_set_window_focus;
    void (*set_window_layout)(MonstroContext* ctx, MonstroLayoutDirection direction) = monstro_set_window_layout;
    void (*set_window_title)(MonstroContext* ctx, const char* title) = monstro_set_window_title;
    void (*set_window_pos_named)(MonstroContext* ctx, const char* name, float x, float y, MonstroCondition cond) = monstro_set_window_pos_named;
    void (*set_window_size_named)(MonstroContext* ctx, const char* name, float width, float height, MonstroCondition cond) = monstro_set_window_size_named;
    void (*set_window_focus_named)(MonstroContext* ctx, const char* name) = monstro_set_window_focus_named;
    void (*set_window_layout_named)(MonstroContext* ctx, const char* name, MonstroLayoutDirection direction) = monstro_set_window_layout_named;
    void (*set_window_title_named)(MonstroContext* ctx, const char* name, const char* title) = monstro_set_window_title_named;
    float (*get_scroll_x)(MonstroContext* ctx) = monstro_get_scroll_x;
    float (*get_scroll_y)(MonstroContext* ctx) = monstro_get_scroll_y;
    void (*set_scroll_x)(MonstroContext* ctx, float scroll_x) = monstro_set_scroll_x;
    void (*set_scroll_y)(MonstroContext* ctx, float scroll_y) = monstro_set_scroll_y;
    bool (*is_window_collapsed)(MonstroContext* ctx) = monstro_is_window_collapsed;
    bool (*is_window_focused)(MonstroContext* ctx) = monstro_is_window_focused;
    bool (*is_window_hovered)(MonstroContext* ctx) = monstro_is_window_hovered;
    bool (*begin_child)(MonstroContext* ctx, const char* str_id, float width, float height, MonstroWindowFlags flags) = monstro_begin_child;
    void (*end_child)(MonstroContext* ctx) = monstro_end_child;

    void (*begin_layout)(MonstroContext* ctx, MonstroLayoutDirection direction) = monstro_begin_layout;
    void (*begin_layout_at)(MonstroContext* ctx, float x, float y, MonstroLayoutDirection direction) = monstro_begin_layout_at;
    void (*end_layout)(MonstroContext* ctx) = monstro_end_layout;
    void (*add_item)(MonstroContext* ctx, float width, float height) = monstro_add_item;
    MonstroVec2 (*get_cursor_pos)(MonstroContext* ctx) = monstro_get_cursor_pos;
    void (*set_cursor_pos)(MonstroContext* ctx, float x, float y) = monstro_set_cursor_pos;
    MonstroVec2 (*get_cursor_screen_pos)(MonstroContext* ctx) = monstro_get_cursor_screen_pos;
    void (*set_cursor_screen_pos)(MonstroContext* ctx, float x, float y) = monstro_set_cursor_screen_pos;
    MonstroVec2 (*get_content_region_avail)(MonstroContext* ctx) = monstro_get_content_region_avail;
    void (*indent)(MonstroContext* ctx, float indent_width) = monstro_indent;
    void (*unindent)(MonstroContext* ctx, float indent_width) = monstro_unindent;
    void (*spacer)(MonstroContext* ctx, float size) = monstro_spacer;
    void (*dummy)(MonstroContext* ctx, float width, float height) = monstro_dummy;
    float (*percent)(MonstroContext* ctx, float percent, MonstroLayoutDirection direction) = monstro_percent;
    void (*begin_columns)(MonstroContext* ctx, const char* str_id, int count) = monstro_begin_columns;
    void (*next_column)(MonstroContext* ctx) = monstro_next_column;
    void (*end_columns)(MonstroContext* ctx) = monstro_end_columns;

    bool (*is_item_hovered)(MonstroContext* ctx) = monstro_is_item_hovered;
    bool (*is_item_active)(MonstroContext* ctx) = monstro_is_item_active;
    bool (*is_item_clicked)(MonstroContext* ctx, MonstroMouseButton button) = monstro_is_item_clicked;
    bool (*is_item_edited)(MonstroContext* ctx) = monstro_is_item_edited;
    bool (*is_item_deactivated_after_edit)(MonstroContext* ctx) = monstro_is_item_deactivated_after_edit;
    MonstroRect (*get_item_rect)(MonstroContext* ctx) = monstro_get_item_rect;
    MonstroVec2 (*get_item_rect_min)(MonstroContext* ctx) = monstro_get_item_rect_min;
    MonstroVec2 (*get_item_rect_max)(MonstroContext* ctx) = monstro_get_item_rect_max;
    MonstroVec2 (*get_item_rect_size)(MonstroContext* ctx) = monstro_get_item_rect_size;
    void (*begin_disabled)(MonstroContext* ctx, bool disabled) = monstro_begin_disabled;
    void (*end_disabled)(MonstroContext* ctx) = monstro_end_disabled;
    bool (*is_disabled)(MonstroContext* ctx) = monstro_is_disabled;
    bool (*button)(MonstroContext* ctx, const char* label, const char* tooltip) = monstro_button;
    bool (*button_sized)(MonstroContext* ctx, const char* label, float width, float height, const char* tooltip) = monstro_button_sized;
    void (*label)(MonstroContext* ctx, const char* text) = monstro_label;
    void (*label_sized)(MonstroContext* ctx, const char* text, float width, float height) = monstro_label_sized;
    void (*label_wrapped)(MonstroContext* ctx, const char* text, float wrap_width) = monstro_label_wrapped;
    void (*image)(MonstroContext* ctx, uint64_t texture_id, float width, float height) = monstro_image;
    bool (*checkbox)(MonstroContext* ctx, const char* label, bool* value, const char* tooltip) = monstro_checkbox;
    bool (*radio_button)(MonstroContext* ctx, const char* label, int* value, int button_value, const char* tooltip) = monstro_radio_button;
    bool (*slider_float)(MonstroContext* ctx, const char* label, float* value, float min, float max, const char* format, const char* tooltip) = monstro_slider_float;
    bool (*slider_int)(MonstroContext* ctx, const char* label, int* value, int min, int max, const char* format, const char* tooltip) = monstro_slider_int;
    bool (*slider_float_ex)(MonstroContext* ctx, const char* label, float* value, float min, float max, const char* format, float width, float height, MonstroSliderFlags flags, const char* tooltip) = monstro_slider_float_ex;
    bool (*slider_int_ex)(MonstroContext* ctx, const char* label, int* value, int min, int max, const char* format, float width, float height, MonstroSliderFlags flags, const char* tooltip) = monstro_slider_int_ex;
    bool (*knob)(MonstroContext* ctx, const char* label, float* value, float min, float max, const char* format, const char* tooltip) = monstro_knob;
    bool (*knob_sized)(MonstroContext* ctx, const char* label, float* value, float min, float max, const char* format, float diameter, const char* tooltip) = monstro_knob_sized;
    bool (*input_int)(MonstroContext* ctx, const char* label, int* value, int step, int step_fast, const char* tooltip) = monstro_input_int;
    bool (*input_float)(MonstroContext* ctx, const char* label, float* value, float step, float step_fast, const char* format, const char* tooltip) = monstro_input_float;
    bool (*input_double)(MonstroContext* ctx, const char* label, double* value, double step, double step_fast, const char* format, const char* tooltip) = monstro_input_double;
    void (*progress_bar)(MonstroContext* ctx, float fraction, float width, float height, const char* overlay) = monstro_progress_bar;
    void (*tooltip)(MonstroContext* ctx, const char* text) = monstro_tooltip;
    void (*separator)(MonstroContext* ctx) = monstro_separator;
    void (*separator_text)(MonstroContext* ctx, const char* text) = monstro_separator_text;
};

static const MonstroApi g_api{};

static bool ensure_api_loaded(MobiusState* state) {
    (void)state;
    return true;
}

static void unload_api() {
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

static void push_string_field(MobiusState* state, int table_idx, const char* key, const char* value) {
    mobius_stack_pushString(state, value ? value : "");
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

static const char* get_optional_string(MobiusState* state, int idx) {
    return mobius_stack_isNil(state, idx) ? nullptr : mobius_stack_asString(state, idx);
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
    if (!ctx_obj->closed && ctx_obj->handle) {
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

static int context_set_double_click_distance(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_double_click_distance() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_double_click_distance() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_double_click_distance(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_set_mouse_drag_threshold(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_mouse_drag_threshold() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_mouse_drag_threshold() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_mouse_drag_threshold(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_set_key_repeat_delay(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_key_repeat_delay() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_key_repeat_delay() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_key_repeat_delay(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_set_key_repeat_rate(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_key_repeat_rate() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_key_repeat_rate() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_key_repeat_rate(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_double_click_distance(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:double_click_distance() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:double_click_distance() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_double_click_distance(ctx_obj->handle));
    return 1;
}

static int context_mouse_drag_threshold(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:mouse_drag_threshold() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:mouse_drag_threshold() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_mouse_drag_threshold(ctx_obj->handle));
    return 1;
}

static int context_key_repeat_delay(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:key_repeat_delay() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:key_repeat_delay() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_key_repeat_delay(ctx_obj->handle));
    return 1;
}

static int context_key_repeat_rate(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:key_repeat_rate() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:key_repeat_rate() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_key_repeat_rate(ctx_obj->handle));
    return 1;
}

static int context_mouse_delta(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:mouse_delta() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:mouse_delta() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 value = g_api.get_mouse_delta(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, value);
    return 1;
}

static int context_is_mouse_down(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:is_mouse_down() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_mouse_down() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool down = g_api.is_mouse_down(ctx_obj->handle, (MonstroMouseButton)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, down);
    return 1;
}

static int context_is_mouse_clicked(MobiusState* state, int arg_count) {
    if (arg_count != 2 && arg_count != 3) return mobius_error(state, "context:is_mouse_clicked() expects 1 or 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_mouse_clicked() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool repeat = arg_count == 3 ? mobius_stack_asBool(state, 2) : false;
    bool clicked = g_api.is_mouse_clicked(ctx_obj->handle, (MonstroMouseButton)mobius_stack_asInt64(state, 1), repeat);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, clicked);
    return 1;
}

static int context_is_mouse_released(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:is_mouse_released() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_mouse_released() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool released = g_api.is_mouse_released(ctx_obj->handle, (MonstroMouseButton)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, released);
    return 1;
}

static int context_is_mouse_double_clicked(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:is_mouse_double_clicked() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_mouse_double_clicked() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool clicked = g_api.is_mouse_double_clicked(ctx_obj->handle, (MonstroMouseButton)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, clicked);
    return 1;
}

static int context_is_mouse_dragging(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:is_mouse_dragging() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_mouse_dragging() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool dragging = g_api.is_mouse_dragging(ctx_obj->handle, (MonstroMouseButton)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, dragging);
    return 1;
}

static int context_mouse_drag_delta(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:mouse_drag_delta() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:mouse_drag_delta() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 value = g_api.get_mouse_drag_delta(ctx_obj->handle, (MonstroMouseButton)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    push_vec2_table(state, value);
    return 1;
}

static int context_mouse_wheel(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:mouse_wheel() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:mouse_wheel() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_mouse_wheel(ctx_obj->handle));
    return 1;
}

static int context_mouse_wheel_h(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:mouse_wheel_h() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:mouse_wheel_h() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_mouse_wheel_h(ctx_obj->handle));
    return 1;
}

static int context_is_key_down(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:is_key_down() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_key_down() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool down = g_api.is_key_down(ctx_obj->handle, (MonstroKey)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, down);
    return 1;
}

static int context_is_key_pressed(MobiusState* state, int arg_count) {
    if (arg_count != 2 && arg_count != 3) return mobius_error(state, "context:is_key_pressed() expects 1 or 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_key_pressed() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool repeat = arg_count == 3 ? mobius_stack_asBool(state, 2) : false;
    bool pressed = g_api.is_key_pressed(ctx_obj->handle, (MonstroKey)mobius_stack_asInt64(state, 1), repeat);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushBool(state, pressed);
    return 1;
}

static int context_is_key_released(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:is_key_released() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_key_released() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool released = g_api.is_key_released(ctx_obj->handle, (MonstroKey)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, released);
    return 1;
}

static int context_key_mods(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:key_mods() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:key_mods() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, (int64_t)g_api.get_key_mods(ctx_obj->handle));
    return 1;
}

static int context_input_text(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:input_text() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:input_text() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    const char* text = g_api.get_input_text(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    mobius_stack_pushString(state, text ? text : "");
    return 1;
}

static int context_set_mouse_cursor(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_mouse_cursor() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_mouse_cursor() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_mouse_cursor(ctx_obj->handle, (MonstroMouseCursor)mobius_stack_asInt64(state, 1));
    return return_self(state, arg_count);
}

static int context_mouse_cursor(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:mouse_cursor() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:mouse_cursor() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushInt64(state, (int64_t)g_api.get_mouse_cursor(ctx_obj->handle));
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

static int context_set_next_window_focus(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:set_next_window_focus() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_next_window_focus() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_next_window_focus(ctx_obj->handle);
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

static int context_window_pos(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:window_pos() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:window_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 pos = g_api.get_window_pos(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, pos);
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

static int context_set_window_pos(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "context:set_window_pos() expects 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_window_pos(ctx_obj->handle,
                         (float)mobius_stack_asFloat64(state, 1),
                         (float)mobius_stack_asFloat64(state, 2),
                         (MonstroCondition)mobius_stack_asInt64(state, 3));
    return return_self(state, arg_count);
}

static int context_set_window_size(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "context:set_window_size() expects 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_size() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_window_size(ctx_obj->handle,
                          (float)mobius_stack_asFloat64(state, 1),
                          (float)mobius_stack_asFloat64(state, 2),
                          (MonstroCondition)mobius_stack_asInt64(state, 3));
    return return_self(state, arg_count);
}

static int context_set_window_focus(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:set_window_focus() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_focus() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_window_focus(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_set_window_layout(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_window_layout() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_layout() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_window_layout(ctx_obj->handle, (MonstroLayoutDirection)mobius_stack_asInt64(state, 1));
    return return_self(state, arg_count);
}

static int context_set_window_title(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_window_title() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_title() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:set_window_title() title must be a string");
    g_api.set_window_title(ctx_obj->handle, mobius_stack_asString(state, 1));
    return return_self(state, arg_count);
}

static int context_set_window_pos_named(MobiusState* state, int arg_count) {
    if (arg_count != 5) return mobius_error(state, "context:set_window_pos_named() expects 4 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_pos_named() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:set_window_pos_named() name must be a string");
    g_api.set_window_pos_named(ctx_obj->handle, mobius_stack_asString(state, 1),
                               (float)mobius_stack_asFloat64(state, 2),
                               (float)mobius_stack_asFloat64(state, 3),
                               (MonstroCondition)mobius_stack_asInt64(state, 4));
    return return_self(state, arg_count);
}

static int context_set_window_size_named(MobiusState* state, int arg_count) {
    if (arg_count != 5) return mobius_error(state, "context:set_window_size_named() expects 4 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_size_named() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:set_window_size_named() name must be a string");
    g_api.set_window_size_named(ctx_obj->handle, mobius_stack_asString(state, 1),
                                (float)mobius_stack_asFloat64(state, 2),
                                (float)mobius_stack_asFloat64(state, 3),
                                (MonstroCondition)mobius_stack_asInt64(state, 4));
    return return_self(state, arg_count);
}

static int context_set_window_focus_named(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_window_focus_named() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_focus_named() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:set_window_focus_named() name must be a string");
    g_api.set_window_focus_named(ctx_obj->handle, mobius_stack_asString(state, 1));
    return return_self(state, arg_count);
}

static int context_set_window_layout_named(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_window_layout_named() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_layout_named() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:set_window_layout_named() name must be a string");
    g_api.set_window_layout_named(ctx_obj->handle, mobius_stack_asString(state, 1),
                                  (MonstroLayoutDirection)mobius_stack_asInt64(state, 2));
    return return_self(state, arg_count);
}

static int context_set_window_title_named(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_window_title_named() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_window_title_named() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1) || !mobius_stack_isString(state, 2)) {
        return mobius_error(state, "context:set_window_title_named() name and title must be strings");
    }
    g_api.set_window_title_named(ctx_obj->handle, mobius_stack_asString(state, 1), mobius_stack_asString(state, 2));
    return return_self(state, arg_count);
}

static int context_scroll_x(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:scroll_x() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:scroll_x() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_scroll_x(ctx_obj->handle));
    return 1;
}

static int context_scroll_y(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:scroll_y() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:scroll_y() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushFloat64(state, g_api.get_scroll_y(ctx_obj->handle));
    return 1;
}

static int context_set_scroll_x(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_scroll_x() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_scroll_x() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_scroll_x(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_set_scroll_y(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:set_scroll_y() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_scroll_y() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_scroll_y(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_is_window_collapsed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_window_collapsed() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_window_collapsed() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_window_collapsed(ctx_obj->handle));
    return 1;
}

static int context_is_window_focused(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_window_focused() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_window_focused() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_window_focused(ctx_obj->handle));
    return 1;
}

static int context_is_window_hovered(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_window_hovered() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_window_hovered() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_window_hovered(ctx_obj->handle));
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

static int context_begin_layout(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:begin_layout() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:begin_layout() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.begin_layout(ctx_obj->handle, (MonstroLayoutDirection)mobius_stack_asInt64(state, 1));
    return return_self(state, arg_count);
}

static int context_begin_layout_at(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "context:begin_layout_at() expects 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:begin_layout_at() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.begin_layout_at(ctx_obj->handle,
                          (float)mobius_stack_asFloat64(state, 1),
                          (float)mobius_stack_asFloat64(state, 2),
                          (MonstroLayoutDirection)mobius_stack_asInt64(state, 3));
    return return_self(state, arg_count);
}

static int context_end_layout(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:end_layout() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:end_layout() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.end_layout(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_add_item(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:add_item() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:add_item() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.add_item(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1), (float)mobius_stack_asFloat64(state, 2));
    return return_self(state, arg_count);
}

static int context_cursor_pos(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:cursor_pos() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:cursor_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 pos = g_api.get_cursor_pos(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, pos);
    return 1;
}

static int context_set_cursor_pos(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_cursor_pos() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_cursor_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_cursor_pos(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1), (float)mobius_stack_asFloat64(state, 2));
    return return_self(state, arg_count);
}

static int context_cursor_screen_pos(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:cursor_screen_pos() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:cursor_screen_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 pos = g_api.get_cursor_screen_pos(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, pos);
    return 1;
}

static int context_set_cursor_screen_pos(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:set_cursor_screen_pos() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:set_cursor_screen_pos() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.set_cursor_screen_pos(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1), (float)mobius_stack_asFloat64(state, 2));
    return return_self(state, arg_count);
}

static int context_content_region_avail(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:content_region_avail() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:content_region_avail() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 size = g_api.get_content_region_avail(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, size);
    return 1;
}

static int context_indent(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:indent() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:indent() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.indent(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_unindent(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:unindent() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:unindent() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.unindent(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_spacer(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:spacer() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:spacer() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.spacer(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1));
    return return_self(state, arg_count);
}

static int context_dummy(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:dummy() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:dummy() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.dummy(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1), (float)mobius_stack_asFloat64(state, 2));
    return return_self(state, arg_count);
}

static int context_percent(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:percent() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:percent() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    float value = g_api.percent(ctx_obj->handle, (float)mobius_stack_asFloat64(state, 1),
                                (MonstroLayoutDirection)mobius_stack_asInt64(state, 2));
    mobius_stack_pop(state, 3);
    mobius_stack_pushFloat64(state, value);
    return 1;
}

static int context_begin_columns(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:begin_columns() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:begin_columns() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:begin_columns() id must be a string");
    g_api.begin_columns(ctx_obj->handle, mobius_stack_asString(state, 1), (int)mobius_stack_asInt64(state, 2));
    return return_self(state, arg_count);
}

static int context_next_column(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:next_column() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:next_column() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.next_column(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_end_columns(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:end_columns() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:end_columns() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.end_columns(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_is_item_hovered(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_item_hovered() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_item_hovered() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_item_hovered(ctx_obj->handle));
    return 1;
}

static int context_is_item_active(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_item_active() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_item_active() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_item_active(ctx_obj->handle));
    return 1;
}

static int context_is_item_clicked(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:is_item_clicked() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_item_clicked() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    bool clicked = g_api.is_item_clicked(ctx_obj->handle, (MonstroMouseButton)mobius_stack_asInt64(state, 1));
    mobius_stack_pop(state, 2);
    mobius_stack_pushBool(state, clicked);
    return 1;
}

static int context_is_item_edited(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_item_edited() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_item_edited() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_item_edited(ctx_obj->handle));
    return 1;
}

static int context_is_item_deactivated_after_edit(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_item_deactivated_after_edit() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_item_deactivated_after_edit() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_item_deactivated_after_edit(ctx_obj->handle));
    return 1;
}

static int context_item_rect(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:item_rect() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:item_rect() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroRect rect = g_api.get_item_rect(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_rect_table(state, rect);
    return 1;
}

static int context_item_rect_min(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:item_rect_min() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:item_rect_min() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 value = g_api.get_item_rect_min(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, value);
    return 1;
}

static int context_item_rect_max(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:item_rect_max() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:item_rect_max() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 value = g_api.get_item_rect_max(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, value);
    return 1;
}

static int context_item_rect_size(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:item_rect_size() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:item_rect_size() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    MonstroVec2 value = g_api.get_item_rect_size(ctx_obj->handle);
    mobius_stack_pop(state, 1);
    push_vec2_table(state, value);
    return 1;
}

static int context_begin_disabled(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:begin_disabled() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:begin_disabled() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.begin_disabled(ctx_obj->handle, mobius_stack_asBool(state, 1));
    return return_self(state, arg_count);
}

static int context_end_disabled(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:end_disabled() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:end_disabled() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.end_disabled(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_is_disabled(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:is_disabled() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:is_disabled() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    mobius_stack_pop(state, 1);
    mobius_stack_pushBool(state, g_api.is_disabled(ctx_obj->handle));
    return 1;
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

static int context_button_sized(MobiusState* state, int arg_count) {
    if (arg_count != 4 && arg_count != 5) return mobius_error(state, "context:button_sized() expects 3 or 4 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:button_sized() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:button_sized() label must be a string");
    const char* tooltip = arg_count == 5 ? get_optional_string(state, 4) : nullptr;
    bool pressed = g_api.button_sized(ctx_obj->handle, mobius_stack_asString(state, 1),
                                      (float)mobius_stack_asFloat64(state, 2),
                                      (float)mobius_stack_asFloat64(state, 3),
                                      tooltip);
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

static int context_label_sized(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "context:label_sized() expects 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:label_sized() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:label_sized() text must be a string");
    g_api.label_sized(ctx_obj->handle, mobius_stack_asString(state, 1),
                      (float)mobius_stack_asFloat64(state, 2),
                      (float)mobius_stack_asFloat64(state, 3));
    return return_self(state, arg_count);
}

static int context_label_wrapped(MobiusState* state, int arg_count) {
    if (arg_count != 3) return mobius_error(state, "context:label_wrapped() expects 2 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:label_wrapped() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:label_wrapped() text must be a string");
    g_api.label_wrapped(ctx_obj->handle, mobius_stack_asString(state, 1), (float)mobius_stack_asFloat64(state, 2));
    return return_self(state, arg_count);
}

static int context_image(MobiusState* state, int arg_count) {
    if (arg_count != 4) return mobius_error(state, "context:image() expects 3 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:image() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.image(ctx_obj->handle, mobius_stack_asUInt64(state, 1),
                (float)mobius_stack_asFloat64(state, 2),
                (float)mobius_stack_asFloat64(state, 3));
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

static int context_radio_button(MobiusState* state, int arg_count) {
    if (arg_count != 4 && arg_count != 5) return mobius_error(state, "context:radio_button() expects 3 or 4 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:radio_button() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:radio_button() label must be a string");
    int value = (int)mobius_stack_asInt64(state, 2);
    int button_value = (int)mobius_stack_asInt64(state, 3);
    const char* tooltip = arg_count == 5 ? get_optional_string(state, 4) : nullptr;
    bool changed = g_api.radio_button(ctx_obj->handle, mobius_stack_asString(state, 1), &value, button_value, tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_int_field(state, tbl, "value", value);
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

static int context_slider_float_ex(MobiusState* state, int arg_count) {
    if (arg_count < 8 || arg_count > 10) return mobius_error(state, "context:slider_float_ex() expects 7 to 9 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:slider_float_ex() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:slider_float_ex() label must be a string");
    float value = (float)mobius_stack_asFloat64(state, 2);
    float min = (float)mobius_stack_asFloat64(state, 3);
    float max = (float)mobius_stack_asFloat64(state, 4);
    const char* format = (arg_count >= 9 && !mobius_stack_isNil(state, 8)) ? mobius_stack_asString(state, 8) : "%.3f";
    const char* tooltip = (arg_count == 10 && !mobius_stack_isNil(state, 9)) ? mobius_stack_asString(state, 9) : nullptr;
    bool changed = g_api.slider_float_ex(ctx_obj->handle, mobius_stack_asString(state, 1), &value,
                                         min, max, format,
                                         (float)mobius_stack_asFloat64(state, 5),
                                         (float)mobius_stack_asFloat64(state, 6),
                                         (MonstroSliderFlags)mobius_stack_asInt64(state, 7),
                                         tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_float_field(state, tbl, "value", value);
    return 1;
}

static int context_slider_int_ex(MobiusState* state, int arg_count) {
    if (arg_count < 8 || arg_count > 10) return mobius_error(state, "context:slider_int_ex() expects 7 to 9 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:slider_int_ex() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:slider_int_ex() label must be a string");
    int value = (int)mobius_stack_asInt64(state, 2);
    int min = (int)mobius_stack_asInt64(state, 3);
    int max = (int)mobius_stack_asInt64(state, 4);
    const char* format = (arg_count >= 9 && !mobius_stack_isNil(state, 8)) ? mobius_stack_asString(state, 8) : "%d";
    const char* tooltip = (arg_count == 10 && !mobius_stack_isNil(state, 9)) ? mobius_stack_asString(state, 9) : nullptr;
    bool changed = g_api.slider_int_ex(ctx_obj->handle, mobius_stack_asString(state, 1), &value,
                                       min, max, format,
                                       (float)mobius_stack_asFloat64(state, 5),
                                       (float)mobius_stack_asFloat64(state, 6),
                                       (MonstroSliderFlags)mobius_stack_asInt64(state, 7),
                                       tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_int_field(state, tbl, "value", value);
    return 1;
}

static int context_knob(MobiusState* state, int arg_count) {
    if (arg_count < 5 || arg_count > 7) return mobius_error(state, "context:knob() expects 4 to 6 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:knob() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:knob() label must be a string");
    float value = (float)mobius_stack_asFloat64(state, 2);
    const char* format = (arg_count >= 6 && !mobius_stack_isNil(state, 5)) ? mobius_stack_asString(state, 5) : "%.3f";
    const char* tooltip = (arg_count == 7 && !mobius_stack_isNil(state, 6)) ? mobius_stack_asString(state, 6) : nullptr;
    bool changed = g_api.knob(ctx_obj->handle, mobius_stack_asString(state, 1), &value,
                              (float)mobius_stack_asFloat64(state, 3),
                              (float)mobius_stack_asFloat64(state, 4),
                              format, tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_float_field(state, tbl, "value", value);
    return 1;
}

static int context_knob_sized(MobiusState* state, int arg_count) {
    if (arg_count < 6 || arg_count > 8) return mobius_error(state, "context:knob_sized() expects 5 to 7 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:knob_sized() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:knob_sized() label must be a string");
    float value = (float)mobius_stack_asFloat64(state, 2);
    const char* format = (arg_count >= 7 && !mobius_stack_isNil(state, 6)) ? mobius_stack_asString(state, 6) : "%.3f";
    const char* tooltip = (arg_count == 8 && !mobius_stack_isNil(state, 7)) ? mobius_stack_asString(state, 7) : nullptr;
    bool changed = g_api.knob_sized(ctx_obj->handle, mobius_stack_asString(state, 1), &value,
                                    (float)mobius_stack_asFloat64(state, 3),
                                    (float)mobius_stack_asFloat64(state, 4),
                                    format,
                                    (float)mobius_stack_asFloat64(state, 5),
                                    tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_float_field(state, tbl, "value", value);
    return 1;
}

static int context_input_int(MobiusState* state, int arg_count) {
    if (arg_count != 4 && arg_count != 5 && arg_count != 6) return mobius_error(state, "context:input_int() expects 3 to 5 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:input_int() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:input_int() label must be a string");
    int value = (int)mobius_stack_asInt64(state, 2);
    const char* tooltip = arg_count >= 6 ? get_optional_string(state, 5) : nullptr;
    bool changed = g_api.input_int(ctx_obj->handle, mobius_stack_asString(state, 1), &value,
                                   (int)mobius_stack_asInt64(state, 3),
                                   (int)mobius_stack_asInt64(state, 4),
                                   tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_int_field(state, tbl, "value", value);
    return 1;
}

static int context_input_float(MobiusState* state, int arg_count) {
    if (arg_count < 4 || arg_count > 7) return mobius_error(state, "context:input_float() expects 3 to 6 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:input_float() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:input_float() label must be a string");
    float value = (float)mobius_stack_asFloat64(state, 2);
    float step_fast = arg_count >= 5 ? (float)mobius_stack_asFloat64(state, 4) : 0.0f;
    const char* format = (arg_count >= 6 && !mobius_stack_isNil(state, 5)) ? mobius_stack_asString(state, 5) : "%.3f";
    const char* tooltip = (arg_count == 7 && !mobius_stack_isNil(state, 6)) ? mobius_stack_asString(state, 6) : nullptr;
    bool changed = g_api.input_float(ctx_obj->handle, mobius_stack_asString(state, 1), &value,
                                     (float)mobius_stack_asFloat64(state, 3), step_fast, format, tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_float_field(state, tbl, "value", value);
    return 1;
}

static int context_input_double(MobiusState* state, int arg_count) {
    if (arg_count < 4 || arg_count > 7) return mobius_error(state, "context:input_double() expects 3 to 6 arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:input_double() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:input_double() label must be a string");
    double value = mobius_stack_asFloat64(state, 2);
    double step_fast = arg_count >= 5 ? mobius_stack_asFloat64(state, 4) : 0.0;
    const char* format = (arg_count >= 6 && !mobius_stack_isNil(state, 5)) ? mobius_stack_asString(state, 5) : "%.6f";
    const char* tooltip = (arg_count == 7 && !mobius_stack_isNil(state, 6)) ? mobius_stack_asString(state, 6) : nullptr;
    bool changed = g_api.input_double(ctx_obj->handle, mobius_stack_asString(state, 1), &value,
                                      mobius_stack_asFloat64(state, 3), step_fast, format, tooltip);
    mobius_stack_pop(state, arg_count);
    mobius_stack_pushNewTable(state, 2);
    int tbl = mobius_stack_size(state) - 1;
    push_bool_field(state, tbl, "changed", changed);
    push_float_field(state, tbl, "value", value);
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

static int context_tooltip(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:tooltip() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:tooltip() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:tooltip() text must be a string");
    g_api.tooltip(ctx_obj->handle, mobius_stack_asString(state, 1));
    return return_self(state, arg_count);
}

static int context_separator(MobiusState* state, int arg_count) {
    if (arg_count != 1) return mobius_error(state, "context:separator() expects no arguments");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:separator() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    g_api.separator(ctx_obj->handle);
    return return_self(state, arg_count);
}

static int context_separator_text(MobiusState* state, int arg_count) {
    if (arg_count != 2) return mobius_error(state, "context:separator_text() expects 1 argument");
    if (!ensure_api_loaded(state)) return -1;
    ContextObject* ctx_obj = get_context_object(state, 0, "context:separator_text() self is not a monstro context");
    if (!ctx_obj) return -1;
    if (ensure_context_open(state, ctx_obj, "monstro context has been destroyed") < 0) return -1;
    if (!mobius_stack_isString(state, 1)) return mobius_error(state, "context:separator_text() text must be a string");
    g_api.separator_text(ctx_obj->handle, mobius_stack_asString(state, 1));
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

    mobius_stack_pushNewTable(state, 80);
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
    copy_module_function(state, module_idx, "__context_set_double_click_distance", context_proto, "set_double_click_distance");
    copy_module_function(state, module_idx, "__context_set_mouse_drag_threshold", context_proto, "set_mouse_drag_threshold");
    copy_module_function(state, module_idx, "__context_set_key_repeat_delay", context_proto, "set_key_repeat_delay");
    copy_module_function(state, module_idx, "__context_set_key_repeat_rate", context_proto, "set_key_repeat_rate");
    copy_module_function(state, module_idx, "__context_double_click_distance", context_proto, "double_click_distance");
    copy_module_function(state, module_idx, "__context_mouse_drag_threshold", context_proto, "mouse_drag_threshold");
    copy_module_function(state, module_idx, "__context_key_repeat_delay", context_proto, "key_repeat_delay");
    copy_module_function(state, module_idx, "__context_key_repeat_rate", context_proto, "key_repeat_rate");
    copy_module_function(state, module_idx, "__context_mouse_delta", context_proto, "mouse_delta");
    copy_module_function(state, module_idx, "__context_is_mouse_down", context_proto, "is_mouse_down");
    copy_module_function(state, module_idx, "__context_is_mouse_clicked", context_proto, "is_mouse_clicked");
    copy_module_function(state, module_idx, "__context_is_mouse_released", context_proto, "is_mouse_released");
    copy_module_function(state, module_idx, "__context_is_mouse_double_clicked", context_proto, "is_mouse_double_clicked");
    copy_module_function(state, module_idx, "__context_is_mouse_dragging", context_proto, "is_mouse_dragging");
    copy_module_function(state, module_idx, "__context_mouse_drag_delta", context_proto, "mouse_drag_delta");
    copy_module_function(state, module_idx, "__context_mouse_wheel", context_proto, "mouse_wheel");
    copy_module_function(state, module_idx, "__context_mouse_wheel_h", context_proto, "mouse_wheel_h");
    copy_module_function(state, module_idx, "__context_is_key_down", context_proto, "is_key_down");
    copy_module_function(state, module_idx, "__context_is_key_pressed", context_proto, "is_key_pressed");
    copy_module_function(state, module_idx, "__context_is_key_released", context_proto, "is_key_released");
    copy_module_function(state, module_idx, "__context_key_mods", context_proto, "key_mods");
    copy_module_function(state, module_idx, "__context_input_text", context_proto, "input_text");
    copy_module_function(state, module_idx, "__context_set_mouse_cursor", context_proto, "set_mouse_cursor");
    copy_module_function(state, module_idx, "__context_mouse_cursor", context_proto, "mouse_cursor");
    copy_module_function(state, module_idx, "__context_set_next_window_pos", context_proto, "set_next_window_pos");
    copy_module_function(state, module_idx, "__context_set_next_window_size", context_proto, "set_next_window_size");
    copy_module_function(state, module_idx, "__context_set_next_window_focus", context_proto, "set_next_window_focus");
    copy_module_function(state, module_idx, "__context_begin_window", context_proto, "begin_window");
    copy_module_function(state, module_idx, "__context_end_window", context_proto, "end_window");
    copy_module_function(state, module_idx, "__context_window_rect", context_proto, "window_rect");
    copy_module_function(state, module_idx, "__context_window_pos", context_proto, "window_pos");
    copy_module_function(state, module_idx, "__context_window_size", context_proto, "window_size");
    copy_module_function(state, module_idx, "__context_set_window_pos", context_proto, "set_window_pos");
    copy_module_function(state, module_idx, "__context_set_window_size", context_proto, "set_window_size");
    copy_module_function(state, module_idx, "__context_set_window_focus", context_proto, "set_window_focus");
    copy_module_function(state, module_idx, "__context_set_window_layout", context_proto, "set_window_layout");
    copy_module_function(state, module_idx, "__context_set_window_title", context_proto, "set_window_title");
    copy_module_function(state, module_idx, "__context_set_window_pos_named", context_proto, "set_window_pos_named");
    copy_module_function(state, module_idx, "__context_set_window_size_named", context_proto, "set_window_size_named");
    copy_module_function(state, module_idx, "__context_set_window_focus_named", context_proto, "set_window_focus_named");
    copy_module_function(state, module_idx, "__context_set_window_layout_named", context_proto, "set_window_layout_named");
    copy_module_function(state, module_idx, "__context_set_window_title_named", context_proto, "set_window_title_named");
    copy_module_function(state, module_idx, "__context_scroll_x", context_proto, "scroll_x");
    copy_module_function(state, module_idx, "__context_scroll_y", context_proto, "scroll_y");
    copy_module_function(state, module_idx, "__context_set_scroll_x", context_proto, "set_scroll_x");
    copy_module_function(state, module_idx, "__context_set_scroll_y", context_proto, "set_scroll_y");
    copy_module_function(state, module_idx, "__context_is_window_collapsed", context_proto, "is_window_collapsed");
    copy_module_function(state, module_idx, "__context_is_window_focused", context_proto, "is_window_focused");
    copy_module_function(state, module_idx, "__context_is_window_hovered", context_proto, "is_window_hovered");
    copy_module_function(state, module_idx, "__context_begin_child", context_proto, "begin_child");
    copy_module_function(state, module_idx, "__context_end_child", context_proto, "end_child");
    copy_module_function(state, module_idx, "__context_begin_layout", context_proto, "begin_layout");
    copy_module_function(state, module_idx, "__context_begin_layout_at", context_proto, "begin_layout_at");
    copy_module_function(state, module_idx, "__context_end_layout", context_proto, "end_layout");
    copy_module_function(state, module_idx, "__context_add_item", context_proto, "add_item");
    copy_module_function(state, module_idx, "__context_cursor_pos", context_proto, "cursor_pos");
    copy_module_function(state, module_idx, "__context_set_cursor_pos", context_proto, "set_cursor_pos");
    copy_module_function(state, module_idx, "__context_cursor_screen_pos", context_proto, "cursor_screen_pos");
    copy_module_function(state, module_idx, "__context_set_cursor_screen_pos", context_proto, "set_cursor_screen_pos");
    copy_module_function(state, module_idx, "__context_content_region_avail", context_proto, "content_region_avail");
    copy_module_function(state, module_idx, "__context_indent", context_proto, "indent");
    copy_module_function(state, module_idx, "__context_unindent", context_proto, "unindent");
    copy_module_function(state, module_idx, "__context_spacer", context_proto, "spacer");
    copy_module_function(state, module_idx, "__context_dummy", context_proto, "dummy");
    copy_module_function(state, module_idx, "__context_percent", context_proto, "percent");
    copy_module_function(state, module_idx, "__context_begin_columns", context_proto, "begin_columns");
    copy_module_function(state, module_idx, "__context_next_column", context_proto, "next_column");
    copy_module_function(state, module_idx, "__context_end_columns", context_proto, "end_columns");
    copy_module_function(state, module_idx, "__context_is_item_hovered", context_proto, "is_item_hovered");
    copy_module_function(state, module_idx, "__context_is_item_active", context_proto, "is_item_active");
    copy_module_function(state, module_idx, "__context_is_item_clicked", context_proto, "is_item_clicked");
    copy_module_function(state, module_idx, "__context_is_item_edited", context_proto, "is_item_edited");
    copy_module_function(state, module_idx, "__context_is_item_deactivated_after_edit", context_proto, "is_item_deactivated_after_edit");
    copy_module_function(state, module_idx, "__context_item_rect", context_proto, "item_rect");
    copy_module_function(state, module_idx, "__context_item_rect_min", context_proto, "item_rect_min");
    copy_module_function(state, module_idx, "__context_item_rect_max", context_proto, "item_rect_max");
    copy_module_function(state, module_idx, "__context_item_rect_size", context_proto, "item_rect_size");
    copy_module_function(state, module_idx, "__context_begin_disabled", context_proto, "begin_disabled");
    copy_module_function(state, module_idx, "__context_end_disabled", context_proto, "end_disabled");
    copy_module_function(state, module_idx, "__context_is_disabled", context_proto, "is_disabled");
    copy_module_function(state, module_idx, "__context_button", context_proto, "button");
    copy_module_function(state, module_idx, "__context_button_sized", context_proto, "button_sized");
    copy_module_function(state, module_idx, "__context_label", context_proto, "label");
    copy_module_function(state, module_idx, "__context_label_sized", context_proto, "label_sized");
    copy_module_function(state, module_idx, "__context_label_wrapped", context_proto, "label_wrapped");
    copy_module_function(state, module_idx, "__context_image", context_proto, "image");
    copy_module_function(state, module_idx, "__context_checkbox", context_proto, "checkbox");
    copy_module_function(state, module_idx, "__context_radio_button", context_proto, "radio_button");
    copy_module_function(state, module_idx, "__context_slider_float", context_proto, "slider_float");
    copy_module_function(state, module_idx, "__context_slider_int", context_proto, "slider_int");
    copy_module_function(state, module_idx, "__context_slider_float_ex", context_proto, "slider_float_ex");
    copy_module_function(state, module_idx, "__context_slider_int_ex", context_proto, "slider_int_ex");
    copy_module_function(state, module_idx, "__context_knob", context_proto, "knob");
    copy_module_function(state, module_idx, "__context_knob_sized", context_proto, "knob_sized");
    copy_module_function(state, module_idx, "__context_input_int", context_proto, "input_int");
    copy_module_function(state, module_idx, "__context_input_float", context_proto, "input_float");
    copy_module_function(state, module_idx, "__context_input_double", context_proto, "input_double");
    copy_module_function(state, module_idx, "__context_progress_bar", context_proto, "progress_bar");
    copy_module_function(state, module_idx, "__context_tooltip", context_proto, "tooltip");
    copy_module_function(state, module_idx, "__context_separator", context_proto, "separator");
    copy_module_function(state, module_idx, "__context_separator_text", context_proto, "separator_text");
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
        "__context_want_text_input", "__context_set_double_click_distance",
        "__context_set_mouse_drag_threshold", "__context_set_key_repeat_delay",
        "__context_set_key_repeat_rate", "__context_double_click_distance",
        "__context_mouse_drag_threshold", "__context_key_repeat_delay",
        "__context_key_repeat_rate", "__context_mouse_delta", "__context_is_mouse_down",
        "__context_is_mouse_clicked", "__context_is_mouse_released",
        "__context_is_mouse_double_clicked", "__context_is_mouse_dragging",
        "__context_mouse_drag_delta", "__context_mouse_wheel",
        "__context_mouse_wheel_h", "__context_is_key_down",
        "__context_is_key_pressed", "__context_is_key_released",
        "__context_key_mods", "__context_input_text", "__context_set_mouse_cursor",
        "__context_mouse_cursor", "__context_set_next_window_pos",
        "__context_set_next_window_size", "__context_set_next_window_focus",
        "__context_begin_window", "__context_end_window", "__context_window_rect",
        "__context_window_pos", "__context_window_size", "__context_set_window_pos",
        "__context_set_window_size", "__context_set_window_focus",
        "__context_set_window_layout", "__context_set_window_title",
        "__context_set_window_pos_named", "__context_set_window_size_named",
        "__context_set_window_focus_named", "__context_set_window_layout_named",
        "__context_set_window_title_named", "__context_scroll_x",
        "__context_scroll_y", "__context_set_scroll_x", "__context_set_scroll_y",
        "__context_is_window_collapsed", "__context_is_window_focused",
        "__context_is_window_hovered", "__context_begin_child",
        "__context_end_child", "__context_begin_layout",
        "__context_begin_layout_at", "__context_end_layout",
        "__context_add_item", "__context_cursor_pos", "__context_set_cursor_pos",
        "__context_cursor_screen_pos", "__context_set_cursor_screen_pos",
        "__context_content_region_avail", "__context_indent",
        "__context_unindent", "__context_spacer", "__context_dummy",
        "__context_percent", "__context_begin_columns", "__context_next_column",
        "__context_end_columns", "__context_is_item_hovered",
        "__context_is_item_active", "__context_is_item_clicked",
        "__context_is_item_edited", "__context_is_item_deactivated_after_edit",
        "__context_item_rect", "__context_item_rect_min", "__context_item_rect_max",
        "__context_item_rect_size", "__context_begin_disabled",
        "__context_end_disabled", "__context_is_disabled", "__context_button",
        "__context_button_sized", "__context_label", "__context_label_sized",
        "__context_label_wrapped", "__context_image", "__context_checkbox",
        "__context_radio_button", "__context_slider_float",
        "__context_slider_int", "__context_slider_float_ex",
        "__context_slider_int_ex", "__context_knob", "__context_knob_sized",
        "__context_input_int", "__context_input_float", "__context_input_double",
        "__context_progress_bar", "__context_tooltip", "__context_separator",
        "__context_separator_text", "__context_draw_data"
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
    {"__context_set_double_click_distance", context_set_double_click_distance, 2, MOBIUS_VAL_USERDATA, "Internal set_double_click_distance method"},
    {"__context_set_mouse_drag_threshold", context_set_mouse_drag_threshold, 2, MOBIUS_VAL_USERDATA, "Internal set_mouse_drag_threshold method"},
    {"__context_set_key_repeat_delay", context_set_key_repeat_delay, 2, MOBIUS_VAL_USERDATA, "Internal set_key_repeat_delay method"},
    {"__context_set_key_repeat_rate", context_set_key_repeat_rate, 2, MOBIUS_VAL_USERDATA, "Internal set_key_repeat_rate method"},
    {"__context_double_click_distance", context_double_click_distance, 1, MOBIUS_VAL_FLOAT64, "Internal double_click_distance method"},
    {"__context_mouse_drag_threshold", context_mouse_drag_threshold, 1, MOBIUS_VAL_FLOAT64, "Internal mouse_drag_threshold method"},
    {"__context_key_repeat_delay", context_key_repeat_delay, 1, MOBIUS_VAL_FLOAT64, "Internal key_repeat_delay method"},
    {"__context_key_repeat_rate", context_key_repeat_rate, 1, MOBIUS_VAL_FLOAT64, "Internal key_repeat_rate method"},
    {"__context_mouse_delta", context_mouse_delta, 1, MOBIUS_VAL_TABLE, "Internal mouse_delta method"},
    {"__context_is_mouse_down", context_is_mouse_down, 2, MOBIUS_VAL_BOOL, "Internal is_mouse_down method"},
    {"__context_is_mouse_clicked", context_is_mouse_clicked, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal is_mouse_clicked method"},
    {"__context_is_mouse_released", context_is_mouse_released, 2, MOBIUS_VAL_BOOL, "Internal is_mouse_released method"},
    {"__context_is_mouse_double_clicked", context_is_mouse_double_clicked, 2, MOBIUS_VAL_BOOL, "Internal is_mouse_double_clicked method"},
    {"__context_is_mouse_dragging", context_is_mouse_dragging, 2, MOBIUS_VAL_BOOL, "Internal is_mouse_dragging method"},
    {"__context_mouse_drag_delta", context_mouse_drag_delta, 2, MOBIUS_VAL_TABLE, "Internal mouse_drag_delta method"},
    {"__context_mouse_wheel", context_mouse_wheel, 1, MOBIUS_VAL_FLOAT64, "Internal mouse_wheel method"},
    {"__context_mouse_wheel_h", context_mouse_wheel_h, 1, MOBIUS_VAL_FLOAT64, "Internal mouse_wheel_h method"},
    {"__context_is_key_down", context_is_key_down, 2, MOBIUS_VAL_BOOL, "Internal is_key_down method"},
    {"__context_is_key_pressed", context_is_key_pressed, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal is_key_pressed method"},
    {"__context_is_key_released", context_is_key_released, 2, MOBIUS_VAL_BOOL, "Internal is_key_released method"},
    {"__context_key_mods", context_key_mods, 1, MOBIUS_VAL_INT64, "Internal key_mods method"},
    {"__context_input_text", context_input_text, 1, MOBIUS_VAL_STRING, "Internal input_text method"},
    {"__context_set_mouse_cursor", context_set_mouse_cursor, 2, MOBIUS_VAL_USERDATA, "Internal set_mouse_cursor method"},
    {"__context_mouse_cursor", context_mouse_cursor, 1, MOBIUS_VAL_INT64, "Internal mouse_cursor method"},
    {"__context_set_next_window_pos", context_set_next_window_pos, 4, MOBIUS_VAL_USERDATA, "Internal set_next_window_pos method"},
    {"__context_set_next_window_size", context_set_next_window_size, 4, MOBIUS_VAL_USERDATA, "Internal set_next_window_size method"},
    {"__context_set_next_window_focus", context_set_next_window_focus, 1, MOBIUS_VAL_USERDATA, "Internal set_next_window_focus method"},
    {"__context_begin_window", context_begin_window, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal begin_window method"},
    {"__context_end_window", context_end_window, 1, MOBIUS_VAL_USERDATA, "Internal end_window method"},
    {"__context_window_rect", context_window_rect, 1, MOBIUS_VAL_TABLE, "Internal window_rect method"},
    {"__context_window_pos", context_window_pos, 1, MOBIUS_VAL_TABLE, "Internal window_pos method"},
    {"__context_window_size", context_window_size, 1, MOBIUS_VAL_TABLE, "Internal window_size method"},
    {"__context_set_window_pos", context_set_window_pos, 4, MOBIUS_VAL_USERDATA, "Internal set_window_pos method"},
    {"__context_set_window_size", context_set_window_size, 4, MOBIUS_VAL_USERDATA, "Internal set_window_size method"},
    {"__context_set_window_focus", context_set_window_focus, 1, MOBIUS_VAL_USERDATA, "Internal set_window_focus method"},
    {"__context_set_window_layout", context_set_window_layout, 2, MOBIUS_VAL_USERDATA, "Internal set_window_layout method"},
    {"__context_set_window_title", context_set_window_title, 2, MOBIUS_VAL_USERDATA, "Internal set_window_title method"},
    {"__context_set_window_pos_named", context_set_window_pos_named, 5, MOBIUS_VAL_USERDATA, "Internal set_window_pos_named method"},
    {"__context_set_window_size_named", context_set_window_size_named, 5, MOBIUS_VAL_USERDATA, "Internal set_window_size_named method"},
    {"__context_set_window_focus_named", context_set_window_focus_named, 2, MOBIUS_VAL_USERDATA, "Internal set_window_focus_named method"},
    {"__context_set_window_layout_named", context_set_window_layout_named, 3, MOBIUS_VAL_USERDATA, "Internal set_window_layout_named method"},
    {"__context_set_window_title_named", context_set_window_title_named, 3, MOBIUS_VAL_USERDATA, "Internal set_window_title_named method"},
    {"__context_scroll_x", context_scroll_x, 1, MOBIUS_VAL_FLOAT64, "Internal scroll_x method"},
    {"__context_scroll_y", context_scroll_y, 1, MOBIUS_VAL_FLOAT64, "Internal scroll_y method"},
    {"__context_set_scroll_x", context_set_scroll_x, 2, MOBIUS_VAL_USERDATA, "Internal set_scroll_x method"},
    {"__context_set_scroll_y", context_set_scroll_y, 2, MOBIUS_VAL_USERDATA, "Internal set_scroll_y method"},
    {"__context_is_window_collapsed", context_is_window_collapsed, 1, MOBIUS_VAL_BOOL, "Internal is_window_collapsed method"},
    {"__context_is_window_focused", context_is_window_focused, 1, MOBIUS_VAL_BOOL, "Internal is_window_focused method"},
    {"__context_is_window_hovered", context_is_window_hovered, 1, MOBIUS_VAL_BOOL, "Internal is_window_hovered method"},
    {"__context_begin_child", context_begin_child, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal begin_child method"},
    {"__context_end_child", context_end_child, 1, MOBIUS_VAL_USERDATA, "Internal end_child method"},
    {"__context_begin_layout", context_begin_layout, 2, MOBIUS_VAL_USERDATA, "Internal begin_layout method"},
    {"__context_begin_layout_at", context_begin_layout_at, 4, MOBIUS_VAL_USERDATA, "Internal begin_layout_at method"},
    {"__context_end_layout", context_end_layout, 1, MOBIUS_VAL_USERDATA, "Internal end_layout method"},
    {"__context_add_item", context_add_item, 3, MOBIUS_VAL_USERDATA, "Internal add_item method"},
    {"__context_cursor_pos", context_cursor_pos, 1, MOBIUS_VAL_TABLE, "Internal cursor_pos method"},
    {"__context_set_cursor_pos", context_set_cursor_pos, 3, MOBIUS_VAL_USERDATA, "Internal set_cursor_pos method"},
    {"__context_cursor_screen_pos", context_cursor_screen_pos, 1, MOBIUS_VAL_TABLE, "Internal cursor_screen_pos method"},
    {"__context_set_cursor_screen_pos", context_set_cursor_screen_pos, 3, MOBIUS_VAL_USERDATA, "Internal set_cursor_screen_pos method"},
    {"__context_content_region_avail", context_content_region_avail, 1, MOBIUS_VAL_TABLE, "Internal content_region_avail method"},
    {"__context_indent", context_indent, 2, MOBIUS_VAL_USERDATA, "Internal indent method"},
    {"__context_unindent", context_unindent, 2, MOBIUS_VAL_USERDATA, "Internal unindent method"},
    {"__context_spacer", context_spacer, 2, MOBIUS_VAL_USERDATA, "Internal spacer method"},
    {"__context_dummy", context_dummy, 3, MOBIUS_VAL_USERDATA, "Internal dummy method"},
    {"__context_percent", context_percent, 3, MOBIUS_VAL_FLOAT64, "Internal percent method"},
    {"__context_begin_columns", context_begin_columns, 3, MOBIUS_VAL_USERDATA, "Internal begin_columns method"},
    {"__context_next_column", context_next_column, 1, MOBIUS_VAL_USERDATA, "Internal next_column method"},
    {"__context_end_columns", context_end_columns, 1, MOBIUS_VAL_USERDATA, "Internal end_columns method"},
    {"__context_is_item_hovered", context_is_item_hovered, 1, MOBIUS_VAL_BOOL, "Internal is_item_hovered method"},
    {"__context_is_item_active", context_is_item_active, 1, MOBIUS_VAL_BOOL, "Internal is_item_active method"},
    {"__context_is_item_clicked", context_is_item_clicked, 2, MOBIUS_VAL_BOOL, "Internal is_item_clicked method"},
    {"__context_is_item_edited", context_is_item_edited, 1, MOBIUS_VAL_BOOL, "Internal is_item_edited method"},
    {"__context_is_item_deactivated_after_edit", context_is_item_deactivated_after_edit, 1, MOBIUS_VAL_BOOL, "Internal is_item_deactivated_after_edit method"},
    {"__context_item_rect", context_item_rect, 1, MOBIUS_VAL_TABLE, "Internal item_rect method"},
    {"__context_item_rect_min", context_item_rect_min, 1, MOBIUS_VAL_TABLE, "Internal item_rect_min method"},
    {"__context_item_rect_max", context_item_rect_max, 1, MOBIUS_VAL_TABLE, "Internal item_rect_max method"},
    {"__context_item_rect_size", context_item_rect_size, 1, MOBIUS_VAL_TABLE, "Internal item_rect_size method"},
    {"__context_begin_disabled", context_begin_disabled, 2, MOBIUS_VAL_USERDATA, "Internal begin_disabled method"},
    {"__context_end_disabled", context_end_disabled, 1, MOBIUS_VAL_USERDATA, "Internal end_disabled method"},
    {"__context_is_disabled", context_is_disabled, 1, MOBIUS_VAL_BOOL, "Internal is_disabled method"},
    {"__context_button", context_button, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal button method"},
    {"__context_button_sized", context_button_sized, SIZE_MAX, MOBIUS_VAL_BOOL, "Internal button_sized method"},
    {"__context_label", context_label, 2, MOBIUS_VAL_USERDATA, "Internal label method"},
    {"__context_label_sized", context_label_sized, 4, MOBIUS_VAL_USERDATA, "Internal label_sized method"},
    {"__context_label_wrapped", context_label_wrapped, 3, MOBIUS_VAL_USERDATA, "Internal label_wrapped method"},
    {"__context_image", context_image, 4, MOBIUS_VAL_USERDATA, "Internal image method"},
    {"__context_checkbox", context_checkbox, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal checkbox method"},
    {"__context_radio_button", context_radio_button, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal radio_button method"},
    {"__context_slider_float", context_slider_float, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal slider_float method"},
    {"__context_slider_int", context_slider_int, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal slider_int method"},
    {"__context_slider_float_ex", context_slider_float_ex, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal slider_float_ex method"},
    {"__context_slider_int_ex", context_slider_int_ex, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal slider_int_ex method"},
    {"__context_knob", context_knob, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal knob method"},
    {"__context_knob_sized", context_knob_sized, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal knob_sized method"},
    {"__context_input_int", context_input_int, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal input_int method"},
    {"__context_input_float", context_input_float, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal input_float method"},
    {"__context_input_double", context_input_double, SIZE_MAX, MOBIUS_VAL_TABLE, "Internal input_double method"},
    {"__context_progress_bar", context_progress_bar, SIZE_MAX, MOBIUS_VAL_USERDATA, "Internal progress_bar method"},
    {"__context_tooltip", context_tooltip, 2, MOBIUS_VAL_USERDATA, "Internal tooltip method"},
    {"__context_separator", context_separator, 1, MOBIUS_VAL_USERDATA, "Internal separator method"},
    {"__context_separator_text", context_separator_text, 2, MOBIUS_VAL_USERDATA, "Internal separator_text method"},
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
