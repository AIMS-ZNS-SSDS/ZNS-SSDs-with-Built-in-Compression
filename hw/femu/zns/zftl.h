#ifndef __FEMU_ZFTL_H
#define __FEMU_ZFTL_H
#define learn_ftl
#include "../nvme.h"
// #include "../alex/src/core/alex.h"
#define INVALID_PPA     (~(0ULL))
#define INVALID_LPN     (~(0ULL))
#define UNMAPPED_PPA    (~(0ULL))

void zftl_init(FemuCtrl *n);

#ifdef FEMU_DEBUG_ZFTL
#define ftl_debug(fmt, ...) \
    do { printf("[Misao] ZFTL-Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ftl_debug(fmt, ...) \
    do { } while (0)
#endif

#define ftl_err(fmt, ...) \
    do { fprintf(stderr, "[Misao] ZFTL-Err: " fmt, ## __VA_ARGS__); } while (0)

#define ftl_log(fmt, ...) \
    do { printf("[Misao] ZFTL-Log: " fmt, ## __VA_ARGS__); } while (0)

#ifdef learn_ftl
typedef struct learned_mgmt {
    segment_param *sp;
    // uint8_t *bitmap;
}learned_mgmt;
typedef struct segment_param {
    int sgement_cnt;
    int page_total_cnt;
    int mapping_per_page;
    int threshold;
    int buffer_len;
}segment_param;

typedef struct simple_segment {
    uint64_t slpa;
    int len;
    uint8_t is_accurate;
    float k;
    uint64_t inter;
    struct simple_segment *next;
    
}simple_segment;

typedef struct segment {
    int segment_no;
    struct simple_segment* segment_head;
    int seg_cnt;
    bool is_valid;
    struct segment* next;
}segment;
#endif
/* FEMU assert() */
#ifdef FEMU_DEBUG_FTL
#define ftl_assert(expression) assert(expression)
#else
#define ftl_assert(expression)
#endif

#endif
