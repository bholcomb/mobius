#include "library/struct_view_lib.h"

#include "data/array.h"
#include "data/buffer.h"
#include "data/shared_cell.h"
#include "data/table.h"
#include "data/value.h"
#include "internal/ref_counted.h"
#include "state/mobius_state.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

static constexpr const char* STRUCT_LAYOUT_TYPE = "StructLayout";
static constexpr const char* STRUCT_VIEW_TYPE = "StructView";
static constexpr const char* STRUCT_ARRAY_VIEW_TYPE = "StructArrayView";
enum class InteropScalarKind {
    INT8,
    UINT8,
    INT16,
    UINT16,
    INT32,
    UINT32,
    INT64,
    UINT64,
    FLOAT32,
    FLOAT64,
    BOOL8,
};

enum class StructFieldKind {
    SCALAR,
    LAYOUT,
};

struct StructLayout;

struct StructFieldDesc {
    MobiusString* name = nullptr;
    StructFieldKind kind = StructFieldKind::SCALAR;
    InteropScalarKind scalar = InteropScalarKind::UINT8;
    StructLayout* nested_layout = nullptr;
    size_t offset = 0;
    size_t size = 0;
    size_t align = 1;
    size_t stride = 0;
    size_t count = 1;
};

struct StructLayout : public RefCounted {
    std::string name;
    bool packed = false;
    size_t size = 0;
    size_t align = 1;
    std::vector<StructFieldDesc> fields;
    std::unordered_map<MobiusString*, size_t> field_lookup;

    ~StructLayout() override {
        for (StructFieldDesc& field : fields) {
            if (field.nested_layout) field.nested_layout->release();
        }
    }
};

struct StructView {
    StructLayout* layout = nullptr;
    BufferValue* buffer = nullptr;
    size_t base_offset = 0;
};

struct StructArrayView {
    StructLayout* nested_layout = nullptr;
    BufferValue* buffer = nullptr;
    InteropScalarKind scalar = InteropScalarKind::UINT8;
    StructFieldKind kind = StructFieldKind::SCALAR;
    size_t base_offset = 0;
    size_t stride = 0;
    size_t count = 0;
};

struct TypeInfo {
    StructFieldKind kind = StructFieldKind::SCALAR;
    InteropScalarKind scalar = InteropScalarKind::UINT8;
    StructLayout* nested_layout = nullptr;
    size_t size = 0;
    size_t align = 1;
};

struct LayoutBuildResult {
    std::vector<StructFieldDesc> fields;
    size_t size = 0;
    size_t align = 1;
};

static size_t align_up(size_t value, size_t alignment) {
    if (alignment <= 1) return value;
    size_t rem = value % alignment;
    return rem == 0 ? value : value + (alignment - rem);
}

static bool value_to_size(const Value& value, size_t* out) {
    if (value.type == VAL_INT64 && value.as.i64 >= 0) {
        *out = (size_t)value.as.i64;
        return true;
    }
    if (value.type == VAL_UINT64) {
        *out = (size_t)value.as.u64;
        return true;
    }
    return false;
}

static bool value_to_boolish(const Value& value, bool* out) {
    if (value.type == VAL_BOOL) {
        *out = value.as.boolean;
        return true;
    }
    if (value.type == VAL_INT64) {
        *out = value.as.i64 != 0;
        return true;
    }
    if (value.type == VAL_UINT64) {
        *out = value.as.u64 != 0;
        return true;
    }
    return false;
}

static bool resolve_builtin_scalar(MobiusString* name, InteropScalarKind* out_kind,
                                   size_t* out_size, size_t* out_align) {
    if (!name) return false;
    const char* text = name->data;
    if (strcmp(text, "int8") == 0) {
        *out_kind = InteropScalarKind::INT8; *out_size = *out_align = 1; return true;
    }
    if (strcmp(text, "uint8") == 0 || strcmp(text, "byte") == 0) {
        *out_kind = InteropScalarKind::UINT8; *out_size = *out_align = 1; return true;
    }
    if (strcmp(text, "bool") == 0 || strcmp(text, "bool8") == 0) {
        *out_kind = InteropScalarKind::BOOL8; *out_size = *out_align = 1; return true;
    }
    if (strcmp(text, "int16") == 0) {
        *out_kind = InteropScalarKind::INT16; *out_size = *out_align = 2; return true;
    }
    if (strcmp(text, "uint16") == 0) {
        *out_kind = InteropScalarKind::UINT16; *out_size = *out_align = 2; return true;
    }
    if (strcmp(text, "int32") == 0) {
        *out_kind = InteropScalarKind::INT32; *out_size = *out_align = 4; return true;
    }
    if (strcmp(text, "uint32") == 0) {
        *out_kind = InteropScalarKind::UINT32; *out_size = *out_align = 4; return true;
    }
    if (strcmp(text, "int64") == 0) {
        *out_kind = InteropScalarKind::INT64; *out_size = *out_align = 8; return true;
    }
    if (strcmp(text, "uint64") == 0) {
        *out_kind = InteropScalarKind::UINT64; *out_size = *out_align = 8; return true;
    }
    if (strcmp(text, "float32") == 0) {
        *out_kind = InteropScalarKind::FLOAT32; *out_size = *out_align = 4; return true;
    }
    if (strcmp(text, "float64") == 0) {
        *out_kind = InteropScalarKind::FLOAT64; *out_size = *out_align = 8; return true;
    }
    return false;
}

static bool extract_layout_userdata(const Value& value, StructLayout** out) {
    if (value.type != VAL_USERDATA || !value.as.userdata || !value.as.userdata->type_tag) return false;
    if (strcmp(value.as.userdata->type_name, STRUCT_LAYOUT_TYPE) != 0) return false;
    *out = static_cast<StructLayout*>(value.as.userdata->ptr);
    return *out != nullptr;
}

static BufferValue* extract_buffer_value(const Value& value) {
    return (value.type == VAL_BUFFER && value.as.buffer) ? value.as.buffer : nullptr;
}

static BufferValue* extract_buffer_self_or_shared(MobiusState* state, const char* err) {
    const Value& self = state->npeek_self();
    if (self.type == VAL_BUFFER && self.as.buffer) return self.as.buffer;
    if (self.type == VAL_SHARED_CELL && self.as.shared_cell) {
        std::lock_guard<std::recursive_mutex> lock(self.as.shared_cell->mutex());
        Value& inner = self.as.shared_cell->unsafeValue();
        if (inner.type == VAL_BUFFER && inner.as.buffer) return inner.as.buffer;
    }
    state->error(err);
    return nullptr;
}

static StructLayout* extract_layout_arg(MobiusState* state, const Value& value, const char* err) {
    StructLayout* layout = nullptr;
    if (!extract_layout_userdata(value, &layout)) {
        state->error(err);
        return nullptr;
    }
    return layout;
}

static StructLayout* require_layout_self(MobiusState* state, const char* err) {
    const Value& self = state->npeek_self();
    StructLayout* layout = nullptr;
    if (!extract_layout_userdata(self, &layout)) {
        state->error(err);
        return nullptr;
    }
    return layout;
}

static StructView* require_view_self(MobiusState* state, const char* err) {
    const Value& self = state->npeek_self();
    if (self.type != VAL_USERDATA || !self.as.userdata ||
        strcmp(self.as.userdata->type_name, STRUCT_VIEW_TYPE) != 0) {
        state->error(err);
        return nullptr;
    }
    return static_cast<StructView*>(self.as.userdata->ptr);
}

static StructArrayView* require_array_view_self(MobiusState* state, const char* err) {
    const Value& self = state->npeek_self();
    if (self.type != VAL_USERDATA || !self.as.userdata ||
        strcmp(self.as.userdata->type_name, STRUCT_ARRAY_VIEW_TYPE) != 0) {
        state->error(err);
        return nullptr;
    }
    return static_cast<StructArrayView*>(self.as.userdata->ptr);
}

static bool ensure_view_span(MobiusState* state, BufferValue* buffer, size_t offset, size_t needed) {
    if (!buffer) return false;
    if (offset > buffer->size() || needed > (buffer->size() - offset)) {
        state->error("struct view is out of bounds for the current buffer size");
        return false;
    }
    return true;
}

static void destroy_struct_layout_userdata(void* ptr);
static void destroy_struct_view_userdata(void* ptr);
static void destroy_struct_array_view_userdata(void* ptr);

static Value make_struct_view_value(MobiusState* state, StructLayout* layout,
                                    BufferValue* buffer, size_t base_offset);
static Value make_struct_array_view_value(MobiusState* state, const StructFieldDesc& field,
                                          BufferValue* buffer, size_t base_offset);

template<typename T>
static T load_unaligned(const uint8_t* ptr) {
    T out{};
    memcpy(&out, ptr, sizeof(T));
    return out;
}

template<typename T>
static void store_unaligned(uint8_t* ptr, T value) {
    memcpy(ptr, &value, sizeof(T));
}

static Value read_scalar_value(const StructFieldDesc& field, const uint8_t* ptr) {
    switch (field.scalar) {
        case InteropScalarKind::INT8:   return make_int64_value((int64_t)*(const int8_t*)ptr);
        case InteropScalarKind::UINT8:  return make_uint64_value((uint64_t)*(const uint8_t*)ptr);
        case InteropScalarKind::INT16:  return make_int64_value((int64_t)load_unaligned<int16_t>(ptr));
        case InteropScalarKind::UINT16: return make_uint64_value((uint64_t)load_unaligned<uint16_t>(ptr));
        case InteropScalarKind::INT32:  return make_int64_value((int64_t)load_unaligned<int32_t>(ptr));
        case InteropScalarKind::UINT32: return make_uint64_value((uint64_t)load_unaligned<uint32_t>(ptr));
        case InteropScalarKind::INT64:  return make_int64_value((int64_t)load_unaligned<int64_t>(ptr));
        case InteropScalarKind::UINT64: return make_uint64_value((uint64_t)load_unaligned<uint64_t>(ptr));
        case InteropScalarKind::FLOAT32:return make_float_value((double)load_unaligned<float>(ptr));
        case InteropScalarKind::FLOAT64:return make_float_value(load_unaligned<double>(ptr));
        case InteropScalarKind::BOOL8:  return make_bool_value(*ptr != 0);
    }
    return Value();
}

static bool write_scalar_value(MobiusState* state, const StructFieldDesc& field,
                               uint8_t* ptr, const Value& value) {
    switch (field.scalar) {
        case InteropScalarKind::INT8:
        case InteropScalarKind::INT16:
        case InteropScalarKind::INT32:
        case InteropScalarKind::INT64: {
            if (value.type != VAL_INT64 && value.type != VAL_UINT64) {
                state->error("expected integer value for scalar field");
                return false;
            }
            int64_t v = (value.type == VAL_UINT64) ? (int64_t)value.as.u64 : value.as.i64;
            switch (field.scalar) {
                case InteropScalarKind::INT8:  *(int8_t*)ptr = (int8_t)v; break;
                case InteropScalarKind::INT16: store_unaligned<int16_t>(ptr, (int16_t)v); break;
                case InteropScalarKind::INT32: store_unaligned<int32_t>(ptr, (int32_t)v); break;
                case InteropScalarKind::INT64: store_unaligned<int64_t>(ptr, (int64_t)v); break;
                default: break;
            }
            return true;
        }
        case InteropScalarKind::UINT8:
        case InteropScalarKind::UINT16:
        case InteropScalarKind::UINT32:
        case InteropScalarKind::UINT64: {
            if (value.type != VAL_INT64 && value.type != VAL_UINT64) {
                state->error("expected integer value for scalar field");
                return false;
            }
            uint64_t v = (value.type == VAL_UINT64) ? value.as.u64 : (uint64_t)value.as.i64;
            switch (field.scalar) {
                case InteropScalarKind::UINT8:  *(uint8_t*)ptr = (uint8_t)v; break;
                case InteropScalarKind::UINT16: store_unaligned<uint16_t>(ptr, (uint16_t)v); break;
                case InteropScalarKind::UINT32: store_unaligned<uint32_t>(ptr, (uint32_t)v); break;
                case InteropScalarKind::UINT64: store_unaligned<uint64_t>(ptr, (uint64_t)v); break;
                default: break;
            }
            return true;
        }
        case InteropScalarKind::FLOAT32:
        case InteropScalarKind::FLOAT64: {
            if (value.type != VAL_FLOAT64 && value.type != VAL_INT64 && value.type != VAL_UINT64) {
                state->error("expected numeric value for floating-point field");
                return false;
            }
            double v = value.type == VAL_FLOAT64 ? value.as.double_val
                      : value.type == VAL_UINT64 ? (double)value.as.u64
                                                 : (double)value.as.i64;
            if (field.scalar == InteropScalarKind::FLOAT32) {
                store_unaligned<float>(ptr, (float)v);
            } else {
                store_unaligned<double>(ptr, v);
            }
            return true;
        }
        case InteropScalarKind::BOOL8: {
            bool b = false;
            if (!value_to_boolish(value, &b)) {
                state->error("expected bool-compatible value for bool field");
                return false;
            }
            *ptr = b ? 1 : 0;
            return true;
        }
    }
    return false;
}

static Value read_field_value(MobiusState* state, const StructFieldDesc& field,
                              BufferValue* buffer, size_t base_offset) {
    size_t absolute = base_offset + field.offset;
    if (!ensure_view_span(state, buffer, absolute, field.size)) return Value();
    const uint8_t* ptr = buffer->data() + absolute;
    if (field.count > 1) {
        return make_struct_array_view_value(state, field, buffer, absolute);
    }
    if (field.kind == StructFieldKind::SCALAR) {
        return read_scalar_value(field, ptr);
    }
    return make_struct_view_value(state, field.nested_layout, buffer, absolute);
}

static bool write_field_value(MobiusState* state, const StructFieldDesc& field,
                              BufferValue* buffer, size_t base_offset, const Value& value) {
    if (field.count > 1) {
        state->error("array fields must be assigned through indexing");
        return false;
    }
    if (field.kind != StructFieldKind::SCALAR) {
        state->error("nested struct fields are read-only views");
        return false;
    }
    size_t absolute = base_offset + field.offset;
    if (!ensure_view_span(state, buffer, absolute, field.size)) return false;
    return write_scalar_value(state, field, buffer->data() + absolute, value);
}

static int layout_method_index(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("__index on struct layout expects key");
    StructLayout* layout = require_layout_self(state, "__index on struct layout requires layout self");
    if (!layout) return -1;

    Value key = state->npop();
    state->npop();
    if (key.type != VAL_STRING || !key.as.string) {
        state->npush(make_nil_value());
        return 1;
    }

    if (strcmp(key.as.string->data, "size") == 0) {
        state->npush(make_int64_value((int64_t)layout->size));
        return 1;
    }
    if (strcmp(key.as.string->data, "align") == 0) {
        state->npush(make_int64_value((int64_t)layout->align));
        return 1;
    }
    if (strcmp(key.as.string->data, "name") == 0) {
        state->npush(make_string_value(state->stringPool()->intern(layout->name.c_str())));
        return 1;
    }
    if (strcmp(key.as.string->data, "packed") == 0) {
        state->npush(make_bool_value(layout->packed));
        return 1;
    }

    state->npush(make_nil_value());
    return 1;
}

static int view_method_index(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("__index on struct view expects key");
    StructView* view = require_view_self(state, "__index on struct view requires struct view self");
    if (!view) return -1;

    Value key = state->npop();
    state->npop();
    if (key.type != VAL_STRING || !key.as.string) {
        state->npush(make_nil_value());
        return 1;
    }

    auto it = view->layout->field_lookup.find(key.as.string);
    if (it == view->layout->field_lookup.end()) {
        if (strcmp(key.as.string->data, "size") == 0) {
            state->npush(make_int64_value((int64_t)view->layout->size));
            return 1;
        }
        if (strcmp(key.as.string->data, "offset") == 0) {
            state->npush(make_int64_value((int64_t)view->base_offset));
            return 1;
        }
        if (strcmp(key.as.string->data, "buffer") == 0) {
            view->buffer->retain();
            state->npush(make_buffer_value(view->buffer));
            return 1;
        }
        if (strcmp(key.as.string->data, "layout") == 0) {
            view->layout->retain();
            state->npush(make_userdata_value(state, view->layout, destroy_struct_layout_userdata,
                                             STRUCT_LAYOUT_TYPE, sizeof(StructLayout)));
            return 1;
        }
        state->npush(make_nil_value());
        return 1;
    }

    state->npush(read_field_value(state, view->layout->fields[it->second], view->buffer, view->base_offset));
    return 1;
}

static int view_method_newindex(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("__newindex on struct view expects key and value");
    StructView* view = require_view_self(state, "__newindex on struct view requires struct view self");
    if (!view) return -1;

    Value value = state->npop();
    Value key = state->npop();
    state->npop();
    if (key.type != VAL_STRING || !key.as.string) {
        return state->error("struct field assignment expects a string field name");
    }

    auto it = view->layout->field_lookup.find(key.as.string);
    if (it == view->layout->field_lookup.end()) {
        return state->error("unknown struct field");
    }
    return write_field_value(state, view->layout->fields[it->second], view->buffer, view->base_offset, value) ? 0 : -1;
}

static int array_view_method_index(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("__index on struct array view expects key");
    StructArrayView* view = require_array_view_self(state, "__index on struct array view requires self");
    if (!view) return -1;

    Value key = state->npop();
    state->npop();
    if (key.type == VAL_STRING && key.as.string) {
        if (strcmp(key.as.string->data, "length") == 0) {
            state->npush(make_int64_value((int64_t)view->count));
            return 1;
        }
        if (strcmp(key.as.string->data, "offset") == 0) {
            state->npush(make_int64_value((int64_t)view->base_offset));
            return 1;
        }
        if (strcmp(key.as.string->data, "buffer") == 0) {
            view->buffer->retain();
            state->npush(make_buffer_value(view->buffer));
            return 1;
        }
        if (strcmp(key.as.string->data, "layout") == 0 && view->nested_layout) {
            view->nested_layout->retain();
            state->npush(make_userdata_value(state, view->nested_layout, destroy_struct_layout_userdata,
                                             STRUCT_LAYOUT_TYPE, sizeof(StructLayout)));
            return 1;
        }
    }

    size_t idx = 0;
    if (!value_to_size(key, &idx) || idx >= view->count) {
        return state->error("struct array index out of bounds");
    }

    StructFieldDesc field = {};
    field.kind = view->kind;
    field.scalar = view->scalar;
    field.nested_layout = view->nested_layout;
    field.offset = idx * view->stride;
    field.size = view->kind == StructFieldKind::SCALAR ? view->stride : view->nested_layout->size;
    field.align = 1;
    field.stride = view->stride;
    field.count = 1;

    state->npush(read_field_value(state, field, view->buffer, view->base_offset));
    return 1;
}

static int array_view_method_newindex(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("__newindex on struct array view expects key and value");
    StructArrayView* view = require_array_view_self(state, "__newindex on struct array view requires self");
    if (!view) return -1;

    Value value = state->npop();
    Value key = state->npop();
    state->npop();

    size_t idx = 0;
    if (!value_to_size(key, &idx) || idx >= view->count) {
        return state->error("struct array index out of bounds");
    }
    if (view->kind != StructFieldKind::SCALAR) {
        return state->error("nested struct arrays are read-only views");
    }

    StructFieldDesc field = {};
    field.kind = view->kind;
    field.scalar = view->scalar;
    field.offset = idx * view->stride;
    field.size = view->stride;
    field.stride = view->stride;
    field.count = 1;
    return write_field_value(state, field, view->buffer, view->base_offset, value) ? 0 : -1;
}

static void destroy_struct_layout_userdata(void* ptr) {
    StructLayout* layout = static_cast<StructLayout*>(ptr);
    if (layout) layout->release();
}

static void destroy_struct_view_userdata(void* ptr) {
    StructView* view = static_cast<StructView*>(ptr);
    if (!view) return;
    if (view->layout) view->layout->release();
    if (view->buffer) view->buffer->release();
    delete view;
}

static void destroy_struct_array_view_userdata(void* ptr) {
    StructArrayView* view = static_cast<StructArrayView*>(ptr);
    if (!view) return;
    if (view->nested_layout) view->nested_layout->release();
    if (view->buffer) view->buffer->release();
    delete view;
}

static Value make_struct_view_value(MobiusState* state, StructLayout* layout,
                                    BufferValue* buffer, size_t base_offset) {
    StructView* view = new (std::nothrow) StructView();
    if (!view) {
        state->error("failed to allocate struct view");
        return Value();
    }
    view->layout = layout;
    view->buffer = buffer;
    view->base_offset = base_offset;
    layout->retain();
    buffer->retain();
    return make_userdata_value(state, view, destroy_struct_view_userdata, STRUCT_VIEW_TYPE, sizeof(StructView));
}

static Value make_struct_array_view_value(MobiusState* state, const StructFieldDesc& field,
                                          BufferValue* buffer, size_t base_offset) {
    StructArrayView* view = new (std::nothrow) StructArrayView();
    if (!view) {
        state->error("failed to allocate struct array view");
        return Value();
    }
    view->nested_layout = field.nested_layout;
    view->buffer = buffer;
    view->scalar = field.scalar;
    view->kind = field.kind;
    view->base_offset = base_offset;
    view->stride = field.stride;
    view->count = field.count;
    if (view->nested_layout) view->nested_layout->retain();
    buffer->retain();
    return make_userdata_value(state, view, destroy_struct_array_view_userdata,
                               STRUCT_ARRAY_VIEW_TYPE, sizeof(StructArrayView));
}

static bool resolve_type_info(MobiusState* /*state*/, const Value& type_value,
                              TypeInfo* out, std::string* error) {
    if (type_value.type == VAL_STRING && type_value.as.string) {
        InteropScalarKind kind;
        size_t size = 0;
        size_t align = 1;
        if (!resolve_builtin_scalar(type_value.as.string, &kind, &size, &align)) {
            *error = std::string("unknown scalar type '") + type_value.as.string->data + "'";
            return false;
        }
        out->kind = StructFieldKind::SCALAR;
        out->scalar = kind;
        out->size = size;
        out->align = align;
        return true;
    }

    StructLayout* nested = nullptr;
    if (extract_layout_userdata(type_value, &nested)) {
        out->kind = StructFieldKind::LAYOUT;
        out->nested_layout = nested;
        out->size = nested->size;
        out->align = nested->align;
        return true;
    }

    *error = "field type must be a builtin scalar or another struct layout";
    return false;
}

static bool build_layout_from_member_array(MobiusState* state, ArrayValue* members,
                                           bool packed, bool is_union,
                                           LayoutBuildResult* out,
                                           std::string* error);

static bool build_field_from_spec(MobiusState* state, Table* spec, bool packed, bool is_union,
                                  LayoutBuildResult* out, size_t* cursor, std::string* error) {
    StringInternPool* pool = state->stringPool();
    Value name_value = spec->getByString(pool->intern("name"));
    Value type_value = spec->getByString(pool->intern("type"));
    Value count_value = spec->getByString(pool->intern("count"));
    Value offset_value = spec->getByString(pool->intern("offset"));

    if (name_value.type != VAL_STRING || !name_value.as.string) {
        *error = "struct field is missing a string name";
        return false;
    }

    TypeInfo type_info = {};
    if (!resolve_type_info(state, type_value, &type_info, error)) return false;

    size_t count = 1;
    if (count_value.type != VAL_NIL && !value_to_size(count_value, &count)) {
        *error = "struct field count must be a positive integer";
        return false;
    }
    if (count == 0) {
        *error = "struct field count must be positive";
        return false;
    }

    size_t align = packed ? 1 : type_info.align;
    size_t offset = 0;
    if (offset_value.type != VAL_NIL) {
        if (!value_to_size(offset_value, &offset)) {
            *error = "struct field offset must be a non-negative integer";
            return false;
        }
    } else if (is_union) {
        offset = 0;
    } else {
        offset = packed ? *cursor : align_up(*cursor, align);
    }

    StructFieldDesc field = {};
    field.name = name_value.as.string;
    field.kind = type_info.kind;
    field.scalar = type_info.scalar;
    field.nested_layout = type_info.nested_layout;
    field.offset = offset;
    field.align = align;
    field.count = count;
    field.stride = type_info.size;
    field.size = type_info.size * count;
    if (field.nested_layout) field.nested_layout->retain();

    out->fields.push_back(field);
    out->align = std::max(out->align, align);
    out->size = std::max(out->size, offset + field.size);
    if (!is_union) *cursor = std::max(*cursor, offset + field.size);
    return true;
}

static bool build_layout_from_member_array(MobiusState* state, ArrayValue* members,
                                           bool packed, bool is_union,
                                           LayoutBuildResult* out,
                                           std::string* error) {
    size_t cursor = 0;
    for (size_t i = 0; i < members->length(); i++) {
        Value entry = members->get(i);
        if (entry.type != VAL_TABLE || !entry.as.table) {
            *error = "struct member spec must be a table";
            return false;
        }
        Table* spec = entry.as.table;
        Value kind = spec->getByString(state->stringPool()->intern("kind"));
        if (kind.type != VAL_STRING || !kind.as.string) {
            *error = "struct member is missing kind";
            return false;
        }

        if (strcmp(kind.as.string->data, "field") == 0) {
            if (!build_field_from_spec(state, spec, packed, is_union, out, &cursor, error)) return false;
            continue;
        }

        if (strcmp(kind.as.string->data, "union") == 0 ||
            strcmp(kind.as.string->data, "struct") == 0) {
            Value child_members = spec->getByString(state->stringPool()->intern("members"));
            if (child_members.type != VAL_ARRAY || !child_members.as.array) {
                *error = "group member requires an array of child members";
                return false;
            }

            bool child_is_union = strcmp(kind.as.string->data, "union") == 0;
            LayoutBuildResult group_result = {};
            if (!build_layout_from_member_array(state, child_members.as.array, packed, child_is_union, &group_result, error)) {
                return false;
            }

            size_t group_offset = is_union ? 0 : (packed ? cursor : align_up(cursor, group_result.align));
            for (StructFieldDesc& field : group_result.fields) {
                field.offset += group_offset;
                out->fields.push_back(field);
            }
            out->size = std::max(out->size, group_offset + group_result.size);
            out->align = std::max(out->align, group_result.align);
            if (!is_union) cursor = std::max(cursor, group_offset + group_result.size);
            continue;
        }

        *error = "unknown struct member kind";
        return false;
    }

    if (!packed) out->size = align_up(out->size, out->align);
    return true;
}

} // namespace

int lib_define_struct(MobiusState* state, int arg_count) {
    if (arg_count != 2) {
        return state->error("__define_struct expects name and spec");
    }

    Value spec_value = state->npop();
    Value name_value = state->npop();

    if (name_value.type != VAL_STRING || !name_value.as.string) {
        return state->error("__define_struct name must be a string");
    }
    if (spec_value.type != VAL_TABLE || !spec_value.as.table) {
        return state->error("__define_struct spec must be a table");
    }

    Table* spec = spec_value.as.table;
    StringInternPool* pool = state->stringPool();
    Value layout_value = spec->getByString(pool->intern("layout"));
    Value members_value = spec->getByString(pool->intern("members"));
    if (layout_value.type != VAL_STRING || !layout_value.as.string) {
        return state->error("__define_struct spec.layout must be 'native' or 'packed'");
    }
    if (members_value.type != VAL_ARRAY || !members_value.as.array) {
        return state->error("__define_struct spec.members must be an array");
    }

    bool packed = strcmp(layout_value.as.string->data, "packed") == 0;
    if (!packed && strcmp(layout_value.as.string->data, "native") != 0) {
        return state->error("__define_struct layout must be 'native' or 'packed'");
    }

    LayoutBuildResult result = {};
    std::string error;
    if (!build_layout_from_member_array(state, members_value.as.array, packed, false, &result, &error)) {
        for (StructFieldDesc& field : result.fields) {
            if (field.nested_layout) field.nested_layout->release();
        }
        return state->error(error.c_str());
    }

    StructLayout* layout = new (std::nothrow) StructLayout();
    if (!layout) {
        for (StructFieldDesc& field : result.fields) {
            if (field.nested_layout) field.nested_layout->release();
        }
        return state->error("failed to allocate struct layout");
    }

    layout->name = name_value.as.string->data;
    layout->packed = packed;
    layout->size = result.size;
    layout->align = result.align;
    layout->fields = std::move(result.fields);
    for (size_t i = 0; i < layout->fields.size(); i++) {
        const StructFieldDesc& field = layout->fields[i];
        if (layout->field_lookup.find(field.name) != layout->field_lookup.end()) {
            layout->release();
            return state->error("duplicate field name in struct layout");
        }
        layout->field_lookup[field.name] = i;
    }

    state->npush(make_userdata_value(state, layout, destroy_struct_layout_userdata,
                                     STRUCT_LAYOUT_TYPE, sizeof(StructLayout)));
    return 1;
}

int buffer_method_view_as(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return state->error("buffer:view_as expects layout [, offset]");
    }
    BufferValue* buffer = extract_buffer_self_or_shared(state, "buffer:view_as: self is not a buffer");
    if (!buffer) return -1;

    Value offset_value = make_int64_value(0);
    if (arg_count == 3) offset_value = state->npop();
    Value layout_value = state->npop();
    state->npop();

    StructLayout* layout = extract_layout_arg(state, layout_value, "buffer:view_as expects a struct layout");
    if (!layout) return -1;

    size_t offset = 0;
    if (!value_to_size(offset_value, &offset)) {
        return state->error("buffer:view_as offset must be a non-negative integer");
    }
    if (!ensure_view_span(state, buffer, offset, layout->size)) return -1;
    state->npush(make_struct_view_value(state, layout, buffer, offset));
    return 1;
}

int buffer_method_array_view_as(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 4) {
        return state->error("buffer:array_view_as expects layout [, offset [, count]]");
    }
    BufferValue* buffer = extract_buffer_self_or_shared(state, "buffer:array_view_as: self is not a buffer");
    if (!buffer) return -1;

    Value count_value = make_nil_value();
    if (arg_count == 4) count_value = state->npop();
    Value offset_value = make_int64_value(0);
    if (arg_count >= 3) offset_value = state->npop();
    Value layout_value = state->npop();
    state->npop();

    StructLayout* layout = extract_layout_arg(state, layout_value, "buffer:array_view_as expects a struct layout");
    if (!layout) return -1;

    size_t offset = 0;
    if (!value_to_size(offset_value, &offset)) {
        return state->error("buffer:array_view_as offset must be a non-negative integer");
    }
    if (offset > buffer->size()) {
        return state->error("buffer:array_view_as offset is out of bounds");
    }

    if (layout->size == 0) {
        return state->error("buffer:array_view_as cannot create views for zero-sized layouts");
    }
    size_t available = buffer->size() - offset;
    size_t count = available / layout->size;
    if (count_value.type != VAL_NIL) {
        if (!value_to_size(count_value, &count)) {
            return state->error("buffer:array_view_as count must be a non-negative integer");
        }
    }
    size_t needed = count * layout->size;
    if (needed > available) {
        return state->error("buffer:array_view_as view exceeds buffer bounds");
    }

    StructFieldDesc field = {};
    field.kind = StructFieldKind::LAYOUT;
    field.nested_layout = layout;
    field.size = layout->size;
    field.align = layout->align;
    field.stride = layout->size;
    field.count = count;
    state->npush(make_struct_array_view_value(state, field, buffer, offset));
    return 1;
}

Table* create_struct_layout_metatable(MobiusState* state) {
    Table* mt = new (std::nothrow) Table(state, 1);
    if (!mt) return nullptr;
    mt->setByString(state->stringPool()->intern("__index"), make_native_function_value(layout_method_index));
    return mt;
}

Table* create_struct_view_metatable(MobiusState* state) {
    Table* mt = new (std::nothrow) Table(state, 2);
    if (!mt) return nullptr;
    mt->setByString(state->stringPool()->intern("__index"), make_native_function_value(view_method_index));
    mt->setByString(state->stringPool()->intern("__newindex"), make_native_function_value(view_method_newindex));
    return mt;
}

Table* create_struct_array_view_metatable(MobiusState* state) {
    Table* mt = new (std::nothrow) Table(state, 2);
    if (!mt) return nullptr;
    mt->setByString(state->stringPool()->intern("__index"), make_native_function_value(array_view_method_index));
    mt->setByString(state->stringPool()->intern("__newindex"), make_native_function_value(array_view_method_newindex));
    return mt;
}
