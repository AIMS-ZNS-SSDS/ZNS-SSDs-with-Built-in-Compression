/* This file is autogenerated by tracetool, do not edit. */

#ifndef TRACE_HW_REMOTE_GENERATED_TRACERS_H
#define TRACE_HW_REMOTE_GENERATED_TRACERS_H

#include "trace/control.h"

extern TraceEvent _TRACE_MPQEMU_SEND_IO_ERROR_EVENT;
extern TraceEvent _TRACE_MPQEMU_RECV_IO_ERROR_EVENT;
extern TraceEvent _TRACE_VFU_PROP_EVENT;
extern TraceEvent _TRACE_VFU_CFG_READ_EVENT;
extern TraceEvent _TRACE_VFU_CFG_WRITE_EVENT;
extern TraceEvent _TRACE_VFU_DMA_REGISTER_EVENT;
extern TraceEvent _TRACE_VFU_DMA_UNREGISTER_EVENT;
extern TraceEvent _TRACE_VFU_BAR_REGISTER_EVENT;
extern TraceEvent _TRACE_VFU_BAR_RW_ENTER_EVENT;
extern TraceEvent _TRACE_VFU_BAR_RW_EXIT_EVENT;
extern TraceEvent _TRACE_VFU_INTERRUPT_EVENT;
extern uint16_t _TRACE_MPQEMU_SEND_IO_ERROR_DSTATE;
extern uint16_t _TRACE_MPQEMU_RECV_IO_ERROR_DSTATE;
extern uint16_t _TRACE_VFU_PROP_DSTATE;
extern uint16_t _TRACE_VFU_CFG_READ_DSTATE;
extern uint16_t _TRACE_VFU_CFG_WRITE_DSTATE;
extern uint16_t _TRACE_VFU_DMA_REGISTER_DSTATE;
extern uint16_t _TRACE_VFU_DMA_UNREGISTER_DSTATE;
extern uint16_t _TRACE_VFU_BAR_REGISTER_DSTATE;
extern uint16_t _TRACE_VFU_BAR_RW_ENTER_DSTATE;
extern uint16_t _TRACE_VFU_BAR_RW_EXIT_DSTATE;
extern uint16_t _TRACE_VFU_INTERRUPT_DSTATE;
#define TRACE_MPQEMU_SEND_IO_ERROR_ENABLED 1
#define TRACE_MPQEMU_RECV_IO_ERROR_ENABLED 1
#define TRACE_VFU_PROP_ENABLED 1
#define TRACE_VFU_CFG_READ_ENABLED 1
#define TRACE_VFU_CFG_WRITE_ENABLED 1
#define TRACE_VFU_DMA_REGISTER_ENABLED 1
#define TRACE_VFU_DMA_UNREGISTER_ENABLED 1
#define TRACE_VFU_BAR_REGISTER_ENABLED 1
#define TRACE_VFU_BAR_RW_ENTER_ENABLED 1
#define TRACE_VFU_BAR_RW_EXIT_ENABLED 1
#define TRACE_VFU_INTERRUPT_ENABLED 1
#include "qemu/log-for-trace.h"
#include "qemu/error-report.h"


#define TRACE_MPQEMU_SEND_IO_ERROR_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_MPQEMU_SEND_IO_ERROR) || \
    false)

static inline void _nocheck__trace_mpqemu_send_io_error(int cmd, int size, int nfds)
{
    if (trace_event_get_state(TRACE_MPQEMU_SEND_IO_ERROR) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 3 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:mpqemu_send_io_error " "send command %d size %d, %d file descriptors to remote process" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , cmd, size, nfds);
#line 61 "trace/trace-hw_remote.h"
        } else {
#line 3 "../hw/remote/trace-events"
            qemu_log("mpqemu_send_io_error " "send command %d size %d, %d file descriptors to remote process" "\n", cmd, size, nfds);
#line 65 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_mpqemu_send_io_error(int cmd, int size, int nfds)
{
    if (true) {
        _nocheck__trace_mpqemu_send_io_error(cmd, size, nfds);
    }
}

#define TRACE_MPQEMU_RECV_IO_ERROR_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_MPQEMU_RECV_IO_ERROR) || \
    false)

static inline void _nocheck__trace_mpqemu_recv_io_error(int cmd, int size, int nfds)
{
    if (trace_event_get_state(TRACE_MPQEMU_RECV_IO_ERROR) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 4 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:mpqemu_recv_io_error " "failed to receive %d size %d, %d file descriptors to remote process" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , cmd, size, nfds);
#line 92 "trace/trace-hw_remote.h"
        } else {
#line 4 "../hw/remote/trace-events"
            qemu_log("mpqemu_recv_io_error " "failed to receive %d size %d, %d file descriptors to remote process" "\n", cmd, size, nfds);
#line 96 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_mpqemu_recv_io_error(int cmd, int size, int nfds)
{
    if (true) {
        _nocheck__trace_mpqemu_recv_io_error(cmd, size, nfds);
    }
}

#define TRACE_VFU_PROP_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_PROP) || \
    false)

static inline void _nocheck__trace_vfu_prop(const char * prop, const char * val)
{
    if (trace_event_get_state(TRACE_VFU_PROP) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 7 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_prop " "vfu: setting %s as %s" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , prop, val);
#line 123 "trace/trace-hw_remote.h"
        } else {
#line 7 "../hw/remote/trace-events"
            qemu_log("vfu_prop " "vfu: setting %s as %s" "\n", prop, val);
#line 127 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_prop(const char * prop, const char * val)
{
    if (true) {
        _nocheck__trace_vfu_prop(prop, val);
    }
}

#define TRACE_VFU_CFG_READ_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_CFG_READ) || \
    false)

static inline void _nocheck__trace_vfu_cfg_read(uint32_t offset, uint32_t val)
{
    if (trace_event_get_state(TRACE_VFU_CFG_READ) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 8 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_cfg_read " "vfu: cfg: 0x%x -> 0x%x" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , offset, val);
#line 154 "trace/trace-hw_remote.h"
        } else {
#line 8 "../hw/remote/trace-events"
            qemu_log("vfu_cfg_read " "vfu: cfg: 0x%x -> 0x%x" "\n", offset, val);
#line 158 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_cfg_read(uint32_t offset, uint32_t val)
{
    if (true) {
        _nocheck__trace_vfu_cfg_read(offset, val);
    }
}

#define TRACE_VFU_CFG_WRITE_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_CFG_WRITE) || \
    false)

static inline void _nocheck__trace_vfu_cfg_write(uint32_t offset, uint32_t val)
{
    if (trace_event_get_state(TRACE_VFU_CFG_WRITE) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 9 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_cfg_write " "vfu: cfg: 0x%x <- 0x%x" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , offset, val);
#line 185 "trace/trace-hw_remote.h"
        } else {
#line 9 "../hw/remote/trace-events"
            qemu_log("vfu_cfg_write " "vfu: cfg: 0x%x <- 0x%x" "\n", offset, val);
#line 189 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_cfg_write(uint32_t offset, uint32_t val)
{
    if (true) {
        _nocheck__trace_vfu_cfg_write(offset, val);
    }
}

#define TRACE_VFU_DMA_REGISTER_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_DMA_REGISTER) || \
    false)

static inline void _nocheck__trace_vfu_dma_register(uint64_t gpa, size_t len)
{
    if (trace_event_get_state(TRACE_VFU_DMA_REGISTER) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 10 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_dma_register " "vfu: registering GPA 0x%"PRIx64", %zu bytes" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , gpa, len);
#line 216 "trace/trace-hw_remote.h"
        } else {
#line 10 "../hw/remote/trace-events"
            qemu_log("vfu_dma_register " "vfu: registering GPA 0x%"PRIx64", %zu bytes" "\n", gpa, len);
#line 220 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_dma_register(uint64_t gpa, size_t len)
{
    if (true) {
        _nocheck__trace_vfu_dma_register(gpa, len);
    }
}

#define TRACE_VFU_DMA_UNREGISTER_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_DMA_UNREGISTER) || \
    false)

static inline void _nocheck__trace_vfu_dma_unregister(uint64_t gpa)
{
    if (trace_event_get_state(TRACE_VFU_DMA_UNREGISTER) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 11 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_dma_unregister " "vfu: unregistering GPA 0x%"PRIx64"" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , gpa);
#line 247 "trace/trace-hw_remote.h"
        } else {
#line 11 "../hw/remote/trace-events"
            qemu_log("vfu_dma_unregister " "vfu: unregistering GPA 0x%"PRIx64"" "\n", gpa);
#line 251 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_dma_unregister(uint64_t gpa)
{
    if (true) {
        _nocheck__trace_vfu_dma_unregister(gpa);
    }
}

#define TRACE_VFU_BAR_REGISTER_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_BAR_REGISTER) || \
    false)

static inline void _nocheck__trace_vfu_bar_register(int i, uint64_t addr, uint64_t size)
{
    if (trace_event_get_state(TRACE_VFU_BAR_REGISTER) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 12 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_bar_register " "vfu: BAR %d: addr 0x%"PRIx64" size 0x%"PRIx64"" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , i, addr, size);
#line 278 "trace/trace-hw_remote.h"
        } else {
#line 12 "../hw/remote/trace-events"
            qemu_log("vfu_bar_register " "vfu: BAR %d: addr 0x%"PRIx64" size 0x%"PRIx64"" "\n", i, addr, size);
#line 282 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_bar_register(int i, uint64_t addr, uint64_t size)
{
    if (true) {
        _nocheck__trace_vfu_bar_register(i, addr, size);
    }
}

#define TRACE_VFU_BAR_RW_ENTER_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_BAR_RW_ENTER) || \
    false)

static inline void _nocheck__trace_vfu_bar_rw_enter(const char * op, uint64_t addr)
{
    if (trace_event_get_state(TRACE_VFU_BAR_RW_ENTER) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 13 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_bar_rw_enter " "vfu: %s request for BAR address 0x%"PRIx64"" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , op, addr);
#line 309 "trace/trace-hw_remote.h"
        } else {
#line 13 "../hw/remote/trace-events"
            qemu_log("vfu_bar_rw_enter " "vfu: %s request for BAR address 0x%"PRIx64"" "\n", op, addr);
#line 313 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_bar_rw_enter(const char * op, uint64_t addr)
{
    if (true) {
        _nocheck__trace_vfu_bar_rw_enter(op, addr);
    }
}

#define TRACE_VFU_BAR_RW_EXIT_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_BAR_RW_EXIT) || \
    false)

static inline void _nocheck__trace_vfu_bar_rw_exit(const char * op, uint64_t addr)
{
    if (trace_event_get_state(TRACE_VFU_BAR_RW_EXIT) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 14 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_bar_rw_exit " "vfu: Finished %s of BAR address 0x%"PRIx64"" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , op, addr);
#line 340 "trace/trace-hw_remote.h"
        } else {
#line 14 "../hw/remote/trace-events"
            qemu_log("vfu_bar_rw_exit " "vfu: Finished %s of BAR address 0x%"PRIx64"" "\n", op, addr);
#line 344 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_bar_rw_exit(const char * op, uint64_t addr)
{
    if (true) {
        _nocheck__trace_vfu_bar_rw_exit(op, addr);
    }
}

#define TRACE_VFU_INTERRUPT_BACKEND_DSTATE() ( \
    trace_event_get_state_dynamic_by_id(TRACE_VFU_INTERRUPT) || \
    false)

static inline void _nocheck__trace_vfu_interrupt(int pirq)
{
    if (trace_event_get_state(TRACE_VFU_INTERRUPT) && qemu_loglevel_mask(LOG_TRACE)) {
        if (message_with_timestamp) {
            struct timeval _now;
            gettimeofday(&_now, NULL);
#line 15 "../hw/remote/trace-events"
            qemu_log("%d@%zu.%06zu:vfu_interrupt " "vfu: sending interrupt to device - PIRQ %d" "\n",
                     qemu_get_thread_id(),
                     (size_t)_now.tv_sec, (size_t)_now.tv_usec
                     , pirq);
#line 371 "trace/trace-hw_remote.h"
        } else {
#line 15 "../hw/remote/trace-events"
            qemu_log("vfu_interrupt " "vfu: sending interrupt to device - PIRQ %d" "\n", pirq);
#line 375 "trace/trace-hw_remote.h"
        }
    }
}

static inline void trace_vfu_interrupt(int pirq)
{
    if (true) {
        _nocheck__trace_vfu_interrupt(pirq);
    }
}
#endif /* TRACE_HW_REMOTE_GENERATED_TRACERS_H */
