#include "library/fiber_lib.h"
#include "data/channel.h"
#include "data/future.h"
#include "data/array.h"
#include "data/array_slice.h"
#include "data/value.h"
#include "state/mobius_state.h"

#include <thread>
#include <chrono>

int lib_fiber_channel(MobiusState* state, int arg_count) {
    size_t capacity = 1;
    if (arg_count >= 1) {
        Value cap_arg = state->npeek(0);
        state->npop();
        if (cap_arg.type == VAL_INT64 && cap_arg.as.i64 > 0) {
            capacity = (size_t)cap_arg.as.i64;
        } else {
            return state->error("fiber_channel: capacity must be a positive integer");
        }
    }
    Channel* ch = new Channel(capacity);
    state->npush(make_channel_value(ch));
    ch->release();
    return 1;
}

int lib_fiber_send(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("fiber_send expects 2 arguments (channel, value)");

    Value val = state->npeek(0);
    Value ch_val = state->npeek(1);
    state->npop();
    state->npop();

    if (ch_val.type != VAL_CHANNEL || !ch_val.as.channel) {
        return state->error("fiber_send: first argument must be a channel");
    }

    bool ok = ch_val.as.channel->send(val);
    state->npush(make_bool_value(ok));
    return 1;
}

int lib_fiber_recv(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber_recv expects 1 argument (channel)");

    Value ch_val = state->npeek(0);
    state->npop();

    if (ch_val.type != VAL_CHANNEL || !ch_val.as.channel) {
        return state->error("fiber_recv: argument must be a channel");
    }

    Value result;
    bool ok = ch_val.as.channel->recv(result);
    if (ok) {
        state->npush(result);
    } else {
        state->npush(make_nil_value());
    }
    return 1;
}

int lib_fiber_try_send(MobiusState* state, int arg_count) {
    if (arg_count != 2) return state->error("fiber_try_send expects 2 arguments (channel, value)");

    Value val = state->npeek(0);
    Value ch_val = state->npeek(1);
    state->npop();
    state->npop();

    if (ch_val.type != VAL_CHANNEL || !ch_val.as.channel) {
        return state->error("fiber_try_send: first argument must be a channel");
    }

    bool ok = ch_val.as.channel->trySend(val);
    state->npush(make_bool_value(ok));
    return 1;
}

int lib_fiber_try_recv(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber_try_recv expects 1 argument (channel)");

    Value ch_val = state->npeek(0);
    state->npop();

    if (ch_val.type != VAL_CHANNEL || !ch_val.as.channel) {
        return state->error("fiber_try_recv: argument must be a channel");
    }

    Value result;
    bool ok = ch_val.as.channel->tryRecv(result);
    if (ok) {
        state->npush(result);
    } else {
        state->npush(make_nil_value());
    }
    return 1;
}

int lib_fiber_close(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber_close expects 1 argument (channel)");

    Value ch_val = state->npeek(0);
    state->npop();

    if (ch_val.type != VAL_CHANNEL || !ch_val.as.channel) {
        return state->error("fiber_close: argument must be a channel");
    }

    ch_val.as.channel->close();
    state->npush(make_nil_value());
    return 1;
}

int lib_fiber_cancel(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber_cancel expects 1 argument (future)");

    Value fut_val = state->npeek(0);
    state->npop();

    if (fut_val.type != VAL_FUTURE || !fut_val.as.future) {
        return state->error("fiber_cancel: argument must be a future");
    }

    fut_val.as.future->cancel();
    state->npush(make_nil_value());
    return 1;
}

int lib_fiber_all(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber_all expects 1 argument (array of futures)");

    Value arr_val = state->npeek(0);
    state->npop();

    if (arr_val.type != VAL_ARRAY || !arr_val.as.array) {
        return state->error("fiber_all: argument must be an array of futures");
    }

    ArrayValue* futures = arr_val.as.array;
    size_t count = futures->length();

    for (size_t i = 0; i < count; i++) {
        const Value& fv = futures->get(i);
        if (fv.type != VAL_FUTURE || !fv.as.future) {
            return state->error("fiber_all: all elements must be futures");
        }
    }

    ArrayValue* results = new ArrayValue(count);

    for (size_t i = 0; i < count; i++) {
        FutureValue* future = futures->get(i).as.future;
        if (!future->isDone()) {
            std::unique_lock<std::mutex> lock(future->mutex());
            future->cv().wait(lock, [future]() { return future->isDone(); });
        }
        if (future->isRejected()) {
            results->release();
            return state->error("fiber_all: one or more fibers failed");
        }
        results->push(future->result());
    }

    state->npush(make_array_value(results));
    return 1;
}

int lib_fiber_any(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber_any expects 1 argument (array of futures)");

    Value arr_val = state->npeek(0);
    state->npop();

    if (arr_val.type != VAL_ARRAY || !arr_val.as.array) {
        return state->error("fiber_any: argument must be an array of futures");
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
            return state->error("fiber_any: all elements must be futures");
        }
    }

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
        std::this_thread::yield();
    }
}

int lib_fiber_sleep(MobiusState* state, int arg_count) {
    if (arg_count != 1) return state->error("fiber_sleep expects 1 argument (milliseconds)");

    Value ms_val = state->npeek(0);
    state->npop();

    int64_t ms = 0;
    if (ms_val.type == VAL_INT64) {
        ms = ms_val.as.i64;
    } else if (ms_val.type == VAL_FLOAT64) {
        ms = (int64_t)ms_val.as.double_val;
    } else {
        return state->error("fiber_sleep: argument must be a number (milliseconds)");
    }

    if (ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    state->npush(make_nil_value());
    return 1;
}

int lib_fiber_slice(MobiusState* state, int arg_count) {
    if (arg_count != 3) return state->error("fiber_slice expects 3 arguments (array, start, length)");

    Value len_val = state->npeek(0);
    Value start_val = state->npeek(1);
    Value arr_val = state->npeek(2);
    state->npop();
    state->npop();
    state->npop();

    if (arr_val.type != VAL_ARRAY || !arr_val.as.array) {
        return state->error("fiber_slice: first argument must be an array");
    }
    if (start_val.type != VAL_INT64 || len_val.type != VAL_INT64) {
        return state->error("fiber_slice: start and length must be integers");
    }

    int64_t start = start_val.as.i64;
    int64_t length = len_val.as.i64;
    ArrayValue* arr = arr_val.as.array;

    if (start < 0 || length < 0 || (size_t)(start + length) > arr->length()) {
        return state->error("fiber_slice: range out of bounds for array");
    }

    ArraySlice* slice = new ArraySlice(arr, (size_t)start, (size_t)length);
    state->npush(make_array_slice_value(slice));
    slice->release();
    return 1;
}
