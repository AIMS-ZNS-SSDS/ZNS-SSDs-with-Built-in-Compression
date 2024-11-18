#include "zns.h"

#define learn_ftl
//#define FEMU_DEBUG_ZFTL

#ifdef learn_ftl
#define errbnd 1 
#define TRAIN_LEN 256
//todo:修改
#define      8388608
void LeastSquare(uint64_t *x, uint64_t *y, int num, float *w, float *b) {  

		// // first standardlize the data
        float t1=0, t2=0, t3=0, t4=0;  
        for(int i=0; i<num; ++i) {  
            t1 += x[i]*x[i];  
            t2 += x[i];  
            t3 += x[i]*y[i];  
            t4 += y[i];  
        }  
        *w = (t3*num - t2*t4) / (t1*num - t2*t2);  
        //b = (t4 - a*t2) / num;  
        *b = (t1*t4 - t2*t3) / (t1*num - t2*t2);  
		// printf("training result: %f %f\n", *w, *b);
		
		// int train_success_num = 0;
		// for (int i = 0; i < num; i++) {
		// 	float yy = predict(x[i], w, b);
		// 	int ya = y[i];
		// 	if (abs(yy - ya) < 1) {
		// 		train_success_num++;
		// 	}
		// }
		// printf("train success: %d ; %d\n", train_success_num, num);
}

//todo：vppn生成方法修改对接
static uint64_t ppa2vppn(struct zns_ssd *ssd, struct ppa *ppa) {
    
    struct ssdparams *spp = &ssd->sp;
    uint64_t vppn;
    vppn = ppa->g.ch + \
            ppa->g.lun * spp->chn_per_lun + \
            ppa->g.pl * spp->chn_per_pl + \
            ppa->g.pg * spp->chn_per_pg + \
            ppa->g.blk * spp->chn_per_blk;
    
    return vppn;
}
static struct ppa vppn2ppa(struct zns_ssd *ssd, uint64_t vppn) {
    struct ppa ppa;
    struct ssdparams *spp = &ssd->sp;
    ppa.g.blk = vppn / spp->chn_per_blk;
    vppn -= ppa.g.blk*spp->chn_per_blk;
    ppa.g.pg = vppn / spp->chn_per_pg;
    vppn -= ppa.g.pg * spp->chn_per_pg;
    ppa.g.pl = vppn / spp->chn_per_pl;
    vppn -= ppa.g.pl * spp->chn_per_pl;
    ppa.g.lun = vppn / spp->chn_per_lun;
    ppa.g.ch = vppn - ppa.g.lun * spp->chn_per_lun;

    return ppa;
}

static segment_param* segment_param_init(struct zns_ssd *ssd) {
    //todo:修改参数
    segment_param* sp = g_malloc0(sizeof(segment_param));
    sp->buffer_len = 2048;
    sp->mapping_per_page = 256;
    sp->page_total_cnt = 8388608;
    sp->threshold = 10;
    sp->sgement_cnt = sp->page_total_cnt/sp->mapping_per_page;

    return sp;
}
static void regression(uint64_t* lpns, uint64_t* ppns, int len, learned_mgmt* lm, int seg_no) {
    float k, b;
    uint64_t start_lpn = lpns[0];
    uint64_t start_ppn = ppns[0];
    uint64_t off_x[TRAIN_LEN];
    uint64_t off_y[TRAIN_LEN];
    uint64_t group_start = seg_no * lm->sp->mapping_per_page;
    segment_total_cnt++;
    //偏移
    for (int i = 0; i < len; i++) {
        off_x[i] = lpns[i] - start_lpn;
        off_y[i] = ppns[i] - start_ppn;
    }
    //最小二乘法
    LeastSquare(off_x, off_y, len, &k, &b);
    bool within_errbnd = true;
    for (int i = 0; i < len; i++) {
        uint64_t predicted_ppn = k * off_x[i] + b + start_ppn;
        if (abs(predicted_ppn - ppns[i]) > errbnd) {
            within_errbnd = false;
            break;
        }
    }
    if (within_errbnd) {
        simple_segment *new_seg = g_malloc0(sizeof(simple_segment));
        //斜率
        new_seg->k = k;                          
        //索引的最大长度
        new_seg->len = lpns[len-1] - lpns[0];
        //距离起点距离
        new_seg->slpa = lpns[0]-group_start;
        new_seg->inter = b + start_ppn;  
        segment_insert_simple_segment(new_seg, new_seg, len);
    } else {
        // 不满足errbnd，分化
        int mid = len / 2;
        //todo：两个sg
        regression(lpns, ppns, mid, lm, seg_no, errbnd);
        regression(lpns + mid, ppns + mid, len - mid, lm, seg_no, errbnd);
    } 
}

static uint64_t get_upper_lpn_bound(simple_segment* sg) {
    uint64_t ub = sg->slpa+sg->len;
    return ub;
}

static uint64_t get_lower_lpn_bound(simple_segment* sg) {
    uint64_t lb = sg->slpa;
    return lb;
}
static void segment_init(segment *sg, int no) {
    
    sg->seg_cnt = 0;
    sg->segment_head = NULL;
    sg->segment_no = no;
}
static int segment_search_simple_segment(simple_segment* sg, uint64_t lpn, uint64_t* ppn, learned_mgmt* lm) {
    int flag = 0;
    segment_param *sp = lm->sp;
    int seg_no = lpn / sp->mapping_per_page;
    uint64_t group_start = seg_no*sp->mapping_per_page;
    uint64_t off_lpn = lpn - group_start;
    *ppn = off_lpn*sg->k + sg->inter;
    flag = 1;
    return flag;
}
//将simple seg插入到seg
static segment* segment_insert_simple_segment(segment* sg, simple_segment* new_seg, int len) {

    uint64_t start_lpn = get_lower_lpn_bound(new_seg);
    uint64_t end_lpn = get_upper_lpn_bound(new_seg);
    //队首
    if (sg->segment_head == NULL) {
        sg->segment_head = new_seg;
        new_seg->next = NULL;
    } else { //非队首
        uint64_t sg_lb = get_lower_lpn_bound(sg->segment_head); 
        // uint64_t sg_ub = get_upper_lpn_bound(sg->segment_head);
        //插到队首
        if (end_lpn < sg_lb) {
            new_seg->next = sg->segment_head;
            sg->segment_head = new_seg;

            return NULL;
        }

        simple_segment* p = sg->segment_head;
        simple_segment* q = sg->segment_head->next;
        
        while (q) {
            
            uint64_t p_ub = get_upper_lpn_bound(p);
            uint64_t q_lb = get_lower_lpn_bound(q);
            uint64_t q_ub = get_upper_lpn_bound(q);

            if (end_lpn < q_lb && start_lpn > p_ub) {
                new_seg->next = q;
                p->next = new_seg;
                break;
            } else if(start_lpn > q_ub) {
                q = q->next;
                p = p->next;
                if (q == NULL) {
                    p->next = new_seg;
                    break;
                }
            } else {
                // TODO: 发生重写
                return sg;
            }
        }
        
    }

    return NULL;

}
//lpn2ppn
static uint64_t get_ppn(segment *sg, uint64_t lpn) {
    simple_segment *sm = sg->segment_head;
    uint64_t ppn = INVALID_VAL;
    
    while (sm) {
        if (segment_search_simple_segment(sm, lpn, &ppn, NULL)) {
            return ppn;
        }
        sm = sm->next;
    }
    return INVALID_VAL;
}

static void flush_lpn_ppn_mappings(uint64_t *lpns, uint64_t *ppns, int len) {
    // simple_segment *new_seg = g_malloc0(sizeof(simple_segment));
    regression(lpns, ppns, len, NULL, 0);
    // segment_insert_simple_segment(sg, new_seg, len);
}
static learned_mgmt* learned_segments_init(struct zns_ssd* ssd) {
    learned_mgmt* lm = g_malloc0(sizeof(learned_mgmt));
    lm->sp = segment_param_init(ssd);
    // lm->bitmap = g_malloc0(sizeof(uint8_t)*ssd->sp.tt_pgs);
    return lm;
}

#else
#endif


static void *ftl_thread(void *arg);

static inline struct ppa get_maptbl_ent(struct zns_ssd *zns, uint64_t lpn)
{
    return zns->maptbl[lpn];
}

static inline void set_maptbl_ent(struct zns_ssd *zns, uint64_t lpn, struct ppa *ppa)
{
    ftl_assert(lpn < zns->l2p_sz);
    zns->maptbl[lpn] = *ppa;
}

void zftl_init(FemuCtrl *n)
{
    struct zns_ssd *ssd = n->zns;
    #ifdef learn_ftl
    learned_segments_init(ssd);
    #endif
    qemu_thread_create(&ssd->ftl_thread, "FEMU-FTL-Thread", ftl_thread, n,
                       QEMU_THREAD_JOINABLE);
}

static inline struct zns_ch *get_ch(struct zns_ssd *zns, struct ppa *ppa)
{
    return &(zns->ch[ppa->g.ch]);
}

static inline struct zns_fc *get_fc(struct zns_ssd *zns, struct ppa *ppa)
{
    struct zns_ch *ch = get_ch(zns, ppa);
    return &(ch->fc[ppa->g.fc]);
}

static inline struct zns_plane *get_plane(struct zns_ssd *zns, struct ppa *ppa)
{
    struct zns_fc *fc = get_fc(zns, ppa);
    return &(fc->plane[ppa->g.pl]);
}

static inline struct zns_blk *get_blk(struct zns_ssd *zns, struct ppa *ppa)
{
    struct zns_plane *pl = get_plane(zns, ppa);
    return &(pl->blk[ppa->g.blk]);
}

static inline void check_addr(int a, int max)
{
   assert(a >= 0 && a < max);
}

static void zns_advance_write_pointer(struct zns_ssd *zns)
{
    struct write_pointer *wpp = &zns->wp;

    check_addr(wpp->ch, zns->num_ch);
    wpp->ch++;
    if (wpp->ch == zns->num_ch) {
        wpp->ch = 0;
        check_addr(wpp->lun, zns->num_lun);
        wpp->lun++;
        /* in this case, we should go to next lun */
        if (wpp->lun == zns->num_lun) {
            wpp->lun = 0;
        }
    }
}

static uint64_t zns_advance_status(struct zns_ssd *zns, struct ppa *ppa,struct nand_cmd *ncmd)
{
    int c = ncmd->cmd;

    uint64_t nand_stime;
    uint64_t req_stime = (ncmd->stime == 0) ? \
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : ncmd->stime;

    //plane level parallism
    struct zns_plane *pl = get_plane(zns, ppa);

    uint64_t lat = 0;
    int nand_type = get_blk(zns,ppa)->nand_type;

    uint64_t read_delay = zns->timing.pg_rd_lat[nand_type];
    uint64_t write_delay = zns->timing.pg_wr_lat[nand_type];
    uint64_t erase_delay = zns->timing.blk_er_lat[nand_type];

    switch (c) {
    case NAND_READ:
        nand_stime = (pl->next_plane_avail_time < req_stime) ? req_stime : \
                     pl->next_plane_avail_time;
        pl->next_plane_avail_time = nand_stime + read_delay;
        lat = pl->next_plane_avail_time - req_stime;
	    break;

    case NAND_WRITE:
	    nand_stime = (pl->next_plane_avail_time < req_stime) ? req_stime : \
		            pl->next_plane_avail_time;
	    pl->next_plane_avail_time = nand_stime + write_delay;
	    lat = pl->next_plane_avail_time - req_stime;
	    break;

    case NAND_ERASE:
        nand_stime = (pl->next_plane_avail_time < req_stime) ? req_stime : \
                        pl->next_plane_avail_time;
        pl->next_plane_avail_time = nand_stime + erase_delay;
        lat = pl->next_plane_avail_time - req_stime;
        break;

    default:
        /* To silent warnings */
        ;
    }

    return lat;
}

static inline bool valid_ppa(struct zns_ssd *zns, struct ppa *ppa)
{
    int ch = ppa->g.ch;
    int lun = ppa->g.fc;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sub_pg = ppa->g.spg;

    if (ch >= 0 && ch < zns->num_ch && lun >= 0 && lun < zns->num_lun && pl >=
        0 && pl < zns->num_plane && blk >= 0 && blk < zns->num_blk && pg>=0 && pg < zns->num_page && sub_pg >= 0 && sub_pg < ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE)
        return true;

    return false;
}

static inline bool mapped_ppa(struct ppa *ppa)
{
    return !(ppa->ppa == UNMAPPED_PPA);
}

static struct ppa get_new_page(struct zns_ssd *zns)
{
    struct write_pointer *wpp = &zns->wp;
    struct ppa ppa;
    ppa.ppa = 0;
    ppa.g.ch = wpp->ch;
    ppa.g.fc = wpp->lun;
    ppa.g.blk = zns->active_zone;
    ppa.g.V = 1; //not padding page
    /*pl?*/
    if(!valid_ppa(zns,&ppa))
    {
        ftl_err("[Misao] invalid ppa: ch %u lun %u pl %u blk %u pg %u subpg  %u \n",ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg);
        ppa.ppa = UNMAPPED_PPA;
    }
    return ppa;
}

static int zns_get_wcidx(struct zns_ssd* zns)
{
    int i;
    for(i = 0;i < zns->cache.num_wc;i++)
    {
        if(zns->cache.write_cache[i].sblk==zns->active_zone)
        {
            return i;
        }
    }
    return -1;
}

static uint64_t zns_read(struct zns_ssd *zns, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    uint32_t nlb = req->nlb;
    uint64_t secs_per_pg = LOGICAL_PAGE_SIZE/zns->lbasz;
    uint64_t start_lpn = lba / secs_per_pg;
    uint64_t end_lpn = (lba + nlb - 1) / secs_per_pg;
    //int wcidx = zns_get_wcidx(zns);
    struct ppa ppa;
    uint64_t lpn;
    uint64_t sublat, maxlat = 0;

    /* normal IO read path */
    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        #ifdef learn_ftl
        //todo
        uint64_t vppn = get_ppn(zns->learned_segments, lpn);
        ppa = vppn2ppa(zns, vppn);
        if (!mapped_ppa(&ppa) || !valid_ppa(zns, &ppa)) {
            continue;
        }       
        #else
        ppa = get_maptbl_ent(zns, lpn);
        if (!mapped_ppa(&ppa) || !valid_ppa(zns, &ppa)) {
            continue;
        }
        #endif
        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;

        sublat = zns_advance_status(zns, &ppa, &srd);
        femu_log("[R] lpn:\t%lu\t<--ch:\t%u\tlun:\t%u\tpl:\t%u\tblk:\t%u\tpg:\t%u\tsubpg:\t%u\tlat\t%lu\n",lpn,ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg,sublat);
        maxlat = (sublat > maxlat) ? sublat : maxlat;
    }

    return maxlat;
}

static uint64_t zns_wc_flush(struct zns_ssd* zns, int wcidx, int type,uint64_t stime)
{
    int i,j,p,subpage;
    struct ppa ppa;
    struct ppa oldppa;
    uint64_t lpn;
    int flash_type = zns->flash_type;
    uint64_t sublat = 0, maxlat = 0;

    #ifdef learn_ftl
    uint64_t lpns[zns->cache.write_cache[wcidx].used];
    uint64_t ppns[zns->cache.write_cache[wcidx].used];
    #endif

    i = 0;
    while(i < zns->cache.write_cache[wcidx].used)
    {
        for(p = 0;p<zns->num_plane;p++){
            /* new write */
            ppa = get_new_page(zns);
            ppa.g.pl = p;
            for(j = 0; j < flash_type ;j++)
            {
                ppa.g.pg = get_blk(zns,&ppa)->page_wp;
                get_blk(zns,&ppa)->page_wp++;
                for(subpage = 0;subpage < ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE;subpage++)
                {
                    if(i+subpage >= zns->cache.write_cache[wcidx].used)
                    {
                        //No need to write an invalid page
                        break;
                    }
                    lpn = zns->cache.write_cache[wcidx].lpns[i+subpage];
                    oldppa = get_maptbl_ent(zns, lpn);
                    if (mapped_ppa(&oldppa)) {
                        /* FIXME: Misao: update old page information*/
                    }
                    ppa.g.spg = subpage;
                    /* update maptbl  */
                    #ifdef learn_ftl
                    lpns[i] = lpn;
                    ppns[i] = ppa2vppn(zns , &ppa);

                    #endif
                    set_maptbl_ent(zns, lpn, &ppa);
                
                    //femu_log("[F] lpn:\t%lu\t-->ch:\t%u\tlun:\t%u\tpl:\t%u\tblk:\t%u\tpg:\t%u\tsubpg:\t%u\tlat\t%lu\n",lpn,ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg,sublat);
                }
                i+=ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE;
            }
            //FIXME Misao: identify padding page
            if(ppa.g.V)
            {
                struct nand_cmd swr;
                swr.type = type;
                swr.cmd = NAND_WRITE;
                swr.stime = stime;
                /* get latency statistics */
                sublat = zns_advance_status(zns, &ppa, &swr);
                maxlat = (sublat > maxlat) ? sublat : maxlat;
            }
        }
        /* need to advance the write pointer here */
        zns_advance_write_pointer(zns);
    }
     #ifdef learn_ftl
    //满时学习
    //todo：修改时机
    flush_lpn_ppn_mappings(lpns, ppns, zns->cache.write_cache[wcidx].used);
    #endif
    zns->cache.write_cache[wcidx].used = 0;
    return maxlat;
}

static uint64_t zns_write(struct zns_ssd *zns, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    uint32_t nlb = req->nlb;
    uint64_t secs_per_pg = LOGICAL_PAGE_SIZE/zns->lbasz;
    uint64_t start_lpn = lba / secs_per_pg;
    uint64_t end_lpn = (lba + nlb - 1) / secs_per_pg;
    uint64_t lpn;
    uint64_t sublat = 0, maxlat = 0;
    int i;
    int wcidx = zns_get_wcidx(zns);

    if(wcidx==-1)
    {
        //need flush
        wcidx = 0;
        uint64_t t_used = zns->cache.write_cache[wcidx].used;
        for(i = 1;i < zns->cache.num_wc;i++)
        {
            if(zns->cache.write_cache[i].used==0)
            {
                t_used = 0;
                wcidx = i; //free wc！
                break;
            }
            if(zns->cache.write_cache[i].used > t_used)
            {
                t_used = zns->cache.write_cache[i].used;
                wcidx = i;
            }
        }
        if(t_used) maxlat = zns_wc_flush(zns,wcidx,USER_IO,req->stime);
        zns->cache.write_cache[wcidx].sblk = zns->active_zone;
    }

    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        if(zns->cache.write_cache[wcidx].used==zns->cache.write_cache[wcidx].cap)
        {
            femu_log("[W] flush wc %d (%u/%u)\n",wcidx,(int)zns->cache.write_cache[wcidx].used,(int)zns->cache.write_cache[wcidx].cap);
            sublat = zns_wc_flush(zns,wcidx,USER_IO,req->stime);
            femu_log("[W] flush lat: %u\n", (int)sublat);
            maxlat = (sublat > maxlat) ? sublat : maxlat;
            sublat = 0;
            // #ifdef learn_ftl
            // //满时学习
            // //todo：修改时机
            // uint64_t lpns[zns->cache.write_cache[wcidx].used];
            // uint64_t ppns[zns->cache.write_cache[wcidx].used];
            // for (i = 0; i < zns->cache.write_cache[wcidx].used; i++) {
            //     lpns[i] = zns->cache.write_cache[wcidx].lpns[i];
            //     //fix:删除maptbl，缓存单组lpn ppn
            //     ppns[i] = ppa2vppn(zns, &get_maptbl_ent(zns, lpns[i])); // 获取当前映射的PPN
            // }
            // flush_lpn_ppn_mappings(lpns, ppns, zns->cache.write_cache[wcidx].used);
            // #endif
        }
        zns->cache.write_cache[wcidx].lpns[zns->cache.write_cache[wcidx].used++]=lpn;
        sublat += SRAM_WRITE_LATENCY_NS; //Simplified timing emulation
        maxlat = (sublat > maxlat) ? sublat : maxlat;
        femu_log("[W] lpn:\t%lu\t-->wc cache:%u, used:%u\n",lpn,(int)wcidx,(int)zns->cache.write_cache[wcidx].used);
    }
    return maxlat;
}

static void *ftl_thread(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct zns_ssd *zns = n->zns;
    NvmeRequest *req = NULL;
    uint64_t lat = 0;
    int rc;
    int i;

    while (!*(zns->dataplane_started_ptr)) {
        usleep(100000);
    }

    /* FIXME: not safe, to handle ->to_ftl and ->to_poller gracefully */
    zns->to_ftl = n->to_ftl;
    zns->to_poller = n->to_poller;

    while (1) {
        for (i = 1; i <= n->nr_pollers; i++) {
            if (!zns->to_ftl[i] || !femu_ring_count(zns->to_ftl[i]))
                continue;

            rc = femu_ring_dequeue(zns->to_ftl[i], (void *)&req, 1);
            if (rc != 1) {
                printf("FEMU: FTL to_ftl dequeue failed\n");
            }

            ftl_assert(req);
            switch (req->cmd.opcode) {
            case NVME_CMD_WRITE:
                lat = zns_write(zns, req);
                break;
            case NVME_CMD_READ:
                lat = zns_read(zns, req);
                break;
            case NVME_CMD_DSM:
                lat = 0;
                break;
            default:
                //ftl_err("FTL received unkown request type, ERROR\n");
                ;
            }

            req->reqlat = lat;
            req->expire_time += lat;

            rc = femu_ring_enqueue(zns->to_poller[i], (void *)&req, 1);
            if (rc != 1) {
                ftl_err("FTL to_poller enqueue failed\n");
            }

        }
    }

    return NULL;
}
