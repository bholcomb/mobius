#include "library/buffer_lib.h"
#include "library/struct_view_lib.h"

#include "data/buffer.h"
#include "data/shared_cell.h"
#include "data/table.h"
#include "data/value.h"
#include "state/mobius_state.h"

#include <algorithm>
#include <mutex>
#include <new>
#include <mobius/mobius_plugin.h>

struct BufferSelfAccess {
    Value self_value;
    BufferValue* buffer = nullptr;
    SharedCell* cell = nullptr;
    std::unique_lock<std::recursive_mutex> lock;
};

static BufferValue* extract_buffer_self(MobiusState* state, const char* err_msg,
                                        BufferSelfAccess* access = nullptr) {
    const Value& self = state->npeek_self();
    if (access) access->self_value = self;
    if (self.type == VAL_BUFFER && self.as.buffer) {
        if (access) access->buffer = self.as.buffer;
        return self.as.buffer;
    }
    if (self.type == VAL_SHARED_CELL && self.as.shared_cell) {
        if (access) {
            access->cell = self.as.shared_cell;
            access->lock = std::unique_lock<std::recursive_mutex>(self.as.shared_cell->mutex());
            Value& inner = self.as.shared_cell->unsafeValue();
            if (inner.type == VAL_BUFFER && inner.as.buffer) {
                access->buffer = inner.as.buffer;
                return inner.as.buffer;
            }
        } else {
            std::lock_guard<std::recursive_mutex> lock(self.as.shared_cell->mutex());
            Value& inner = self.as.shared_cell->unsafeValue();
            if (inner.type == VAL_BUFFER && inner.as.buffer) {
                return inner.as.buffer;
            }
        }
    }
    state->error(err_msg);
    return nullptr;
}

static bool value_to_byte(const Value& value, uint8_t* out) {
    if (value.type == VAL_INT64) {
        if (value.as.i64 < 0 || value.as.i64 > 255) return false;
        *out = (uint8_t)value.as.i64;
        return true;
    }
    if (value.type == VAL_UINT64) {
        if (value.as.u64 > 255) return false;
        *out = (uint8_t)value.as.u64;
        return true;
    }
    return false;
}

static BufferValue* make_buffer_copy(size_t size, uint8_t fill = 0, bool fixed = false) {
    BufferValue* buffer = new (std::nothrow) BufferValue(size, fill, fixed, false);
    if (!buffer || !buffer->ok()) {
        if (buffer) buffer->release();
        return nullptr;
    }
    return buffer;
}

int lib_buffer_create(MobiusState* state, int arg_count) {
    if (arg_count < 1 || arg_count > 2) {
        return state->error("buffer_create expects 1 or 2 arguments (size [, fill_byte])");
    }

    Value fill_value = make_int64_value(0);
    if (arg_count == 2) fill_value = state->npop();
    Value size_value = state->npop();

    if (size_value.type != VAL_INT64 || size_value.as.i64 < 0) {
        return state->error("buffer_create size must be a non-negative integer");
    }

    uint8_t fill = 0;
    if (!value_to_byte(fill_value, &fill)) {
        return state->error("buffer_create fill_byte must be an integer in [0, 255]");
    }

    BufferValue* buffer = make_buffer_copy((size_t)size_value.as.i64, fill);
    if (!buffer) return state->error("Failed to create buffer");
    state->npush(make_buffer_value(buffer));
    return 1;
}

int lib_buffer_from_string(MobiusState* state, int arg_count) {
    if (arg_count != 1) {
        return state->error("buffer_from_string expects 1 argument (string)");
    }

    Value string_value = state->npop();
    if (string_value.type != VAL_STRING || !string_value.as.string) {
        return state->error("buffer_from_string expects a string");
    }

    BufferValue* buffer = make_buffer_copy(0);
    if (!buffer) return state->error("Failed to create buffer");
    if (string_value.as.string->length > 0 &&
        !buffer->appendBytes((const uint8_t*)string_value.as.string->data, string_value.as.string->length)) {
        buffer->release();
        return state->error("Failed to initialize buffer");
    }
    state->npush(make_buffer_value(buffer));
    return 1;
}

int buffer_method_get(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("buffer:get expects 1 argument (index)");

    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:get: self is not a buffer", &access);
    if (!buffer) return -1;

    Value index_value = state->npop();
    state->npop();
    if (index_value.type != VAL_INT64) {
        return state->error("buffer:get index must be an integer");
    }

    int64_t index = index_value.as.i64;
    if (index < 0 || index >= (int64_t)buffer->size()) {
        state->npush(make_nil_value());
    } else {
        state->npush(make_int64_value((int64_t)buffer->get((size_t)index)));
    }
    return 1;
}

int buffer_method_set(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("buffer:set expects 2 arguments (index, byte)");

    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:set: self is not a buffer", &access);
    if (!buffer) return -1;

    Value byte_value = state->npop();
    Value index_value = state->npop();
    state->npop();

    if (index_value.type != VAL_INT64 || index_value.as.i64 < 0) {
        return state->error("buffer:set index must be a non-negative integer");
    }

    uint8_t byte = 0;
    if (!value_to_byte(byte_value, &byte)) {
        return state->error("buffer:set byte must be an integer in [0, 255]");
    }

    size_t index = (size_t)index_value.as.i64;
    if (index >= buffer->size()) {
        if (buffer->isFixed()) return state->error("cannot resize fixed buffer");
        if (!buffer->resize(index + 1, 0)) return state->error("failed to grow buffer");
    }
    if (!buffer->set(index, byte)) {
        return state->error("failed to write buffer byte");
    }
    return 0;
}

int buffer_method_length(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("buffer:length expects 0 arguments");
    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:length: self is not a buffer", &access);
    if (!buffer) return -1;
    state->npop();
    state->npush(make_int64_value((int64_t)buffer->size()));
    return 1;
}

int buffer_method_resize(MobiusState* state, int arg_count) {
    if (arg_count < 2 || arg_count > 3) {
        return state->error("buffer:resize expects 1 or 2 arguments (size [, fill_byte])");
    }

    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:resize: self is not a buffer", &access);
    if (!buffer) return -1;

    Value fill_value = make_int64_value(0);
    if (arg_count == 3) fill_value = state->npop();
    Value size_value = state->npop();
    Value self_value = state->npop();

    if (size_value.type != VAL_INT64 || size_value.as.i64 < 0) {
        return state->error("buffer:resize size must be a non-negative integer");
    }

    uint8_t fill = 0;
    if (!value_to_byte(fill_value, &fill)) {
        return state->error("buffer:resize fill_byte must be an integer in [0, 255]");
    }
    if (!buffer->resize((size_t)size_value.as.i64, fill)) {
        return state->error(buffer->isFixed() ? "cannot resize fixed buffer" : "failed to resize buffer");
    }
    state->npush(self_value);
    return 1;
}

int buffer_method_reserve(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("buffer:reserve expects 1 argument (capacity)");

    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:reserve: self is not a buffer", &access);
    if (!buffer) return -1;

    Value capacity_value = state->npop();
    Value self_value = state->npop();
    if (capacity_value.type != VAL_INT64 || capacity_value.as.i64 < 0) {
        return state->error("buffer:reserve capacity must be a non-negative integer");
    }
    if (!buffer->reserve((size_t)capacity_value.as.i64)) {
        return state->error(buffer->isFixed() ? "cannot reserve on fixed buffer" : "failed to reserve buffer");
    }
    state->npush(self_value);
    return 1;
}

int buffer_method_append(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("buffer:append expects 1 argument");

    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:append: self is not a buffer", &access);
    if (!buffer) return -1;

    Value append_value = state->npop();
    Value self_value = state->npop();

    if (append_value.type == VAL_STRING && append_value.as.string) {
        if (!buffer->appendBytes((const uint8_t*)append_value.as.string->data,
                                 append_value.as.string->length)) {
            return state->error(buffer->isFixed() ? "cannot append to fixed buffer" : "failed to append string bytes");
        }
    } else if (append_value.type == VAL_BUFFER && append_value.as.buffer) {
        if (!buffer->appendBytes(append_value.as.buffer->data(), append_value.as.buffer->size())) {
            return state->error(buffer->isFixed() ? "cannot append to fixed buffer" : "failed to append buffer bytes");
        }
    } else {
        uint8_t byte = 0;
        if (!value_to_byte(append_value, &byte)) {
            return state->error("buffer:append expects a byte integer, string, or buffer");
        }
        if (!buffer->appendByte(byte)) {
            return state->error(buffer->isFixed() ? "cannot append to fixed buffer" : "failed to append byte");
        }
    }

    state->npush(self_value);
    return 1;
}

int buffer_method_copy(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("buffer:copy expects 0 arguments");
    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:copy: self is not a buffer", &access);
    if (!buffer) return -1;
    state->npop();
    BufferValue* copy = buffer->clone();
    if (!copy) return state->error("failed to copy buffer");
    state->npush(make_buffer_value(copy));
    return 1;
}

int buffer_method_slice(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("buffer:slice expects 2 arguments (start, length)");

    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:slice: self is not a buffer", &access);
    if (!buffer) return -1;

    Value length_value = state->npop();
    Value start_value = state->npop();
    state->npop();

    if (start_value.type != VAL_INT64 || length_value.type != VAL_INT64) {
        return state->error("buffer:slice start and length must be integers");
    }

    int64_t start = std::max<int64_t>(0, start_value.as.i64);
    int64_t length = std::max<int64_t>(0, length_value.as.i64);
    if (start >= (int64_t)buffer->size() || length == 0) {
        BufferValue* empty = make_buffer_copy(0);
        if (!empty) return state->error("failed to create buffer slice");
        state->npush(make_buffer_value(empty));
        return 1;
    }

    size_t actual_start = (size_t)start;
    size_t actual_length = std::min<size_t>((size_t)length, buffer->size() - actual_start);
    BufferValue* slice = make_buffer_copy(0);
    if (!slice) return state->error("failed to create buffer slice");
    if (!slice->appendBytes(buffer->data() + actual_start, actual_length)) {
        slice->release();
        return state->error("failed to initialize buffer slice");
    }
    state->npush(make_buffer_value(slice));
    return 1;
}

int buffer_method_to_string(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("buffer:to_string expects 0 arguments");
    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:to_string: self is not a buffer", &access);
    if (!buffer) return -1;
    state->npop();
    const char* data = buffer->size() > 0 ? (const char*)buffer->data() : "";
    state->npush(make_string_value(state->stringPool()->intern(data, buffer->size())));
    return 1;
}

int buffer_method_address(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("buffer:address expects 0 arguments");
    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:address: self is not a buffer", &access);
    if (!buffer) return -1;
    state->npop();
    state->npush(make_uint64_value((uint64_t)buffer->address()));
    return 1;
}

int buffer_method_is_fixed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("buffer:is_fixed expects 0 arguments");
    BufferSelfAccess access;
    BufferValue* buffer = extract_buffer_self(state, "buffer:is_fixed: self is not a buffer", &access);
    if (!buffer) return -1;
    state->npop();
    state->npush(make_bool_value(buffer->isFixed()));
    return 1;
}

Table* create_buffer_type_metatable(MobiusState* state) {
    Table* mt = new (std::nothrow) Table(state, 16);
    if (!mt) return nullptr;
    mt->setByString(state->stringPool()->intern("get"),       make_native_function_value(buffer_method_get));
    mt->setByString(state->stringPool()->intern("set"),       make_native_function_value(buffer_method_set));
    mt->setByString(state->stringPool()->intern("length"),    make_native_function_value(buffer_method_length));
    mt->setByString(state->stringPool()->intern("resize"),    make_native_function_value(buffer_method_resize));
    mt->setByString(state->stringPool()->intern("reserve"),   make_native_function_value(buffer_method_reserve));
    mt->setByString(state->stringPool()->intern("append"),    make_native_function_value(buffer_method_append));
    mt->setByString(state->stringPool()->intern("copy"),      make_native_function_value(buffer_method_copy));
    mt->setByString(state->stringPool()->intern("slice"),     make_native_function_value(buffer_method_slice));
    mt->setByString(state->stringPool()->intern("to_string"), make_native_function_value(buffer_method_to_string));
    mt->setByString(state->stringPool()->intern("address"),   make_native_function_value(buffer_method_address));
    mt->setByString(state->stringPool()->intern("is_fixed"),  make_native_function_value(buffer_method_is_fixed));
    mt->setByString(state->stringPool()->intern("view_as"),   make_native_function_value(buffer_method_view_as));
    mt->setByString(state->stringPool()->intern("array_view_as"), make_native_function_value(buffer_method_array_view_as));
    return mt;
}
