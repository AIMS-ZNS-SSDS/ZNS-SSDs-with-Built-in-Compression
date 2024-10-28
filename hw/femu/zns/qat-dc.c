#include "qat-dc.h"

#define DC_MAX_BUFF 8192
#define MAX_INSTANCES 1024

static void dcDpCallback(CpaDcDpOpData *pOpData)
{
    qatomic_dec((uint32_t *)pOpData->pCallbackTag);
}

CpaPhysicalAddr virtAddrToDevAddr(void *pVirtAddr, CpaInstanceHandle instanceHandle, CpaAccelerationServiceType type)
{
    CpaStatus status;
    CpaInstanceInfo2 instanceInfo = {0};

    /* Get the address translation mode */
    switch (type)
    {
#ifdef DO_CRYPTO
    case CPA_ACC_SVC_TYPE_CRYPTO:
        status = cpaCyInstanceGetInfo2(instanceHandle, &instanceInfo);
        break;
#endif
    case CPA_ACC_SVC_TYPE_DATA_COMPRESSION:
        status = cpaDcInstanceGetInfo2(instanceHandle, &instanceInfo);
        break;
    default:
        status = CPA_STATUS_UNSUPPORTED;
    }

    if (CPA_STATUS_SUCCESS != status)
    {
        return (CpaPhysicalAddr)(uintptr_t)NULL;
    }

    if (instanceInfo.requiresPhysicallyContiguousMemory)
    {
        return sampleVirtToPhys(pVirtAddr);
    }
    else
    {
        return (CpaPhysicalAddr)(uintptr_t)pVirtAddr;
    }
}

static CpaStatus qat_alloc_buffers(FemuCtrl *n)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaInstanceHandle *dcInstHandles = n->dc_inst_handles;
    CpaDcSessionHandle *sessionHdls = n->dc_session_handles;
    uint32_t *dc_inflight_ops = n->dc_inflight_ops;
    Cpa8U **pSrcBuffers = malloc(sizeof(Cpa8U *) * n->dc_inst_num * QAT_OP_PER_INST);
    Cpa8U **pDstBuffers = malloc(sizeof(Cpa8U *) * n->dc_inst_num * QAT_OP_PER_INST);
    CpaDcDpOpData **pOpDatas = malloc(sizeof(CpaDcDpOpData *) * n->dc_inst_num * QAT_OP_PER_INST);

    for (uint32_t op_idx = 0; op_idx < n->dc_inst_num * QAT_OP_PER_INST; op_idx++)
    {
        status = PHYS_CONTIG_ALLOC(&pSrcBuffers[op_idx], DC_MAX_BUFF);
        if (CPA_STATUS_SUCCESS == status)
        {
            status = PHYS_CONTIG_ALLOC(&pDstBuffers[op_idx], DC_MAX_BUFF);
        }
        if (CPA_STATUS_SUCCESS == status)
        {
            status = PHYS_CONTIG_ALLOC_ALIGNED(&pOpDatas[op_idx], sizeof(CpaDcDpOpData), 8);
        }

        uint32_t inst_idx = op_idx / QAT_OP_PER_INST;
        memset(pOpDatas[op_idx], 0, sizeof(CpaDcDpOpData));
        pOpDatas[op_idx]->bufferLenForData = DC_MAX_BUFF;
        pOpDatas[op_idx]->dcInstance = dcInstHandles[inst_idx];
        pOpDatas[op_idx]->pSessionHandle = sessionHdls[inst_idx];
        pOpDatas[op_idx]->srcBuffer =
            virtAddrToDevAddr((void *)pSrcBuffers[op_idx], dcInstHandles[inst_idx], CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        pOpDatas[op_idx]->srcBufferLen = DC_MAX_BUFF;
        pOpDatas[op_idx]->destBuffer =
            virtAddrToDevAddr((void *)pDstBuffers[op_idx], dcInstHandles[inst_idx], CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        pOpDatas[op_idx]->destBufferLen = DC_MAX_BUFF;
        pOpDatas[op_idx]->thisPhys =
            virtAddrToDevAddr((void *)pOpDatas[op_idx], dcInstHandles[inst_idx], CPA_ACC_SVC_TYPE_DATA_COMPRESSION);
        pOpDatas[op_idx]->pCallbackTag = dc_inflight_ops + inst_idx;
    }

    n->dc_src_buffers = pSrcBuffers;
    n->dc_dst_buffers = pDstBuffers;
    n->dc_op_datas = pOpDatas;

    return status;
}

static void qat_free_buffers(FemuCtrl *n)
{
    Cpa8U **pSrcBuffers = n->dc_src_buffers;
    Cpa8U **pDstBuffers = n->dc_dst_buffers;
    CpaDcDpOpData **pOpDatas = n->dc_op_datas;
    for (uint32_t inst_idx = 0; inst_idx < n->dc_inst_num; inst_idx++)
    {
        PHYS_CONTIG_FREE(pSrcBuffers[inst_idx]);
        PHYS_CONTIG_FREE(pDstBuffers[inst_idx]);
        PHYS_CONTIG_FREE(pOpDatas[inst_idx]);
    }
    free(n->dc_src_buffers);
    free(n->dc_dst_buffers);
    free(n->dc_op_datas);
}

static CpaStatus qat_dc_init(FemuCtrl *n)
{
    CpaStatus status = CPA_STATUS_SUCCESS;
    CpaInstanceHandle *dcInstHandles = NULL;
    CpaDcSessionHandle *sessionHdls = NULL;
    uint32_t *dc_inflight_ops = NULL;

    // 获取压缩实例
    status = cpaDcGetNumInstances(&n->dc_inst_num);
    if ((status == CPA_STATUS_SUCCESS) && (n->dc_inst_num > 0))
    {
        dcInstHandles = malloc(sizeof(CpaInstanceHandle) * n->dc_inst_num);
        sessionHdls = malloc(sizeof(CpaDcSessionHandle) * n->dc_inst_num);
        dc_inflight_ops = malloc(sizeof(uint32_t) * n->dc_inst_num);
        status = cpaDcGetInstances(n->dc_inst_num, dcInstHandles);
    }

    for (uint32_t inst_idx = 0; inst_idx < n->dc_inst_num; inst_idx++)
    {
        if (NULL == dcInstHandles[inst_idx])
        {
            qat_error("Error: Failed to get instance %d\n", inst_idx);
            goto err;
        }
    }
    qat_debug("[Success] Get %d instances\n", n->dc_inst_num);

    for (uint32_t inst_idx = 0; inst_idx < n->dc_inst_num; inst_idx++)
    {
        CpaDcInstanceCapabilities cap = {0};
        CpaDcSessionSetupData sd = {0};
        Cpa32U sess_size = 0;
        Cpa32U ctx_size = 0;

        qatomic_set(dc_inflight_ops + inst_idx, 0);

        // 查询压缩实例的功能
        status = cpaDcQueryCapabilities(dcInstHandles[inst_idx], &cap);
        if (status != CPA_STATUS_SUCCESS)
        {
            qat_error("[Failed] query capabilities\n");
            goto err;
        }

        if (!cap.statelessDeflateCompression || !cap.statelessDeflateDecompression)
        {
            qat_error("[Error] Unsupported functionality\n");
            goto err;
        }

        // 设置地址转换函数
        status = cpaDcSetAddressTranslation(dcInstHandles[inst_idx], sampleVirtToPhys);

        // 启动压缩实例
        if (CPA_STATUS_SUCCESS == status)
        {
            status = cpaDcStartInstance(dcInstHandles[inst_idx], 0, NULL);
        }

        if (CPA_STATUS_SUCCESS == status)
        {
            qat_debug("[Success] Start data compression instance[%d]\n", inst_idx);
            // 注册回调函数
            status = cpaDcDpRegCbFunc(dcInstHandles[inst_idx], dcDpCallback);
        }

        // 设置会话参数
        if (CPA_STATUS_SUCCESS == status)
        {
            sd.compLevel = CPA_DC_L1;
            sd.compType = CPA_DC_DEFLATE;
            sd.huffType = CPA_DC_HT_STATIC;
            sd.autoSelectBestHuffmanTree = CPA_DC_ASB_DISABLED;
            sd.sessDirection = CPA_DC_DIR_COMBINED;
            sd.sessState = CPA_DC_STATELESS;
#if (CPA_DC_API_VERSION_NUM_MAJOR == 1 && CPA_DC_API_VERSION_NUM_MINOR < 6)
            sd.deflateWindowSize = 7;
#endif
            sd.checksum = CPA_DC_NONE;

            // 获取会话大小
            status = cpaDcGetSessionSize(dcInstHandles[inst_idx], &sd, &sess_size, &ctx_size);
        }

        if (CPA_STATUS_SUCCESS == status)
        {
            // 分配会话内存
            status = PHYS_CONTIG_ALLOC(&sessionHdls[inst_idx], sess_size);
        }

        if (CPA_STATUS_SUCCESS == status)
        {
            status = cpaDcDpInitSession(dcInstHandles[inst_idx], sessionHdls[inst_idx], &sd);
        }
        qat_debug("[Success] Initialize session[%d]\n", inst_idx);
    }

    n->dc_inflight_ops = dc_inflight_ops;
    n->dc_session_handles = sessionHdls;
    n->dc_inst_handles = dcInstHandles;

    if (CPA_STATUS_SUCCESS == status)
    {
        status = qat_alloc_buffers(n);
    }

    if (CPA_STATUS_SUCCESS == status)
    {
        qat_debug("[Success] Allocate buffers\n");
    }

    return status;

err:
    free(dcInstHandles);
    free(sessionHdls);
    return CPA_STATUS_FAIL;
}

static void qat_dc_exit(FemuCtrl *n)
{
    CpaInstanceHandle *dcInstHandles = n->dc_inst_handles;
    CpaDcSessionHandle *sessionHdls = n->dc_session_handles;

    // 释放缓冲区
    qat_free_buffers(n);
    qat_debug("[Success] Free buffers\n");

    for (uint32_t inst_idx = 0; inst_idx < n->dc_inst_num; inst_idx++)
    {
        // 移除会话
        cpaDcDpRemoveSession(dcInstHandles[inst_idx], sessionHdls[inst_idx]);
        qat_debug("[Success] Remove session[%d]\n", inst_idx);

        // 释放会话上下文
        PHYS_CONTIG_FREE(sessionHdls[inst_idx]);
        qat_debug("[Success] Free session context[%d]\n", inst_idx);

        // 停止压缩实例
        cpaDcStopInstance(dcInstHandles[inst_idx]);
        qat_debug("[Success] Stop data compression instance[%s]\n", inst_idx);
    }

    free(n->dc_inst_handles);
    free(n->dc_session_handles);
}

CpaStatus qat_init(FemuCtrl *n)
{
    CpaStatus stat = CPA_STATUS_SUCCESS;
    n->qat_init_flag = false;
    n->dc_inst_num = 0;

    stat = qaeMemInit();
    if (CPA_STATUS_SUCCESS != stat)
    {
        qat_error("[Fail] Initialize memory driver\n");
        return stat;
    }
    qat_debug("[Success] Initialize memory driver\n");

    stat = icp_sal_userStartMultiProcess("SSL", CPA_FALSE);
    if (CPA_STATUS_SUCCESS != stat)
    {
        qat_error("[Fail] Start user process SSL\n");
        goto ssl_exit;
    }
    qat_debug("[Success] Start user process SSL\n");

    stat = qat_dc_init(n);
    if (CPA_STATUS_SUCCESS != stat)
    {
        qat_error("[Fail] Initialize data compression instance\n");
        goto dc_exit;
    }
    qat_debug("[Success] Initialize data compression instance\n");

    n->qat_init_flag = true;
    return CPA_STATUS_SUCCESS;

dc_exit:
    icp_sal_userStop();
    qat_debug("[Success] Stop user process SSL\n");

ssl_exit:
    qaeMemDestroy();
    qat_debug("[Success] Destory memory driver\n");

    return stat;
}

void qat_exit(FemuCtrl *n)
{
    if (false == n->qat_init_flag)
    {
        return;
    }

    qat_dc_exit(n);
    qat_debug("[Success] Stop data compress instance\n");

    icp_sal_userStop();
    qat_debug("[Success] Stop user process SSL\n");

    qaeMemDestroy();
    qat_debug("[Success] Destory memory driver\n");

    n->qat_init_flag = false;
}

//------------------------------------------------------------------------------

CpaStatus qat_dc_compress(FemuCtrl *n, uint32_t inst_idx, void *input, uint32_t input_len, uint32_t *output_len, uint32_t count)
{
    CpaInstanceHandle *dcInstHandles = n->dc_inst_handles;
    Cpa8U **pSrcBuffers = n->dc_src_buffers;
    CpaDcDpOpData **pOpDatas = n->dc_op_datas;
    uint32_t *dc_inflight_ops = n->dc_inflight_ops;
    CpaStatus status = CPA_STATUS_SUCCESS;
    uint32_t left = count;
    uint32_t batch_sz = 0;

    while (left)
    {
        batch_sz = left;
        if (batch_sz > QAT_OP_PER_INST) batch_sz = QAT_OP_PER_INST;

        for (uint32_t i = 0; i < batch_sz; i++)
        {
            // 拷贝数据到压缩缓冲区
            memcpy(pSrcBuffers[inst_idx * QAT_OP_PER_INST + i], input, input_len);

            pOpDatas[inst_idx * QAT_OP_PER_INST + i]->bufferLenToCompress = input_len;
            pOpDatas[inst_idx * QAT_OP_PER_INST + i]->sessDirection = CPA_DC_DIR_COMPRESS;
            pOpDatas[inst_idx * QAT_OP_PER_INST + i]->srcBufferLen = input_len;
            INIT_DC_DP_CNV_OPDATA(pOpDatas[inst_idx * QAT_OP_PER_INST + i]);

            input += input_len;
        }

        qatomic_set(dc_inflight_ops + inst_idx, batch_sz);

        // 提交压缩操作
        status = cpaDcDpEnqueueOpBatch(batch_sz, pOpDatas + inst_idx * QAT_OP_PER_INST, CPA_TRUE);
        if (CPA_STATUS_SUCCESS != status)
        {
            qat_error("cpaDcDpEnqueueOpBatch failed. (status = %d)\n", status);
        }

        if (CPA_STATUS_SUCCESS == status)
        {
            // 等待压缩操作完成
            do
            {
                status = icp_sal_DcPollDpInstance(dcInstHandles[inst_idx], 0);
            } while (((CPA_STATUS_SUCCESS == status) || (CPA_STATUS_RETRY == status)) &&
                        (qatomic_read(dc_inflight_ops + inst_idx) != 0));
        }
        if (CPA_STATUS_SUCCESS == status)
        {
            // 处理压缩操作结果
            for (uint32_t i = 0; i < batch_sz; i++)
            {
                qatomic_set(output_len + i, pOpDatas[inst_idx * QAT_OP_PER_INST + i]->results.produced);
            }
            output_len += batch_sz;
        }

        left -= batch_sz;
    }
    printf("original: %d, compressed: %d\n", input_len * count, *output_len);
    return status;
}
