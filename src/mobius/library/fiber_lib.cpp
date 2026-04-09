#include "library/fiber_lib.h"
#include "data/channel.h"
#include "data/future.h"
#include "data/array.h"
#include "data/array_slice.h"
#include "data/shared_cell.h"
#include "data/table.h"
#include "data/value.h"
#include "internal/string_intern.h"
#include "state/mobius_state.h"
#include "fiber/job_system.h"
#include "vm/vm.h"

#include <thread>
#include <chrono>

// ============================================================================
// Module-level functions (accessed as fiber.channel, fiber.all, etc.)
// ============================================================================

int lib_fiber_channel(MobiusState* state, int arg_count) {
    size_t capacity = 1;
    if (arg_count >= 1) {
        Value cap_arg = state->npeek(0);
        state->npop();
        if (cap_arg.type == VAL_INT64 && cap_arg.as.i64 > 0) {
            capacity = (size_t)cap_arg.as.i64;
        } else {
            return state->error("fiber.channel: capacity must be a positive integer");
        }
    }
    Channel* ch = new Channel(capacity);
    state->npush(make_channel_value(ch));
    return 1;
}

int lib_fiber_cancel(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber.cancel expects 1 argument (future)");

    Value fut_val = state->npeek(0);
    state->npop();

    if (fut_val.type != VAL_FUTURE || !fut_val.as.future) {
        return state->error("fiber.cancel: argument must be a future");
    }

    fut_val.as.future->cancel();
    state->npush(make_nil_value());
    return 1;
}

int lib_fiber_all(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber.all expects 1 argument (array of futures)");

    Value arr_val = state->npeek(0);
    state->npop();

    if (arr_val.type != VAL_ARRAY || !arr_val.as.array) {
        return state->error("fiber.all: argument must be an array of futures");
    }

    ArrayValue* futures = arr_val.as.array;
    size_t count = futures->length();

    for (size_t i = 0; i < count; i++) {
        const Value& fv = futures->get(i);
        if (fv.type != VAL_FUTURE || !fv.as.future) {
            return state->error("fiber.all: all elements must be futures");
        }
    }

    ArrayValue* results = new ArrayValue(count);
    JobSystem* js = state->jobSystem();

    for (size_t i = 0; i < count; i++) {
        FutureValue* future = futures->get(i).as.future;
        while (!future->isDone()) {
            if (js) js->yieldFiber();
            else std::this_thread::yield();
        }
        if (future->isRejected()) {
            results->release();
            return state->error("fiber.all: one or more fibers failed");
        }
        results->push(future->result());
    }

    state->npush(make_array_value(results));
    return 1;
}

int lib_fiber_any(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber.any expects 1 argument (array of futures)");

    Value arr_val = state->npeek(0);
    state->npop();

    if (arr_val.type != VAL_ARRAY || !arr_val.as.array) {
        return state->error("fiber.any: argument must be an array of futures");
    }

    ArrayValue* futures = arr_val.as.array;
    size_t count = futures->length();
    if (count == 0) {
        state->npush(make_nil_value());
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        const Value& fv = futures->get(i);
        if (fv.type != VAL_FUTURE || !fv.as.future) {
            return state->error("fiber.any: all elements must be futures");
        }
    }

    JobSystem* js = state->jobSystem();
    while (true) {
        for (size_t i = 0; i < count; i++) {
            FutureValue* future = futures->get(i).as.future;
            if (future->isDone()) {
                if (future->isResolved()) {
                    state->npush(future->result());
                    return 1;
                }
            }
        }
        if (js) js->yieldFiber();
        else std::this_thread::yield();
    }
}

int lib_fiber_sleep(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber.sleep expects 1 argument (milliseconds)");

    Value ms_val = state->npeek(0);
    state->npop();

    int64_t ms = 0;
    if (ms_val.type == VAL_INT64) {
        ms = ms_val.as.i64;
    } else if (ms_val.type == VAL_FLOAT64) {
        ms = (int64_t)ms_val.as.double_val;
    } else {
        return state->error("fiber.sleep: argument must be a number (milliseconds)");
    }

    if (ms > 0) {
        MobiusVM* vm = MobiusVM::t_current_vm;
        FutureValue* fut = vm ? vm->future_ : nullptr;
        JobSystem* js = state->jobSystem();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < deadline) {
            if (fut && fut->isCancelled()) {
                return state->error("CancellationError: fiber was cancelled");
            }
            if (js) js->yieldFiber();
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    state->npush(make_nil_value());
    return 1;
}

int lib_fiber_slice(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("fiber.slice expects 3 arguments (array, start, length)");

    Value len_val = state->npeek(0);
    Value start_val = state->npeek(1);
    Value arr_val = state->npeek(2);
    state->npop();
    state->npop();
    state->npop();

    if (start_val.type != VAL_INT64 || len_val.type != VAL_INT64) {
        return state->error("fiber.slice: start and length must be integers");
    }

    int64_t start = start_val.as.i64;
    int64_t length = len_val.as.i64;
    ArrayValue* arr = nullptr;
    SharedCell* owner_cell = nullptr;
    if (arr_val.type == VAL_ARRAY && arr_val.as.array) {
        arr = arr_val.as.array;
    } else if (arr_val.type == VAL_SHARED_CELL && arr_val.as.shared_cell) {
        owner_cell = arr_val.as.shared_cell;
        std::lock_guard<std::recursive_mutex> lock(owner_cell->mutex());
        Value& inner = owner_cell->unsafeValue();
        if (inner.type != VAL_ARRAY || !inner.as.array) {
            return state->error("fiber.slice: first argument must be an array");
        }
        arr = inner.as.array;
    } else {
        return state->error("fiber.slice: first argument must be an array");
    }

    if (start < 0 || length < 0 || (size_t)(start + length) > arr->length()) {
        return state->error("fiber.slice: range out of bounds for array");
    }

    ArraySlice* slice = new ArraySlice(arr, (size_t)start, (size_t)length, owner_cell);
    state->npush(make_array_slice_value(slice));
    return 1;
}

// ============================================================================
// Channel methods (accessed via ':' syntax, e.g. ch:send(42))
// Self is the first argument at registers[base], read via state->npeek_self()
// arg_count includes self, so ch:send(42) has arg_count=2, ch:recv() has arg_count=1
// ============================================================================

static Channel* extract_channel_self(MobiusState* state, const char* err_msg) {
    const Value& self = state->npeek_self();
    if (self.type != VAL_CHANNEL || !self.as.channel) {
        state->error(err_msg);
        return nullptr;
    }
    return self.as.channel;
}

int channel_method_send(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("ch:send expects 1 argument (value)");

    Channel* ch = extract_channel_self(state, "ch:send: self is not a channel");
    if (!ch) return -1;

    Value val = state->npop();
    state->npop();

    // Yield the fiber instead of blocking the OS thread when the channel is full
    JobSystem* js = state->jobSystem();
    while (!ch->trySend(val)) {
        if (ch->isClosed()) {
            state->npush(make_bool_value(false));
            return 1;
        }
        if (js) js->yieldFiber();
        else std::this_thread::yield();
    }
    state->npush(make_bool_value(true));
    return 1;
}

int channel_method_recv(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("ch:recv expects 0 arguments");

    Channel* ch = extract_channel_self(state, "ch:recv: self is not a channel");
    if (!ch) return -1;

    state->npop();

    // Yield the fiber instead of blocking the OS thread when the channel is empty
    JobSystem* js = state->jobSystem();
    Value result;
    while (!ch->tryRecv(result)) {
        if (ch->isClosed()) {
            return state->error("ChannelClosedError: recv on closed and empty channel");
        }
        if (js) js->yieldFiber();
        else std::this_thread::yield();
    }
    state->npush(result);
    return 1;
}

int channel_method_try_send(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("ch:try_send expects 1 argument (value)");

    Channel* ch = extract_channel_self(state, "ch:try_send: self is not a channel");
    if (!ch) return -1;

    Value val = state->npop();
    state->npop();

    bool ok = ch->trySend(val);
    state->npush(make_bool_value(ok));
    return 1;
}

int channel_method_try_recv(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("ch:try_recv expects 0 arguments");

    Channel* ch = extract_channel_self(state, "ch:try_recv: self is not a channel");
    if (!ch) return -1;

    state->npop();

    Value result;
    bool ok = ch->tryRecv(result);

    Table* tbl = new Table(state, 2);
    tbl->setByString(state->stringPool()->intern("ok"), make_bool_value(ok));
    tbl->setByString(state->stringPool()->intern("value"), ok ? result : make_nil_value());
    state->npush(make_table_value(tbl));
    return 1;
}

int channel_method_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("ch:close expects 0 arguments");

    Channel* ch = extract_channel_self(state, "ch:close: self is not a channel");
    if (!ch) return -1;

    ch->close();
    state->npop();
    state->npush(make_nil_value());
    return 1;
}

int channel_method_is_closed(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("ch:is_closed expects 0 arguments");

    Channel* ch = extract_channel_self(state, "ch:is_closed: self is not a channel");
    if (!ch) return -1;

    bool closed = ch->isClosed();
    state->npop();
    state->npush(make_bool_value(closed));
    return 1;
}

// ============================================================================
// Module and type metatable registration
// ============================================================================

Table* register_fiber_module(MobiusState* state) {
    Table* mod = new Table(state, 8);
    mod->setByString(state->stringPool()->intern("channel"), make_native_function_value(lib_fiber_channel));
    mod->setByString(state->stringPool()->intern("all"),     make_native_function_value(lib_fiber_all));
    mod->setByString(state->stringPool()->intern("any"),     make_native_function_value(lib_fiber_any));
    mod->setByString(state->stringPool()->intern("sleep"),   make_native_function_value(lib_fiber_sleep));
    mod->setByString(state->stringPool()->intern("cancel"),  make_native_function_value(lib_fiber_cancel));
    mod->setByString(state->stringPool()->intern("slice"),   make_native_function_value(lib_fiber_slice));
    return mod;
}

Table* create_channel_type_metatable(MobiusState* state) {
    Table* mt = new Table(state, 8);
    mt->setByString(state->stringPool()->intern("send"),      make_native_function_value(channel_method_send));
    mt->setByString(state->stringPool()->intern("recv"),      make_native_function_value(channel_method_recv));
    mt->setByString(state->stringPool()->intern("try_send"),  make_native_function_value(channel_method_try_send));
    mt->setByString(state->stringPool()->intern("try_recv"),  make_native_function_value(channel_method_try_recv));
    mt->setByString(state->stringPool()->intern("close"),     make_native_function_value(channel_method_close));
    mt->setByString(state->stringPool()->intern("is_closed"), make_native_function_value(channel_method_is_closed));
    return mt;
}
