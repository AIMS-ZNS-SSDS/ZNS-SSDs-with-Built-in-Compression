#ifndef __FEMU_QAT_DC_H
#define __FEMU_QAT_DC_H

#include <assert.h>

#define USER_SPACE
#define DO_CRYPTO
#define SC_ENABLE_DYNAMIC_COMPRESSION

#include "../nvme.h"
// #include "/QAT/quickassist/include/cpa.h"
// #include "/QAT/quickassist/include/dc/cpa_dc_dp.h"
// #include "/QAT/quickassist/lookaside/access_layer/src/sample_code/functional/include/cpa_sample_cnv_utils.h"
// #include "/QAT/quickassist/lookaside/access_layer/src/sample_code/functional/include/cpa_sample_utils.h"
// #include "/QAT/quickassist/lookaside/access_layer/include/icp_sal_poll.h"
// #include "/QAT/quickassist/lookaside/access_layer/include/icp_sal_user.h"
#include "qat/cpa.h"
#include "qat/cpa_dc_dp.h"
#include "qat/cpa_sample_cnv_utils.h"
#include "qat/cpa_sample_utils.h"
#include "qat/icp_sal_poll.h"
#include "qat/icp_sal_user.h"
CpaStatus qat_init(FemuCtrl *n);
void qat_exit(FemuCtrl *n);
CpaStatus qat_dc_compress(FemuCtrl *n, uint32_t inst_idx, void *input, uint32_t input_len, uint32_t *output_len, uint32_t count);
CpaStatus qat_dc_decompress(FemuCtrl *n, uint32_t inst_idx, void *input, uint32_t input_len, uint32_t *output_len, uint32_t count);
#define QAT_OP_PER_INST (128)

// #define FEMU_DEBUG_QAT
#ifdef FEMU_DEBUG_QAT
#define qat_debug(fmt, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        printf("[FEMU] QAT-Debug: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__);                                     \
    } while (0)

#define qat_assert(expr)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        assert(expr);                                                                                                  \
    } while (0)
#else
#define qat_debug(fmt, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)

#define qat_assert(expr)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
    } while (0)
#endif

#define qat_error(fmt, ...)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        fprintf(stderr, "[FEMU] QAT-Error: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__);                            \
    } while (0)

#define qat_log(fmt, ...)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        printf("[FEMU] QAT-Log: %s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__);                                       \
    } while (0)

#endif