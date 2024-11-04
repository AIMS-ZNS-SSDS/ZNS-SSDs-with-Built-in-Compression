#include "zns.h"
#include "qat-dc.h"
//#define FEMU_DEBUG_ZFTL
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// #ifdef READ_1
// static void *ftl_thread(void *arg, void *arg2);
// #else
static void *ftl_thread(void *arg);
// #endif

#ifdef COMP_META
uint16_t residue_size = 0;
uint32_t accumulated_size_1 = 0;
uint64_t first_lpn = 0;
uint64_t first_lpn_of_last_ppn = 0;
int j_value = 0;
int p_value = 0;
bool theloop = false;
bool shouldplus = false;
#endif

static inline struct ppa get_maptbl_ent(struct zns_ssd *zns, uint64_t lpn)
{
    return zns->maptbl[lpn];
}

static inline void set_maptbl_ent(struct zns_ssd *zns, uint64_t lpn, struct ppa *ppa)
{
    ftl_assert(lpn < zns->l2p_sz);
    zns->maptbl[lpn] = *ppa;
}

// #ifdef READ_1
// void zftl_init(NvmeNamespace *ns, FemuCtrl *n)
// {
//     struct zns_ssd *ssd = n->zns;

//     qemu_thread_create2(&ssd->ftl_thread, "FEMU-FTL-Thread", ftl_thread, n, ns
//                        QEMU_THREAD_JOINABLE);
// }
// #else
void zftl_init(FemuCtrl *n)
{
    struct zns_ssd *ssd = n->zns;

    qemu_thread_create(&ssd->ftl_thread, "FEMU-FTL-Thread", ftl_thread, n,
                       QEMU_THREAD_JOINABLE);
}
//#endif

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
        lat = pl->next_plane_avail_time - req_stime; //等待的净延迟，一个请求总延迟等于：等待的净延迟(lat)+执行的延迟(read_delay)
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


#ifdef COMP_META
/*added by zwl, zns oob area write/read*/

/*Write a single out-of-bound (OOB) area entry*/
static int zns_write_oob_meta(struct zns_ssd* zns, struct ppa ppa, void *meta)
{   
    assert(ppa.ppa != UNMAPPED_PPA);
    uint64_t absolute_page_number = (ppa.g.ch * zns->num_lun * zns->num_plane * zns->num_blk * zns->num_page) +
                           (ppa.g.fc * zns->num_plane * zns->num_blk * zns->num_page) +
                           (ppa.g.pl * zns->num_blk * zns->num_page) +
                           (ppa.g.blk * zns->num_page) + ppa.g.pg;
    uint64_t oft = absolute_page_number * zns->meta_len + zns->int_meta_size;
    uint8_t *tgt_sos_meta_buf = &zns->meta_buf[oft];
    assert(oft + zns->sos < zns->meta_total_bytes);
    if(oft + zns->sos < zns->meta_total_bytes) memcpy(tgt_sos_meta_buf, meta, zns->sos);

    return 0;
}

#ifdef READ_1
/* Read a single out-of-bound (OOB) area entry */
static int zns_read_oob_meta(struct zns_ssd* zns, struct ppa ppa, void *meta)
{
    uint64_t absolute_page_number = (ppa.g.ch * zns->num_lun * zns->num_plane * zns->num_blk * zns->num_page) +
                           (ppa.g.fc * zns->num_plane * zns->num_blk * zns->num_page) +
                           (ppa.g.pl * zns->num_blk * zns->num_page) +
                           (ppa.g.blk * zns->num_page) + ppa.g.pg;
    uint64_t oft = absolute_page_number * zns->meta_len + zns->int_meta_size;
    uint8_t *tgt_sos_meta_buf = &zns->meta_buf[oft];

    assert(oft + zns->sos < zns->meta_total_bytes);
    if(oft + zns->sos < zns->meta_total_bytes) memcpy(meta, tgt_sos_meta_buf, zns->sos);

    return 0;
}
#endif
#endif

#ifdef CHECK_READ_RIGHT
//这个函数用来模拟learned index中的错误。
// 生成 [min, max] 范围内的随机整数
static inline uint64_t random_uint64(uint64_t min, uint64_t max) {
    return min + (rand() % (max - min + 1));
}

// 生成 [0.0, 1.0] 范围内的随机浮点数
// static inline double random_probability() {
//     return 
// }

static inline uint64_t random_adjust_lpn(uint64_t lpn, uint64_t low_n, uint64_t high_n, uint32_t first_4_bytes) {
    // 计算增加和减少的范围
    uint64_t increase_min = low_n + first_4_bytes - lpn - 1;
    uint64_t increase_max = low_n;
    uint64_t decrease_min = lpn - first_4_bytes;
    uint64_t decrease_max = high_n;

    // 检查范围是否合法
    if (increase_min > increase_max || decrease_min > decrease_max) {
        fprintf(stderr, "Invalid ranges for LPN adjustment!\n");
        return lpn;  // 返回原始 lpn
    }

    // 生成随机概率
    double p = (double)rand() / (double)RAND_MAX;

    // 根据概率执行：不变、增加或减少
    if (p < 0.33) {
        // 不变，直接返回原始 lpn
        return lpn;
    } else if (p < 0.66) {
        // 执行增加操作
        return random_uint64(increase_min, increase_max);
    } else {
        // 执行减少操作
        return random_uint64(decrease_min, decrease_max);
    }
}

/*interface for getting ppn from learned index, modify the paramters as you need*/
static inline struct ppa get_right_ppa_from_oob(FemuCtrl *n, NvmeCmd cmd, uint64_t lpn, uint64_t *gap, uint16_t *residue){
    //FILE *f = fopen("readdebug.txt", "a");
    struct ppa ppa_from_li = get_maptbl_ent(n->zns, lpn);
    //fprintf(f,"lpn:%lu ppn:%lu \n", lpn, ppa_from_li.ppa);
    if(ppa_from_li.ppa == UNMAPPED_PPA) return ppa_from_li;
    /*replace above with real code*/
    //printf("ppa:%lu",ppa_from_li.ppa);
    char *meta = malloc(n->zns->sos);
    zns_read_oob_meta(n->zns, ppa_from_li, meta);
    // 提取前 4 byte (uint32_t)
    uint32_t first_4_bytes;
    uint8_t reverse_mapping;
    memcpy(&first_4_bytes, meta, 4);
    memcpy(&reverse_mapping, meta + 6, 1);
    uint8_t high_n = (reverse_mapping >> 4) & 0x0F;
    uint8_t low_n =  reverse_mapping & 0x0F;

    //模拟得到错误的ppn
    //std::memset(meta, 0, n->zns->sos);
    uint64_t fake_lpn = random_adjust_lpn(lpn, low_n, high_n, first_4_bytes);
    //uint64_t fake_lpn = lpn;
    ppa_from_li = get_maptbl_ent(n->zns, fake_lpn);
    zns_read_oob_meta(n->zns, ppa_from_li, meta);
    // 提取前 4 byte (uint32_t)
    memcpy(&first_4_bytes, meta, 4);
    memcpy(&reverse_mapping, meta + 6, 1);
    high_n = (reverse_mapping >> 4) & 0x0F;
    low_n =  reverse_mapping & 0x0F;

    if(first_4_bytes >= lpn || lpn < first_4_bytes + low_n){
        // 说明这个ppa是正确的
        memcpy(residue, meta + 4, 2); 
        *gap = lpn - first_4_bytes;
        //printf("lpn: %lu, first_4_bytes: %u, high_n: %u, low_n: %u, gap: %lu\n", lpn, first_4_bytes, high_n, low_n, *gap);
        free(meta);
        return ppa_from_li;
    } else if(lpn<first_4_bytes && lpn >= first_4_bytes - high_n){
        struct ppa ppa = get_maptbl_ent(n->zns, (uint64_t)(first_4_bytes - 1));
        zns_read_oob_meta(n->zns, ppa, meta);
        memmove(&first_4_bytes, meta, 4);
        *gap = lpn - first_4_bytes;
        memcpy(residue, meta + 4, 2);
        free(meta);
        return ppa;
    } else if(lpn>first_4_bytes && lpn >=first_4_bytes + low_n){
        struct ppa ppa = get_maptbl_ent(n->zns, (uint64_t)(first_4_bytes + low_n));
        zns_read_oob_meta(n->zns, ppa, meta);
        memmove(&first_4_bytes, meta, 4);
        *gap = lpn - first_4_bytes;
        memcpy(residue, meta + 4, 2);
        free(meta);
        return ppa;
    } else {
        ppa_from_li.ppa = UNMAPPED_PPA;
        femu_err("get ppn error!\n");
        free(meta);
        return ppa_from_li;
    }
    free(meta);
}   
#endif

#ifdef READ_1
static uint64_t read_with_ppn(FemuCtrl *n, NvmeCmd cmd, NvmeRequest *req){
    uint64_t sublat = 0;
    uint64_t sublat1 = 0;
    uint64_t sublat2 = 0;
    uint64_t maxlat = 0;
    uint64_t lba = req->slba;
    uint32_t nlb = req->nlb;
    uint64_t secs_per_pg = LOGICAL_PAGE_SIZE/n->zns->lbasz;
    uint64_t start_lpn = lba / secs_per_pg;
    uint64_t end_lpn = (lba + nlb - 1) / secs_per_pg;

    for(uint64_t lpn = start_lpn; lpn <= end_lpn;){
        uint64_t gap =0;
        uint16_t residue = 0;
        struct ppa ppa = get_right_ppa_from_oob(n, cmd, lpn, &gap, &residue);
        if (!mapped_ppa(&ppa) || !valid_ppa(n->zns, &ppa)) {
            //femu_log("ppa not mapped, skip\n");
            lpn++;
            continue;
        
        }

        char *meta = malloc(n->zns->sos);
        zns_read_oob_meta(n->zns, ppa, meta);
        // 提取前 4 byte (uint32_t)
        uint32_t first_4_bytes;
        uint8_t reverse_mapping;
        memcpy(&first_4_bytes, meta, 4);
        memcpy(&reverse_mapping, meta + 6, 1);
        uint8_t low_n =  reverse_mapping & 0x0F;

        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;
        sublat1 = zns_advance_status(n->zns, &ppa, &srd);

        if((first_4_bytes+low_n - 1 == lpn) || (end_lpn-lpn+1) == (first_4_bytes+low_n-lpn)){ //可能跨页读
            struct ppa ppa_next = get_maptbl_ent(n->zns, first_4_bytes + low_n);
            char *meta2 = malloc(n->zns->sos);
            zns_read_oob_meta(n->zns, ppa_next, meta2);
            uint16_t residue2 = 0;
            memcpy(&residue2,meta+4,2);
            memcpy(&first_4_bytes, meta, 4);
            memcpy(&reverse_mapping, meta + 6, 1);
            low_n =  reverse_mapping & 0x0F;

            if(residue2 != 0){
                // two pages read
                sublat2 = zns_advance_status(n->zns, &ppa_next, &srd);
                
            }
            lpn += first_4_bytes+low_n-lpn;
        } else if((end_lpn-lpn+1) > (first_4_bytes+low_n-lpn)){ //必然跨页读
            struct ppa ppa_next = get_maptbl_ent(n->zns, first_4_bytes + low_n);
            char *meta2 = malloc(n->zns->sos);
            zns_read_oob_meta(n->zns, ppa_next, meta2);
            memcpy(&first_4_bytes, meta, 4);
            memcpy(&reverse_mapping, meta + 6, 1);
            low_n =  reverse_mapping & 0x0F;
            sublat2 = zns_advance_status(n->zns, &ppa_next, &srd);
            if(low_n >end_lpn){
                lpn = end_lpn+1;
            } else {
                lpn += low_n;
            }
        } else { //不跨页读
            lpn = end_lpn+1;
        }
        free(meta);
        //if not optimized, then sublat = sublat1 + sublat2;
        sublat = (sublat1 > sublat2) ? sublat1 : sublat2;
        maxlat = (sublat > maxlat) ? sublat : maxlat;

     }
     return maxlat;
}
#endif

#ifndef READ_1
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
        ppa = get_maptbl_ent(zns, lpn);
        if (!mapped_ppa(&ppa) || !valid_ppa(zns, &ppa)) {
            continue;
        }

        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;

        sublat = zns_advance_status(zns, &ppa, &srd);
        //femu_log("[R] lpn:\t%lu\t<--ch:\t%u\tlun:\t%u\tpl:\t%u\tblk:\t%u\tpg:\t%u\tsubpg:\t%u\tlat\t%lu\n",lpn,ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg,sublat);
        maxlat = (sublat > maxlat) ? sublat : maxlat;
    }

    return maxlat;
}
#endif

static uint64_t zns_wc_flush(struct zns_ssd* zns, int wcidx, int type,uint64_t stime)
{
    int i,j,p,subpage;
    struct ppa ppa;
    struct ppa oldppa;
    uint64_t lpn;
    int flash_type = zns->flash_type;
    uint64_t sublat = 0, maxlat = 0;

// #ifdef COMP_META
//     FILE *file = fopen("write_oob_debug.txt", "a");
// #endif
    i = 0;
    #ifdef COMP_META
    //LPN到PPN的映射
    //FIXME: ONLY support  ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE = 1
    while (i < zns->cache.write_cache[wcidx].used)
    {
        for(p = 0;p<zns->num_plane;p++){
            /* new write */           
            ppa = get_new_page(zns);
            if(theloop){
                p = p_value;
            }
            ppa.g.pl = p;
            for(j = 0; j < flash_type ;j++)
            {
                ppa.g.pg = get_blk(zns,&ppa)->page_wp;
itr:       
                if(theloop){
                    if(shouldplus){
                        j = j_value + 1;
                        shouldplus = false;
                    } else {
                        j = j_value;
                    }
                    theloop = false;
                }
                for(subpage = 0;subpage < ZNS_PAGE_SIZE/LOGICAL_PAGE_SIZE;subpage++)
                {
                    if(i+subpage >= zns->cache.write_cache[wcidx].used)
                    {
                        //No need to write an invalid page
                        break;
                    }
                    
                    lpn = zns->cache.write_cache[wcidx].lpns[i+subpage].lpn;
                
                    oldppa = get_maptbl_ent(zns, lpn);
                    if (mapped_ppa(&oldppa)) {
                        /* FIXME: Misao: update old page information*/
                    }
                    ppa.g.spg = subpage;
                    /* update maptbl */
                    set_maptbl_ent(zns, lpn, &ppa);
                    //femu_log("[F] lpn:\t%lu\t-->ch:\t%u\tlun:\t%u\tpl:\t%u\tblk:\t%u\tpg:\t%u\tsubpg:\t%u\tlat\t%lu\n",lpn,ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg,ppa.g.spg,sublat);
                }
                
                if((residue_size + accumulated_size_1 + zns->cache.write_cache[wcidx].lpns[i].compressed_size) < ZNS_PAGE_SIZE){
                    accumulated_size_1 += zns->cache.write_cache[wcidx].lpns[i].compressed_size;
                    i++;
                    if(i >= zns->cache.write_cache[wcidx].used){                    
                        theloop = true;
                        shouldplus = false;
                        j_value = j;
                        p_value = p;

                        char *meta = malloc(zns->meta_len - zns->int_meta_size);
                        memcpy(meta, &first_lpn, 4);
                        memcpy(meta+4, &residue_size, 2);

                        uint16_t reverse_mapping = ((first_lpn - first_lpn_of_last_ppn) & 0xFF) << 8;
                        memcpy(meta+6, &reverse_mapping, 2);
                        zns_write_oob_meta(zns, ppa, meta);

                        break;
                    }
                    goto itr;
                } else {
                    //一个物理页写满，可以写oob
                    
                    char *meta = malloc(zns->meta_len - zns->int_meta_size);
                    memcpy(meta, &first_lpn, 4);
                    memcpy(meta+4, &residue_size, 2);
                    //fprintf(file, "residue_size: %u, First lpn: %lu, first_lpn_of_last_ppn:%lu\n", residue_size, first_lpn, first_lpn_of_last_ppn);
                    residue_size = zns->cache.write_cache[wcidx].lpns[i].compressed_size - (ZNS_PAGE_SIZE - accumulated_size_1 - residue_size);
                    uint16_t reverse_mapping = (((first_lpn - first_lpn_of_last_ppn) & 0xFF) << 8) | ((zns->cache.write_cache[wcidx].lpns[i].lpn+1 - first_lpn) & 0xFF);
                    
                    memcpy(meta+6, &reverse_mapping, 2);
                    //fprintf(file,"lpn:\t%lu\t-->ch:\t%u\tlun:\t%u\tpl:\t%u\tblk:\t%u\tpg:\t%u\n",lpn,ppa.g.ch,ppa.g.fc,ppa.g.pl,ppa.g.blk,ppa.g.pg);
                    zns_write_oob_meta(zns, ppa, meta);

                    free(meta);

                    get_blk(zns,&ppa)->page_wp++;
                    accumulated_size_1 = 0;
                    
                    first_lpn_of_last_ppn = first_lpn;

                    first_lpn = zns->cache.write_cache[wcidx].lpns[i].lpn+1;
                    
                    i++;

                    if(j == flash_type-1){
                        //fprintf(file, "j == flash_type-1\n");
                        theloop = false;
                        shouldplus = false;
                        j_value = 0;
                        p_value = 0;
                        break;
                    }
                    if(i >= zns->cache.write_cache[wcidx].used){
                        if(j == flash_type-1){
                            //fprintf(file, "j == flash_type-1\n");
                            theloop = false;
                            shouldplus = false;
                            j_value = 0;
                            p_value = 0;
                            break;
                        } else {
                            theloop = true;
                            shouldplus = true;
                            break;
                        }
                    }
                }
            }
            //FIXME Misao: identify padding page
            if(!theloop)
            {
                struct nand_cmd swr;
                swr.type = type;
                swr.cmd = NAND_WRITE;
                swr.stime = stime;
                /* get latency statistics */
                sublat = zns_advance_status(zns, &ppa, &swr);
                maxlat = (sublat > maxlat) ? sublat : maxlat;
            }

            if(i >= zns->cache.write_cache[wcidx].used)
                {
                    break;
                }
                
        }
        //added by zwl
            // advance write pointer here to prioritize channel and chip level parallelism
            if(!theloop) zns_advance_write_pointer(zns);

    }
    
    //check the correctness of reverse mapping
    // for(int temp = 0; temp < zns->cache.write_cache[wcidx].used; temp++){
    //     uint64_t lpn_t = zns->cache.write_cache[wcidx].lpns[temp].lpn;
    //     if(zns->maptbl[lpn_t].ppa != UNMAPPED_PPA){
    //         fprintf(file, "lpn: %ld, ppa: Ch[%d] FC[%d] Pln[%d] Blk[%d] PPN[%d]\n", lpn_t, zns->maptbl[lpn_t].g.ch, zns->maptbl[lpn_t].g.fc, zns->maptbl[lpn_t].g.pl, zns->maptbl[lpn_t].g.blk,zns->maptbl[lpn_t].g.pg);
    //         char *meta = malloc(zns->meta_len - zns->int_meta_size);
    //         zns_read_oob_meta(zns, zns->maptbl[lpn_t], meta);

    //         // 提取前 4 byte (uint32_t)
    //         uint32_t first_4_bytes;
    //         memcpy(&first_4_bytes, meta, 4);

    //         // 提取随后的 2 byte (uint16_t)
    //         uint16_t next_2_bytes;
    //         memcpy(&next_2_bytes, meta + 4, 2);

    //         // 提取随后的 1 byte (uint8_t)
    //         uint16_t next_1_byte;
    //         memcpy(&next_1_byte, meta + 6, 2);
    //         fprintf(file,"SOS Data: First lpn: %u, residue size: %u,", first_4_bytes, next_2_bytes);
    //         fprintf(file, "reverse mapping: former %u, latter %u\n", (next_1_byte & 0xFF00) >> 8, next_1_byte & 0xFF);
    //         //for (int x = next_1_byte - 1; x >= 0; x--) {
    //          //   printf("%c", (next_1_byte & (1ULL << x)) ? '1' : '0');
    //         //}
    //         //printf("\n");
    //         free(meta);
    //     }
    // }
    //fclose(file);
     #else

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
                    /* update maptbl */
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
        //added by zwl
        // advance write pointer here to prioritize channel and chip level parallelism
        //zns_advance_write_pointer(zns);
        }
        /* need to advance the write pointer here */
        zns_advance_write_pointer(zns);
    }
    #endif
    zns->cache.write_cache[wcidx].used = 0;
    return maxlat;
}

static uint64_t zns_write(FemuCtrl *n, struct zns_ssd *zns, NvmeRequest *req)
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
            sublat = zns_wc_flush(zns,wcidx,USER_IO,req->stime);
            maxlat = (sublat > maxlat) ? sublat : maxlat;
            sublat = 0;
        }
        #ifdef COMP_META
        zns->cache.write_cache[wcidx].lpns[zns->cache.write_cache[wcidx].used].lpn = lpn;
        zns->cache.write_cache[wcidx].lpns[zns->cache.write_cache[wcidx].used].compressed_size = req->compressed_size[lpn-start_lpn];
        //zns->cache.write_cache[wcidx].lpns[zns->cache.write_cache[wcidx].used].compressed_size = 35;
        zns->cache.write_cache[wcidx].used++;
        #else
        zns->cache.write_cache[wcidx].lpns[zns->cache.write_cache[wcidx].used++]=lpn;
        #endif
        sublat += SRAM_WRITE_LATENCY_NS * zns->cache.write_cache[wcidx].lpns[zns->cache.write_cache[wcidx].used].compressed_size/ZNS_PAGE_SIZE; //Simplified timing emulation
        //sublat += SRAM_WRITE_LATENCY_NS;
        maxlat = (sublat > maxlat) ? sublat : maxlat;
        //femu_log("[W] lpn:\t%lu\t-->wc cache:%u, used:%u\n",lpn,(int)wcidx,(int)zns->cache.write_cache[wcidx].used);
    }
    #ifdef COMP_META
    free(req->compressed_size);
    #endif
    //fclose(f);
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
                lat = zns_write(n, zns, req);
                break;
            case NVME_CMD_READ:
            #ifdef READ_1
                lat = read_with_ppn(n, req->cmd, req);
            #else
                lat = zns_read(zns, req);
            #endif
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
//#endif
