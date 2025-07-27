#include "./zns.h"
#include "qat-dc.h"
#include <stdlib.h>
#include <stdint.h>
#define MIN_DISCARD_GRANULARITY     (4 * KiB)
#define NVME_DEFAULT_ZONE_SIZE      (128 * MiB)
#define NVME_DEFAULT_MAX_AZ_SIZE    (128 * KiB)

#define UPPER(x, base) ((x + (base) - 1) / (base) * (base)) 

static inline uint32_t zns_zone_idx(NvmeNamespace *ns, uint64_t slba)
{
    FemuCtrl *n = ns->ctrl;

    return (n->zone_size_log2 > 0 ? slba >> n->zone_size_log2 : slba / n->zone_size);
}

static inline NvmeZone *zns_get_zone_by_slba(NvmeNamespace *ns, uint64_t slba)
{
    FemuCtrl *n = ns->ctrl;
    uint32_t zone_idx = zns_zone_idx(ns, slba);

    assert(zone_idx < n->num_zones);
    return &n->zone_array[zone_idx];
}

static int zns_init_zone_geometry(NvmeNamespace *ns, Error **errp)
{
    FemuCtrl *n = ns->ctrl;
    uint64_t zone_size, zone_cap;
    uint32_t lbasz = 1 << zns_ns_lbads(ns);

    if (n->zone_size_bs) {
        zone_size = n->zone_size_bs;
    } else {
        zone_size = NVME_DEFAULT_ZONE_SIZE;
    }

    if (n->zone_cap_bs) {
        zone_cap = n->zone_cap_bs;
    } else {
        zone_cap = zone_size;
    }

    if (zone_cap > zone_size) {
        femu_err("zone capacity %luB > zone size %luB", zone_cap, zone_size);
        return -1;
    }
    if (zone_size < lbasz) {
        femu_err("zone size %luB too small, must >= %uB", zone_size, lbasz);
        return -1;
    }
    if (zone_cap < lbasz) {
        femu_err("zone capacity %luB too small, must >= %uB", zone_cap, lbasz);
        return -1;
    }

    n->zone_size = zone_size / lbasz;
    n->zone_capacity = zone_cap / lbasz;
    n->num_zones = ns->size / lbasz / n->zone_size;
    
    femu_debug("[znbc]zns.c::zns_init_zone_geometry: n->zone_size = %lu n->zone_capacity = %lu n->num_zones = %u\n", n->zone_size, n->zone_capacity, n->num_zones);

    if (n->max_open_zones > n->num_zones) {
        femu_err("max_open_zones value %u exceeds the number of zones %u",
                 n->max_open_zones, n->num_zones);
        return -1;
    }
    if (n->max_active_zones > n->num_zones) {
        femu_err("max_active_zones value %u exceeds the number of zones %u",
                 n->max_active_zones, n->num_zones);
        return -1;
    }

    if (n->zd_extension_size) {
        if (n->zd_extension_size & 0x3f) {
            femu_err("zone descriptor extension size must be multiples of 64B");
            return -1;
        }
        if ((n->zd_extension_size >> 6) > 0xff) {
            femu_err("zone descriptor extension size is too large");
            return -1;
        }
    }

    return 0;
}

#ifdef USE_SUBSUPERBLOCK
// added by znbc, get a free sub-superblock
static uint64_t get_subsuperblock(FemuCtrl *n, uint32_t zone_idx){
    struct zns_ssd *zns = n->zns;
    for(int i = 0; i < zns->num_ssblk; i++){
        if(! zns->ssblk[i].used){
            zns->ssblk[i].used = 1;
            zns->ssblk[i].to_zone = zone_idx;
            femu_debug("[znbc] zns.c::get_subsuperblock : give ssblk(%d) to zone(%d).\n", i, zns->ssblk[i].to_zone);
            return i;
        }
    }
    femu_err("ERROR [znbc] zns.c::get_subsuperblock : Cannot get a free sub-superblock for zone(%d)!\n", zone_idx);
    for(int i = 0; i < zns->num_ssblk; i++){
        if(zns->ssblk[i].used){
            femu_debug("%d,", zns->ssblk[i].to_zone);
        }
    }
    femu_debug("\n[znbc]  zns->num_ssblk = %ld\n.", zns->num_ssblk);
    assert(0);
    return -1;
}
#endif

#ifdef BALLOON_ZNS
static void zns_zone_additional_clear_bz(FemuCtrl *n){
    femu_debug("[znbc] zns.c::zns_zone_additional_clear_bz clear zones for Balloon-ZNS...\n");
    NvmeZone *zone;
    zone = n->zone_array;
    struct zns_ssd *zns = n->zns;
    #ifdef TEST_COMP_EFFECT
    zns->tot_ppa_size = zns->tot_comp_size = 0;
    #endif
    int i;
    for (i = 0; i < n->num_zones; i++, zone++) {
        #ifdef USE_SUBSUPERBLOCK
        zone->num_ssblk = 0;
        zone->ssblk_idx = 0;
        #endif
        #ifdef USE_SLOT
        #ifdef COMP_ADAPTIVE_SLOTTING
        for(int j = 0; j < ZONE_SIZE_TO_PROFILING_WINDOW_SIZE_RATIO; j++){
            memset(zone->pfwd[j].percentile_cnt, 0, sizeof(zone->pfwd[j].percentile_cnt));
            zone->pfwd[j].len = 0;
            zone->pfwd[j].slot_size_percentile = INITIAL_SLOT_SIZE_TO_PAGE_SIZE_PERCENTILE;
            zone->pfwd[j].slot_size_bs = UPPER(LOGICAL_PAGE_SIZE * INITIAL_SLOT_SIZE_TO_PAGE_SIZE_PERCENTILE / 100, SLOT_SIZE_BASE);
        }
        #endif

        #ifdef IncPFWD_NoRes
        for(int j = 0; j < zone->pfwd_maxcnt; j++){
            zone->pfwd[j].len = 0;
            zone->pfwd[j].slot_size_bs = 0;
        }
        zone->pfwd_cnt = 0;
        zone->last_pfwd = 0;
        #endif

        #endif
        #ifdef BALLOON_ZNS_RESIDUE
        zone->num_exssblk = 0;
        zone->exssblk_idx = 0;
        #endif
    }
    #ifdef USE_SUBSUPERBLOCK
    zns->wp_bz.ch = 0;
    for(i = 0; i < zns->num_ssblk; i++){
        if(zns->ssblk[i].used){
            zns->ssblk[i].used = 0;
            zns->ssblk[i].to_zone = 0;
            zns->ssblk[i].is_ext = 0;
            zns->ssblk[i].write_pointer = 0;
        }
    }
    #ifdef BALLOON_ZNS_RESIDUE
    zns->now_exssblk = zns->num_ssblk - 1;
    #endif

    #endif

    for (i = 0; i < zns->l2p_sz; i++) {
        zns->maptbl[i].ppa = UNMAPPED_PPA;
    }

    for(i =0; i < zns->cache.num_wc; i++)
    {
        zns->cache.write_cache[i].sblk = i;
        zns->cache.write_cache[i].used = 0;
        #ifdef CR_ADAPT_WC
        zns->cache.write_cache[i].used_size = 0;
        #endif
    }
}
#endif

static void zns_init_zoned_state(NvmeNamespace *ns)
{
    FemuCtrl *n = ns->ctrl;
    uint64_t start = 0, zone_size = n->zone_size;
    uint64_t capacity = n->num_zones * zone_size;
    NvmeZone *zone;
    int i;

    n->zone_array = g_new0(NvmeZone, n->num_zones);
    if (n->zd_extension_size) {
        n->zd_extensions = g_malloc0(n->zd_extension_size * n->num_zones);
    }

    QTAILQ_INIT(&n->exp_open_zones);
    QTAILQ_INIT(&n->imp_open_zones);
    QTAILQ_INIT(&n->closed_zones);
    QTAILQ_INIT(&n->full_zones);

    zone = n->zone_array;
    for (i = 0; i < n->num_zones; i++, zone++) {
        if (start + zone_size > capacity) {
            zone_size = capacity - start;
        }
        zone->d.zt = NVME_ZONE_TYPE_SEQ_WRITE;
        zns_set_zone_state(zone, NVME_ZONE_STATE_EMPTY);
        zone->d.za = 0;
        zone->d.zcap = n->zone_capacity;
        zone->d.zslba = start;
        zone->d.wp = start;
        zone->w_ptr = start;
        start += zone_size;
        
        #ifdef USE_SUBSUPERBLOCK
        // added by znbc, init the sub-superblocks mapped by each zone
        zone->num_ssblk = 0;
        zone->ssblk = g_malloc0(sizeof(u_int64_t) * SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO);
        zone->ssblk_idx = 0;
        #endif

        #ifdef USE_SLOT

        #if !defined(IncPFWD_NoRes)
        zone->pfwd = g_malloc(sizeof(ProfilingWindow) * (ZONE_SIZE_TO_PROFILING_WINDOW_SIZE_RATIO + 1));
        zone->pfwd_maxcnt = ZONE_SIZE_TO_PROFILING_WINDOW_SIZE_RATIO;
        #else
        zone->pfwd = g_malloc(sizeof(ProfilingWindow) * (LOGICAL_PAGE_SIZE / SLOT_SIZE_BASE + 1));
        zone->pfwd_maxcnt = LOGICAL_PAGE_SIZE / SLOT_SIZE_BASE;
        zone->pfwd_cnt = 0;
        zone->last_pfwd = 0;
        #endif

        for(int j = 0; j < zone->pfwd_maxcnt; j++){
            memset(zone->pfwd[j].percentile_cnt, 0, sizeof(zone->pfwd[j].percentile_cnt));
            zone->pfwd[j].len = 0;
            zone->pfwd[j].slot_size_percentile = INITIAL_SLOT_SIZE_TO_PAGE_SIZE_PERCENTILE;
            zone->pfwd[j].slot_size_bs = UPPER(LOGICAL_PAGE_SIZE * INITIAL_SLOT_SIZE_TO_PAGE_SIZE_PERCENTILE / 100, SLOT_SIZE_BASE);
        }
        #endif

        #ifdef BALLOON_ZNS_RESIDUE
        zone->num_exssblk = 0;
        zone->exssblk = g_malloc0(sizeof(u_int64_t) * SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO);
        zone->exssblk_idx = 0;
        #endif
    }

    n->zone_size_log2 = 0;
    if (is_power_of_2(n->zone_size)) {
        n->zone_size_log2 = 63 - clz64(n->zone_size);
    }
}

static void zns_init_zone_identify(FemuCtrl *n, NvmeNamespace *ns, int lba_index)
{
    NvmeIdNsZoned *id_ns_z;

    zns_init_zoned_state(ns);

    id_ns_z = g_malloc0(sizeof(NvmeIdNsZoned));

    /* MAR/MOR are zeroes-based, 0xffffffff means no limit */
    id_ns_z->mar = cpu_to_le32(n->max_active_zones - 1);
    id_ns_z->mor = cpu_to_le32(n->max_open_zones - 1);
    id_ns_z->zoc = 0;
    id_ns_z->ozcs = n->cross_zone_read ? 0x01 : 0x00;

    id_ns_z->lbafe[lba_index].zsze = cpu_to_le64(n->zone_size);
    id_ns_z->lbafe[lba_index].zdes = n->zd_extension_size >> 6; /* Units of 64B */

    n->csi = NVME_CSI_ZONED;
    ns->id_ns.nsze = cpu_to_le64(n->num_zones * n->zone_size);
    ns->id_ns.ncap = ns->id_ns.nsze;
    ns->id_ns.nuse = ns->id_ns.ncap;

    ns->id_ns.noiob = 1;
    /* NvmeIdNs */
    /*
     * The device uses the BDRV_BLOCK_ZERO flag to determine the "deallocated"
     * status of logical blocks. Since the spec defines that logical blocks
     * SHALL be deallocated when then zone is in the Empty or Offline states,
     * we can only support DULBE if the zone size is a multiple of the
     * calculated NPDG.
     */
    if (n->zone_size % (ns->id_ns.npdg + 1)) {
        femu_err("the zone size (%"PRIu64" blocks) is not a multiple of the"
                 "calculated deallocation granularity (%"PRIu16" blocks); DULBE"
                 "support disabled", n->zone_size, ns->id_ns.npdg + 1);
        ns->id_ns.nsfeat &= ~0x4;
    }

    n->id_ns_zoned = id_ns_z;
}

static void zns_clear_zone(NvmeNamespace *ns, NvmeZone *zone)
{
    FemuCtrl *n = ns->ctrl;
    uint8_t state;

    zone->w_ptr = zone->d.wp;
    femu_debug("[znbc] zns.c::zns_clear_zone closing zone w_ptr = %lu\n", zone->w_ptr);
    
    state = zns_get_zone_state(zone);
    if (zone->d.wp != zone->d.zslba || (zone->d.za & NVME_ZA_ZD_EXT_VALID)) {
        if (state != NVME_ZONE_STATE_CLOSED) {
            zns_set_zone_state(zone, NVME_ZONE_STATE_CLOSED);
        }
        zns_aor_inc_active(ns);
        QTAILQ_INSERT_HEAD(&n->closed_zones, zone, entry);
    } else {
        zns_set_zone_state(zone, NVME_ZONE_STATE_EMPTY);
    }
}

static void zns_zoned_ns_shutdown(NvmeNamespace *ns)
{
    femu_debug("[znbc] zns.c::zns_zoned_ns_shutdown shutdowning...\n");
    FemuCtrl *n = ns->ctrl;
    NvmeZone *zone, *next;

    QTAILQ_FOREACH_SAFE(zone, &n->closed_zones, entry, next) {
        QTAILQ_REMOVE(&n->closed_zones, zone, entry);
        zns_aor_dec_active(ns);
        zns_clear_zone(ns, zone);
    }
    QTAILQ_FOREACH_SAFE(zone, &n->imp_open_zones, entry, next) {
        QTAILQ_REMOVE(&n->imp_open_zones, zone, entry);
        zns_aor_dec_open(ns);
        zns_aor_dec_active(ns);
        zns_clear_zone(ns, zone);
    }
    QTAILQ_FOREACH_SAFE(zone, &n->exp_open_zones, entry, next) {
        QTAILQ_REMOVE(&n->exp_open_zones, zone, entry);
        zns_aor_dec_open(ns);
        zns_aor_dec_active(ns);
        zns_clear_zone(ns, zone);
    }

    assert(n->nr_open_zones == 0);
}

void zns_ns_shutdown(NvmeNamespace *ns)
{
    FemuCtrl *n = ns->ctrl;
    if (n->zoned) {
        zns_zoned_ns_shutdown(ns);
    }
}

void zns_ns_cleanup(NvmeNamespace *ns)
{
    FemuCtrl *n = ns->ctrl;
    if (n->zoned) {
        g_free(n->id_ns_zoned);
        g_free(n->zone_array);
        g_free(n->zd_extensions);
    }
}

static void zns_assign_zone_state(NvmeNamespace *ns, NvmeZone *zone, NvmeZoneState state)
{
    FemuCtrl *n = ns->ctrl;

    if (QTAILQ_IN_USE(zone, entry)) {
        switch (zns_get_zone_state(zone)) {
        case NVME_ZONE_STATE_EXPLICITLY_OPEN:
            QTAILQ_REMOVE(&n->exp_open_zones, zone, entry);
            break;
        case NVME_ZONE_STATE_IMPLICITLY_OPEN:
            QTAILQ_REMOVE(&n->imp_open_zones, zone, entry);
            break;
        case NVME_ZONE_STATE_CLOSED:
            QTAILQ_REMOVE(&n->closed_zones, zone, entry);
            break;
        case NVME_ZONE_STATE_FULL:
            QTAILQ_REMOVE(&n->full_zones, zone, entry);
        default:
            ;
        }
    }

    zns_set_zone_state(zone, state);

    switch (state) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        QTAILQ_INSERT_TAIL(&n->exp_open_zones, zone, entry);
        break;
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        QTAILQ_INSERT_TAIL(&n->imp_open_zones, zone, entry);
        break;
    case NVME_ZONE_STATE_CLOSED:
        QTAILQ_INSERT_TAIL(&n->closed_zones, zone, entry);
        break;
    case NVME_ZONE_STATE_FULL:
        QTAILQ_INSERT_TAIL(&n->full_zones, zone, entry);
    case NVME_ZONE_STATE_READ_ONLY:
        break;
    default:
        zone->d.za = 0;
    }
}

/*
 * Check if we can open a zone without exceeding open/active limits.
 * AOR stands for "Active and Open Resources" (see TP 4053 section 2.5).
 */
static int zns_aor_check(NvmeNamespace *ns, uint32_t act, uint32_t opn)
{
    FemuCtrl *n = ns->ctrl;
    if (n->max_active_zones != 0 &&
        n->nr_active_zones + act > n->max_active_zones) {
        return NVME_ZONE_TOO_MANY_ACTIVE | NVME_DNR;
    }
    if (n->max_open_zones != 0 &&
        n->nr_open_zones + opn > n->max_open_zones) {
        return NVME_ZONE_TOO_MANY_OPEN | NVME_DNR;
    }

    return NVME_SUCCESS;
}

/*added by zwl, zns oob area*/

/*Write a single out-of-bound (OOB) area entry*/
// static int zns_write_oob_meta(FemuCtrl *n, uint64_t lba, void *meta)
// {
//     zns_;
//     uint64_t sec_idx = ppa2secidx(n, ppa);
//     uint64_t oft = sec_idx * n->meta_len + n->int_meta_size;
//     uint8_t *tgt_sos_meta_buf = &n->meta_buf[oft];

//     assert(oft + n->zns_params.sos < n->meta_tbytes);
//     memcpy(tgt_sos_meta_buf, meta, n->zns_params.sos);

//     return 0;
// }

static uint16_t zns_check_zone_state_for_write(NvmeZone *zone)
{
    uint16_t status;

    switch (zns_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EMPTY:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_CLOSED:
        status = NVME_SUCCESS;
        break;
    case NVME_ZONE_STATE_FULL:
        status = NVME_ZONE_FULL;
        break;
    case NVME_ZONE_STATE_OFFLINE:
        status = NVME_ZONE_OFFLINE;
        break;
    case NVME_ZONE_STATE_READ_ONLY:
        status = NVME_ZONE_READ_ONLY;
        break;
    default:
        assert(false);
    }

    return status;
}

static uint16_t zns_check_zone_write(FemuCtrl *n, NvmeNamespace *ns,
                                     NvmeZone *zone, uint64_t slba,
                                     uint32_t nlb, bool append)
{
    uint16_t status;

    if (unlikely((slba + nlb) > zns_zone_wr_boundary(zone))) {
        status = NVME_ZONE_BOUNDARY_ERROR;
    } else {
        status = zns_check_zone_state_for_write(zone);
    }

    if (status != NVME_SUCCESS) {
    } else {
        assert(zns_wp_is_valid(zone));
        if (append) {
            if (unlikely(slba != zone->d.zslba)) {
                status = NVME_INVALID_FIELD;
            }
            if (zns_l2b(ns, nlb) > (n->page_size << n->zasl)) {
                status = NVME_INVALID_FIELD;
            }
        } else if (unlikely(slba != zone->w_ptr)) {
            status = NVME_ZONE_INVALID_WRITE;
        }
    }

    return status;
}

static uint16_t zns_check_zone_state_for_read(NvmeZone *zone)
{
    uint16_t status;

    switch (zns_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EMPTY:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_FULL:
    case NVME_ZONE_STATE_CLOSED:
    case NVME_ZONE_STATE_READ_ONLY:
        status = NVME_SUCCESS;
        break;
    case NVME_ZONE_STATE_OFFLINE:
        status = NVME_ZONE_OFFLINE;
        break;
    default:
        assert(false);
    }

    return status;
}

static uint16_t zns_check_zone_read(NvmeNamespace *ns, uint64_t slba, uint32_t nlb)
{
    FemuCtrl *n = ns->ctrl;
    NvmeZone *zone = zns_get_zone_by_slba(ns, slba);
    uint64_t bndry = zns_zone_rd_boundary(ns, zone);
    uint64_t end = slba + nlb;
    uint16_t status;

    status = zns_check_zone_state_for_read(zone);
    if (status != NVME_SUCCESS) {
        ;
    } else if (unlikely(end > bndry)) {
        if (!n->cross_zone_read) {
            status = NVME_ZONE_BOUNDARY_ERROR;
        } else {
            /*
             * Read across zone boundary - check that all subsequent
             * zones that are being read have an appropriate state.
             */
            do {
                zone++;
                status = zns_check_zone_state_for_read(zone);
                if (status != NVME_SUCCESS) {
                    break;
                }
            } while (end > zns_zone_rd_boundary(ns, zone));
        }
    }

    return status;
}

static void zns_auto_transition_zone(NvmeNamespace *ns)
{
    FemuCtrl *n = ns->ctrl;
    NvmeZone *zone;

    if (n->max_open_zones &&
        n->nr_open_zones == n->max_open_zones) {
        zone = QTAILQ_FIRST(&n->imp_open_zones);
        if (zone) {
             /* Automatically close this implicitly open zone */
            QTAILQ_REMOVE(&n->imp_open_zones, zone, entry);
            zns_aor_dec_open(ns);
            zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_CLOSED);
        }
    }
}

static uint16_t zns_auto_open_zone(NvmeNamespace *ns, NvmeZone *zone)
{
    uint16_t status = NVME_SUCCESS;
    uint8_t zs = zns_get_zone_state(zone);

    if (zs == NVME_ZONE_STATE_EMPTY) {
        zns_auto_transition_zone(ns);
        status = zns_aor_check(ns, 1, 1);
    } else if (zs == NVME_ZONE_STATE_CLOSED) {
        zns_auto_transition_zone(ns);
        status = zns_aor_check(ns, 0, 1);
    }

    return status;
}

static void zns_finalize_zoned_write(NvmeNamespace *ns, NvmeRequest *req, bool failed)
{
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd;
    NvmeZone *zone;
    NvmeZonedResult *res = (NvmeZonedResult *)&req->cqe;
    uint64_t slba;
    uint32_t nlb;

    slba = le64_to_cpu(rw->slba);
    nlb = le16_to_cpu(rw->nlb) + 1;
    zone = zns_get_zone_by_slba(ns, slba);

    zone->d.wp += nlb;

    if (failed) {
        res->slba = 0;
    }

    if (zone->d.wp == zns_zone_wr_boundary(zone)) {
        switch (zns_get_zone_state(zone)) {
        case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        case NVME_ZONE_STATE_EXPLICITLY_OPEN:
            zns_aor_dec_open(ns);
            /* fall through */
        case NVME_ZONE_STATE_CLOSED:
            zns_aor_dec_active(ns);
            /* fall through */
        case NVME_ZONE_STATE_EMPTY:
            zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_FULL);
            /* fall through */
        case NVME_ZONE_STATE_FULL:
            break;
        default:
            assert(false);
        }
    }
}

// Add some function
// --------------------------------

static inline uint64_t zone_slba(FemuCtrl *n, uint32_t zone_idx)
{
    return (zone_idx) * n->zone_size;
}

//static uint64_t zns_advance_zone_wp(NvmeNamespace *ns, NvmeZone *zone, uint32_t nlb)
static uint64_t zns_advance_zone_wp(NvmeNamespace *ns, NvmeZone *zone, uint32_t nlb, uint64_t slba)
{
    uint64_t result = zone->w_ptr;
    uint8_t zs;

    zone->w_ptr += nlb;

    #ifdef USE_SUBSUPERBLOCK
    // add by znbc
    femu_debug("[znbc] zns.c::zns_advance_zone_wp zone->w_ptr-zone->d.zslba=%ld-%ld=%ld  zone->num_ssblk=%ld ns->ctrl->zone_size=%ld next_limit=%ld\n",zone->w_ptr, zone->d.zslba, zone->w_ptr - zone->d.zslba, zone->num_ssblk, ns->ctrl->zone_size, zone->num_ssblk * ns->ctrl->zone_size / SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO);
    if(zone->w_ptr - zone->d.zslba > zone->num_ssblk * ns->ctrl->zone_size / SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO){
        if(zone->num_ssblk < SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO){
            zone->ssblk[zone->num_ssblk++] = get_subsuperblock(ns->ctrl, zns_zone_idx(ns, slba));
        }
        else{
            femu_debug("[znbc] zns.c::zns_advance_zone_wp : Should not have more sub-superblocks!\n");
        }
    }
    #endif

    if (zone->w_ptr < zns_zone_wr_boundary(zone)) {
        zs = zns_get_zone_state(zone);
        switch (zs) {
        case NVME_ZONE_STATE_EMPTY:
            zns_aor_inc_active(ns);
            /* fall through */
        case NVME_ZONE_STATE_CLOSED:
            zns_aor_inc_open(ns);
            zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_IMPLICITLY_OPEN);
        }
    }

    return result;
}

struct zns_zone_reset_ctx {
    NvmeRequest *req;
    NvmeZone    *zone;
};

static void zns_aio_zone_reset_cb(NvmeRequest *req, NvmeZone *zone)
{
    NvmeNamespace *ns = req->ns;

    /* FIXME, We always assume reset SUCCESS */
    switch (zns_get_zone_state(zone)) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        /* fall through */
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        zns_aor_dec_open(ns);
        /* fall through */
    case NVME_ZONE_STATE_CLOSED:
        zns_aor_dec_active(ns);
        /* fall through */
    case NVME_ZONE_STATE_FULL:
        zone->w_ptr = zone->d.zslba;
        zone->d.wp = zone->w_ptr;
        zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_EMPTY);
    default:
        break;
    }

#if 0
    FemuCtrl *n = ns->ctrl;
    int ch, lun;
    struct zns_ssd *zns = n->zns;
    uint64_t num_ch = zns->num_ch;
    uint64_t num_lun = zns->num_lun;

    struct ppa ppa;
    for (ch = 0; ch < num_ch; ch++) {
        for (lun = 0; lun < num_lun; lun++) {
            ppa.g.ch = ch;
            ppa.g.fc = lun;
            ppa.g.blk = zns_zone_idx(ns, zone->d.zslba);
            //FIXME: no erase
        }
    }
#endif
}

typedef uint16_t (*op_handler_t)(NvmeNamespace *, NvmeZone *, NvmeZoneState,
                                 NvmeRequest *);

enum NvmeZoneProcessingMask {
    NVME_PROC_CURRENT_ZONE    = 0,
    NVME_PROC_OPENED_ZONES    = 1 << 0,
    NVME_PROC_CLOSED_ZONES    = 1 << 1,
    NVME_PROC_READ_ONLY_ZONES = 1 << 2,
    NVME_PROC_FULL_ZONES      = 1 << 3,
};

static uint16_t zns_open_zone(NvmeNamespace *ns, NvmeZone *zone,
                              NvmeZoneState state, NvmeRequest *req)
{
    uint16_t status;

    switch (state) {
    case NVME_ZONE_STATE_EMPTY:
        status = zns_aor_check(ns, 1, 0);
        if (status != NVME_SUCCESS) {
            return status;
        }
        zns_aor_inc_active(ns);
        /* fall through */
    case NVME_ZONE_STATE_CLOSED:
        status = zns_aor_check(ns, 0, 1);
        if (status != NVME_SUCCESS) {
            if (state == NVME_ZONE_STATE_EMPTY) {
                zns_aor_dec_active(ns);
            }
            return status;
        }
        zns_aor_inc_open(ns);
        /* fall through */
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_EXPLICITLY_OPEN);
        /* fall through */
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        return NVME_SUCCESS;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t zns_close_zone(NvmeNamespace *ns, NvmeZone *zone,
                               NvmeZoneState state, NvmeRequest *req)
{
    switch (state) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        /* fall through */
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        zns_aor_dec_open(ns);
        zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_CLOSED);
        /* fall through */
    case NVME_ZONE_STATE_CLOSED:
        return NVME_SUCCESS;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t zns_finish_zone(NvmeNamespace *ns, NvmeZone *zone,
                                NvmeZoneState state, NvmeRequest *req)
{
    switch (state) {
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        /* fall through */
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
        zns_aor_dec_open(ns);
        /* fall through */
    case NVME_ZONE_STATE_CLOSED:
        zns_aor_dec_active(ns);
        /* fall through */
    case NVME_ZONE_STATE_EMPTY:
        zone->w_ptr = zns_zone_wr_boundary(zone);
        zone->d.wp = zone->w_ptr;
        zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_FULL);
        /* fall through */
    case NVME_ZONE_STATE_FULL:
        return NVME_SUCCESS;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t zns_reset_zone(NvmeNamespace *ns, NvmeZone *zone,
                               NvmeZoneState state, NvmeRequest *req)
{
    switch (state) {
    case NVME_ZONE_STATE_EMPTY:
        return NVME_SUCCESS;
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_CLOSED:
    case NVME_ZONE_STATE_FULL:
        break;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }

    zns_aio_zone_reset_cb(req, zone);

    return NVME_SUCCESS;
}

static uint16_t zns_offline_zone(NvmeNamespace *ns, NvmeZone *zone,
                                 NvmeZoneState state, NvmeRequest *req)
{
    switch (state) {
    case NVME_ZONE_STATE_READ_ONLY:
        zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_OFFLINE);
        /* fall through */
    case NVME_ZONE_STATE_OFFLINE:
        return NVME_SUCCESS;
    default:
        return NVME_ZONE_INVAL_TRANSITION;
    }
}

static uint16_t zns_set_zd_ext(NvmeNamespace *ns, NvmeZone *zone)
{
    uint16_t status;
    uint8_t state = zns_get_zone_state(zone);

    if (state == NVME_ZONE_STATE_EMPTY) {
        status = zns_aor_check(ns, 1, 0);
        if (status != NVME_SUCCESS) {
            return status;
        }
        zns_aor_inc_active(ns);
        zone->d.za |= NVME_ZA_ZD_EXT_VALID;
        zns_assign_zone_state(ns, zone, NVME_ZONE_STATE_CLOSED);
        return NVME_SUCCESS;
    }

    return NVME_ZONE_INVAL_TRANSITION;
}

static uint16_t zns_bulk_proc_zone(NvmeNamespace *ns, NvmeZone *zone,
                                   enum NvmeZoneProcessingMask proc_mask,
                                   op_handler_t op_hndlr, NvmeRequest *req)
{
    uint16_t status = NVME_SUCCESS;
    NvmeZoneState zs = zns_get_zone_state(zone);
    bool proc_zone;

    switch (zs) {
    case NVME_ZONE_STATE_IMPLICITLY_OPEN:
    case NVME_ZONE_STATE_EXPLICITLY_OPEN:
        proc_zone = proc_mask & NVME_PROC_OPENED_ZONES;
        break;
    case NVME_ZONE_STATE_CLOSED:
        proc_zone = proc_mask & NVME_PROC_CLOSED_ZONES;
        break;
    case NVME_ZONE_STATE_READ_ONLY:
        proc_zone = proc_mask & NVME_PROC_READ_ONLY_ZONES;
        break;
    case NVME_ZONE_STATE_FULL:
        proc_zone = proc_mask & NVME_PROC_FULL_ZONES;
        break;
    default:
        proc_zone = false;
    }

    if (proc_zone) {
        status = op_hndlr(ns, zone, zs, req);
    }

    return status;
}

static uint16_t zns_do_zone_op(NvmeNamespace *ns, NvmeZone *zone,
                               enum NvmeZoneProcessingMask proc_mask,
                               op_handler_t op_hndlr, NvmeRequest *req)
{
    FemuCtrl *n = ns->ctrl;
    NvmeZone *next;
    uint16_t status = NVME_SUCCESS;
    int i;

    if (!proc_mask) {
        status = op_hndlr(ns, zone, zns_get_zone_state(zone), req);
    } else {
        if (proc_mask & NVME_PROC_CLOSED_ZONES) {
            QTAILQ_FOREACH_SAFE(zone, &n->closed_zones, entry, next) {
                status = zns_bulk_proc_zone(ns, zone, proc_mask, op_hndlr, req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }
        if (proc_mask & NVME_PROC_OPENED_ZONES) {
            QTAILQ_FOREACH_SAFE(zone, &n->imp_open_zones, entry, next) {
                status = zns_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }

            QTAILQ_FOREACH_SAFE(zone, &n->exp_open_zones, entry, next) {
                status = zns_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }
        if (proc_mask & NVME_PROC_FULL_ZONES) {
            QTAILQ_FOREACH_SAFE(zone, &n->full_zones, entry, next) {
                status = zns_bulk_proc_zone(ns, zone, proc_mask, op_hndlr, req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }

        if (proc_mask & NVME_PROC_READ_ONLY_ZONES) {
            for (i = 0; i < n->num_zones; i++, zone++) {
                status = zns_bulk_proc_zone(ns, zone, proc_mask, op_hndlr,
                                             req);
                if (status && status != NVME_NO_COMPLETE) {
                    goto out;
                }
            }
        }
    }

out:
    return status;
}

static uint16_t zns_get_mgmt_zone_slba_idx(FemuCtrl *n, NvmeCmd *c,
                                           uint64_t *slba, uint32_t *zone_idx)
{
    NvmeNamespace *ns = &n->namespaces[0];
    uint32_t dw10 = le32_to_cpu(c->cdw10);
    uint32_t dw11 = le32_to_cpu(c->cdw11);

    if (!n->zoned) {
        return NVME_INVALID_OPCODE | NVME_DNR;
    }

    *slba = ((uint64_t)dw11) << 32 | dw10;
    if (unlikely(*slba >= ns->id_ns.nsze)) {
        *slba = 0;
        return NVME_LBA_RANGE | NVME_DNR;
    }

    *zone_idx = zns_zone_idx(ns, *slba);
    assert(*zone_idx < n->num_zones);

    return NVME_SUCCESS;
}

static inline uint16_t zns_check_bounds(NvmeNamespace *ns, uint64_t slba,
                                        uint32_t nlb)
{
    uint64_t nsze = le64_to_cpu(ns->id_ns.nsze);

    if (unlikely(UINT64_MAX - slba < nlb || slba + nlb > nsze)) {
        return NVME_LBA_RANGE | NVME_DNR;
    }

    return NVME_SUCCESS;
}

static uint16_t zns_check_dulbe(NvmeNamespace *ns, uint64_t slba, uint32_t nlb)
{
    return NVME_SUCCESS;
}

static uint16_t zns_map_dptr(FemuCtrl *n, size_t len, NvmeRequest *req)
{
    uint64_t prp1, prp2;

    switch (req->cmd.psdt) {
    case NVME_PSDT_PRP:
        prp1 = le64_to_cpu(req->cmd.dptr.prp1);
        prp2 = le64_to_cpu(req->cmd.dptr.prp2);

        return nvme_map_prp(&req->qsg, &req->iov, prp1, prp2, len, n);
    default:
        return NVME_INVALID_FIELD;
    }
}

#ifdef USE_SLOT
// added by znbc, get profiling window id by start lba
#if !defined(IncPFWD_NoRes)
static inline uint32_t zns_get_pfwd_id_by_slba(NvmeNamespace *ns, uint64_t slba, bool write)
{
    FemuCtrl *n = ns->ctrl;
    uint32_t zone_idx = zns_zone_idx(ns, slba);
    uint64_t zslba = zone_slba(n, zone_idx);
    return (slba - zslba) / (n->zone_size / ZONE_SIZE_TO_PROFILING_WINDOW_SIZE_RATIO);
}
#else
static inline uint32_t zns_get_pfwd_id_by_slba(NvmeNamespace *ns, uint64_t slba, bool write)
{
    FemuCtrl *n = ns->ctrl;
    uint32_t zone_idx = zns_zone_idx(ns, slba);
    uint64_t zslba = zone_slba(n, zone_idx);
    NvmeZone zone = n->zone_array[zone_idx];
    if(!write){
        uint64_t delta = slba - zslba;
        int i = 0;
        for(; i < zone.pfwd_cnt; i++){
            if(delta < zone.pfwd[i].len){
                break;
            }
            delta -= zone.pfwd[i].len;
        }
        return i;
    }
    return zone.last_pfwd;
}
#endif

#endif

#ifdef RESIDUE_NUMBER_COUNT
static int count_slot=0;
static int have_residue=0;
#endif

/*Misao: backend read/write without latency emulation*/
static uint16_t zns_nvme_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                           NvmeRequest *req,bool append)
{
    // printf("zns_nvme_rw\n");
    NvmeRwCmd *rw = (NvmeRwCmd *)&req->cmd; 
    uint64_t slba = le64_to_cpu(rw->slba);
    uint32_t nlb = (uint32_t)le16_to_cpu(rw->nlb) + 1;
    uint64_t data_size = zns_l2b(ns, nlb);
    uint64_t data_offset;
    #ifdef NO_FUNC
    uint64_t data_offset2;
    #endif
    uint16_t status;

    
    NvmeZonedResult *res = (NvmeZonedResult *)&req->cqe;
    assert(n->zoned);
    req->is_write = ((rw->opcode == NVME_CMD_WRITE) || (rw->opcode == NVME_CMD_ZONE_APPEND)) ? 1 : 0;
    // printf("zns_nvme_rw is_write %d\n",req->is_write);
    // printf("nvme command:%u\n",rw->opcode);
    status = nvme_check_mdts(n, data_size);
    if (status) {
        goto err;
    }

    status = zns_check_bounds(ns, slba, nlb);
    if (status) {
        goto err;
    }
    
    NvmeZone *zone;
    zone = zns_get_zone_by_slba(ns, slba);

    #ifdef USE_SLOT
    // added by znbc
    uint32_t pfwd_id = zns_get_pfwd_id_by_slba(ns, slba, req->is_write);
    femu_debug("[znbc] zns.c::zns_nvme_rw : pfwd_id = %d\n", pfwd_id);
    //uint64_t ssp = zone->pfwd[pfwd_id].slot_size_percentile ? zone->pfwd[pfwd_id].slot_size_percentile : INITIAL_SLOT_SIZE_TO_PAGE_SIZE_PERCENTILE;
    #endif

    if(req->is_write)
    {
        // printf("zns_nvme_rw write\n");
        status = zns_check_zone_write(n, ns, zone, slba, nlb, append);
        if (status) {
            femu_err("Misao check zone write failed with status (%u)\n",status);
            goto err;
        }
        if(append)
        {
             status = zns_auto_open_zone(ns, zone);
             if(status)
             {
                goto err;
             }
             slba = zone->w_ptr;
        }

        
        res->slba = zns_advance_zone_wp(ns, zone, nlb, slba);
    }
    else
    {
        status = zns_check_zone_read(ns, slba, nlb);
        if (status) {
            goto err;
        }

        /* Misao
           Deallocated or Unwritten Logical Block Error (DULBE) is an option on
           NVMe drives that allows a storage array to deallocate blocks that are
           part of a volume. Deallocating blocks on a drive can greatly reduce
           the time it takes to initialize volumes. In addition, hosts can
           deallocate logical blocks in the volume using the NVMe Dataset
           Management command.
        */
        if (NVME_ERR_REC_DULBE(n->features.err_rec)) { status =
            zns_check_dulbe(ns, slba, nlb); if (status) { goto err; } } }

    data_offset = zns_l2b(ns, slba);
    #ifdef NO_FUNC
    data_offset2 = zns_l2b(ns, slba);
    #endif
    status = zns_map_dptr(n, data_size, req);
    if (status) {
        goto err;
    }

    req->slba = slba;
    req->status = NVME_SUCCESS;
    req->nlb = nlb;

    if(rw->opcode == NVME_CMD_WRITE){
        //printf("nvme write\n");
    }
    else if(rw->opcode == NVME_CMD_ZONE_APPEND){
        //printf("nvme zone append\n");
    }

    #ifdef COMPQAT
    if(((rw->opcode == NVME_CMD_WRITE) || (rw->opcode == NVME_CMD_ZONE_APPEND))){
         #ifdef NO_FUNC
        //int sg_cur_index = 0;
        //dma_addr_t sg_cur_byte = 0;
        uint64_t mb_oft_2 = (&data_offset2)[0];
        void *mb_2 = n->mbe->logical_space;
        req->compressed_size = g_malloc(sizeof(uint32_t) * req->qsg.nsg);
        // znbc: the len of sg may be different (eg. when doing rocksdb test)
        u_int64_t len = req->qsg.sg[0].len; 
        int nsg = req->qsg.nsg;
        femu_debug("[znbc] zns.c::zns_nvme_rw : sg[0].len = %lu sg[1].len = %lu sg[2].len = %lu, nsg = %d slba = %lu nlb = %u\n", req->qsg.sg[0].len, req->qsg.sg[1].len, req->qsg.sg[2].len, req->qsg.nsg, slba, nlb);
        #ifdef USE_SLOT
        femu_debug("[znbc] zns.c::zns_nvme_rw : pwd_size = %lu mb_oft=%lu\n", n->zns->profiling_window_size, mb_oft_2);
        #endif
        //qat_dc_compress(n,0,mb_2+mb_oft_2,req->qsg.sg[0].len, req->compressed_size,req->qsg.nsg);
    
        #ifdef DIFFERENT_SG_LEN
        u_int64_t *qsg_len = g_malloc(sizeof(u_int64_t) * req->qsg.nsg);
        for(int i = 0; i < nsg; i++){
            qsg_len[i] = req->qsg.sg[i].len;
        }
        #endif

        backend_rw(n->mbe, &req->qsg, &data_offset, req->is_write);
        //printf("nvme write\n");

        #ifdef DIFFERENT_SG_LEN
        qat_dc_compress_sg(n,0, mb_2+mb_oft_2, qsg_len, req->compressed_size, nsg); // added by znbc, for different sg length
        #else
        qat_dc_compress(n,0,mb_2+mb_oft_2,len, req->compressed_size, nsg);
        #endif

        //femu_debug("qat_dc_compress over!\n");
        #ifdef USE_SLOT

        #ifdef SG_LEN_EQU_LOGICAL_PAGE_SIZE
        // for some reason the rocksdb have inputs which sg.len != 4096, I forcefully expanded them to 4096 here, there may be other better ways...
        assert(len <= LOGICAL_PAGE_SIZE);
        if(len != LOGICAL_PAGE_SIZE)
            len = LOGICAL_PAGE_SIZE;
        #endif
        uint64_t comp_tot_size = 0;
        for(int i = 0; i < nsg; i++){
            comp_tot_size += req->compressed_size[i];
        }

        #ifdef IncPFWD_NoRes
        zone->pfwd[pfwd_id].len += comp_tot_size;
        int max_comp_size = 0;
        for(int j = 0; j < nsg; j++){
            if(req->compressed_size[j] > max_comp_size)
                max_comp_size = req->compressed_size[j];
        }
        ProfilingWindow *pfwd = &zone->pfwd[pfwd_id];
        if(pfwd->slot_size_bs == 0){
            pfwd->slot_size_bs = UPPER(max_comp_size, SLOT_SIZE_BASE);
        }else if(pfwd->slot_size_bs < max_comp_size){
            pfwd_id++;
            pfwd = &zone->pfwd[pfwd_id];
            pfwd->slot_size_bs = UPPER(max_comp_size, SLOT_SIZE_BASE);
            zone->last_pfwd = pfwd_id;
            zone->pfwd_cnt++;
            femu_debug("[znbc] IncPFWD_NoRes zns.c::zns_nvme_rw : zone->pfwd[%u].slot_size_bs = %u\n",\
            pfwd_id, zone->pfwd[pfwd_id].slot_size_bs);
        }
        #endif

        

        #ifdef COMP_ADAPTIVE_SLOTTING
        zone->pfwd[pfwd_id].len += len * nsg;
        // added by znbc, update the profiling window
        for(int j = 0; j < nsg; j++){
            zone->pfwd[pfwd_id].percentile_cnt[req->compressed_size[j] * 100 / len] ++;
        }
        
        if(zone->pfwd[pfwd_id].len >= n->zns->profiling_window_size){
            // calculate the slot size
            uint64_t cnt = 0, tot = n->zns->profiling_window_size / len;

            #ifdef FEMU_DEBUG_NVME
            for(int k = 0; k <= 100; k++){
                printf("%lu ", zone->pfwd[pfwd_id].percentile_cnt[k]);
            }
            printf("\n");
            #endif

            for(int k = 0; k <= 100; k++){
                cnt += zone->pfwd[pfwd_id].percentile_cnt[k];
                if(cnt * 100 >= tot * CR_VALUE_PERCENTILE){
                    femu_debug("zone->pfwd[%d].percentile_cnt[%d]=%lu cnt=%lu tot=%lu %lu\n",pfwd_id, k, zone->pfwd[pfwd_id].percentile_cnt[k],  cnt, tot, cnt * 100 - tot * CR_VALUE_PERCENTILE);
                    if(!k) continue;
                    // update the next profiling window
                    zone->pfwd[pfwd_id + 1].slot_size_percentile = k;
                    #ifdef BIGGER_SLOT_SIZE
                    zone->pfwd[pfwd_id + 1].slot_size_bs = UPPER((LOGICAL_PAGE_SIZE * k / 100), SLOT_SIZE_BASE) + SLOT_SIZE_BASE; // can reduce residue number
                    #else
                    zone->pfwd[pfwd_id + 1].slot_size_bs = UPPER((LOGICAL_PAGE_SIZE * k / 100), SLOT_SIZE_BASE); // same as Balloon-ZNS paper
                    #endif
                    if(zone->pfwd[pfwd_id + 1].slot_size_bs > LOGICAL_PAGE_SIZE) zone->pfwd[pfwd_id + 1].slot_size_bs = LOGICAL_PAGE_SIZE;
                    femu_debug("[znbc] zns.c::zns_nvme_rw : zone->pfwd[%u].slot_size_percentile = %u slot_size_bs = %u\n", pfwd_id+1, zone->pfwd[pfwd_id + 1].slot_size_percentile, zone->pfwd[pfwd_id + 1].slot_size_bs);
                    break;
                }
            }
        }
        #endif

        #endif

        #ifdef FEMU_DEBUG_NVME
        printf("zns.c::zns_nvme_rw : compressed_size: ");
        for(int j=0;j<nsg;j++)
            printf("%d, ",req->compressed_size[j]);
        printf("\n");
        #endif

        #ifdef USE_SLOT
        assert(len == LOGICAL_PAGE_SIZE); // So that the nsg is the number of logical pages.(Not necessary. If not, the codes below may need change).
        uint64_t secs_per_pg = LOGICAL_PAGE_SIZE/n->zns->lbasz;
        uint64_t start_lpn = slba / secs_per_pg;
        uint64_t end_lpn = (slba + nlb - 1) / secs_per_pg;
        femu_debug("[znbc] zns.c::zns_nvme_rw : slba=%ld nlb=%d secs_per_pg=%ld start_lpn=%ld end_lpn=%ld \n", slba, nlb, secs_per_pg, start_lpn, end_lpn);
        for (uint64_t lpn = start_lpn, i = 0; lpn < end_lpn; lpn++, i++){
            struct slot_bz *slot = &n->zns->slots[lpn];
            slot->slot_size_bs = zone->pfwd[pfwd_id].slot_size_bs;
            slot->have_residue = (req->compressed_size[i] > zone->pfwd[pfwd_id].slot_size_bs) ? true :false;
            #ifdef IncPFWD_NoRes
            if(slot->have_residue == true){
                femu_debug("[znbc] zns.c::zns_nvme_rw : IncPFWD_NoRes method error! zone->pfwd[%u].slot_size_bs=%u req->compressed_size[%d]=%u\n", pfwd_id, zone->pfwd[pfwd_id].slot_size_bs, i, req->compressed_size[i]);
                assert(0);
            }
            #endif

            #ifdef RESIDUE_NUMBER_COUNT
            if(slot->have_residue == true) have_residue++;
            count_slot++;
            femu_debug("slot_id=lpn=%lu compressed_size[%lu]=%u pfwd_id=%u have_residue=%d cnt_have_residue=%d count_slot=%d\n", lpn, i, req->compressed_size[i], pfwd_id, slot->have_residue, have_residue, count_slot);
            #endif
            
            //femu_debug("(%lu,[%lu],%u,%u,%d), \n", lpn, i, req->compressed_size[i], pfwd_id, slot->have_residue);
        }
        femu_debug("\n");
        #endif

        #else
        //added by wpy
        //printf("qat compress test\n");
        //qat_init(n);
        int sg_cur_index = 0;
        dma_addr_t sg_cur_byte = 0;
        dma_addr_t cur_addr, cur_len;

        /*原始数据，用于压缩*/        
        uint64_t mb_oft_2 = (&data_offset)[0];
        void *mb_2 = n->mbe->logical_space;

        /*压缩后数据*/
        //void *mb = g_malloc(4096); //这个空间大小需要根据实际情况调整
        

        DMADirection dir = DMA_DIRECTION_TO_DEVICE;

        req->compressed_size = g_malloc(sizeof(uint32_t) * req->qsg.nsg);
        // CpaDcDpOpData **opData = n->dc_op_datas;
        //printf("req->qsg.nsg : %d\n",req->qsg.nsg);
        while (sg_cur_index < req->qsg.nsg){
            uint32_t outputlenqat = req->qsg.sg[sg_cur_index].len;

            //qat_dc_compress(n,0,mb_2+mb_oft_2,(req->qsg.sg[sg_cur_index].len - sg_cur_byte),mb, &outputlenqat,1);
            //printf("sg_cur_index : %d, outputlen : %d\n",sg_cur_index,outputlen);

            //req->qsg.sg[sg_cur_index].len = outputlenqat;
            //req->qsg.size = outputlenqat;

            (req->compressed_size)[sg_cur_index] = outputlenqat;

            cur_addr = req->qsg.sg[sg_cur_index].base + sg_cur_byte;
            cur_len = req->qsg.sg[sg_cur_index].len - sg_cur_byte;

            if (dma_memory_rw(req->qsg.as, cur_addr, mb_2, outputlenqat, dir, MEMTXATTRS_UNSPECIFIED)) {
                femu_err("dma_memory_rw error\n");
            }
            
            sg_cur_byte += cur_len;
            if (sg_cur_byte == req->qsg.sg[sg_cur_index].len) {
                sg_cur_byte = 0;
                ++sg_cur_index;
            }
            mb_oft_2 += req->qsg.sg[sg_cur_index].len; //为了这次测试方便我把大小写死了，但是需要将这个变为自适应的
            
            
        }
        //free(mb);
        //qat_exit(n);
        qemu_sglist_destroy(&req->qsg);
        #endif
    }
    else{
        //FILE *f = fopen("sysread.txt","a");
        //fprintf(f,"read\n");
        //fclose(f);
        /*Do nothing here for read request, because it will be done in zftl*/
        backend_rw(n->mbe, &req->qsg, &data_offset, req->is_write);
    }
    #else
    backend_rw(n->mbe, &req->qsg, &data_offset, req->is_write);
    #endif
       
        //backend_rw(n->mbe, &req->qsg, &data_offset, req->is_write);

        // printf("qat decompress test\n");
        // qat_init(n);
        // sg_cur_index = 0;
        // sg_cur_byte = 0;

        // // uint64_t mb_oft_decom = (&data_offset)[0];
        // // void *mb_decom = n->mbe->logical_space;

        // void *mb_buffer1 = g_malloc(4096);//解压前数据
        // void *mb_buffer2 = g_malloc(4096);//解压后数据

        // uint32_t inputlen = compressed_len;
        // outputlen = 0;
        // dir = DMA_DIRECTION_FROM_DEVICE;
        // while (sg_cur_index < (&req->qsg)->nsg){
        //     cur_addr = (&req->qsg)->sg[sg_cur_index].base + sg_cur_byte;

        //     if (dma_memory_rw((&req->qsg)->as, cur_addr, mb_buffer1, inputlen, dir, MEMTXATTRS_UNSPECIFIED)) {
        //         femu_err("dma_memory_rw error\n");
        //     }
        //     qat_dc_decompress(n,0,mb_buffer1, 4096,mb_buffer2,&outputlen,1);
        //     fprintf(stdout,"decompress %u bytes to %u bytes\n",inputlen,outputlen);

        //     sg_cur_byte += inputlen;
        //     if (sg_cur_byte >= (&req->qsg)->sg[sg_cur_index].len) {
        //         sg_cur_byte = 0;
        //         ++sg_cur_index;
        //     }
        //     // mb_oft_decom += outputlen;
        //     sg_cur_index++;//这里是为了测试，实际情况需要删除
        // }
        // qat_exit(n);
        // qemu_sglist_destroy(&req->qsg);
   
    


    if(req->is_write)
    {
        zns_finalize_zoned_write(ns, req, false);
    }

    n->zns->active_zone = zns_zone_idx(ns,slba);
    femu_debug("[znbc] zns.c::zns_nvme_rw active_zone=%u\n", n->zns->active_zone);
    return NVME_SUCCESS;
err:
    return status | NVME_DNR;
}

static uint16_t zns_zone_mgmt_send(FemuCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = (NvmeCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    uint64_t prp1 = le64_to_cpu(cmd->dptr.prp1);
    uint64_t prp2 = le64_to_cpu(cmd->dptr.prp2);
    NvmeZone *zone;
    uintptr_t *resets;
    uint8_t *zd_ext;
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint64_t slba = 0;
    uint32_t zone_idx = 0;
    uint16_t status;
    uint8_t action;
    bool all;
    enum NvmeZoneProcessingMask proc_mask = NVME_PROC_CURRENT_ZONE;

    action = dw13 & 0xff;
    all = dw13 & 0x100;

    req->status = NVME_SUCCESS;

    if (!all) {
        status = zns_get_mgmt_zone_slba_idx(n, cmd, &slba, &zone_idx);
        if (status) {
            return status;
        }
    }

    zone = &n->zone_array[zone_idx];
    if (slba != zone->d.zslba) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    femu_debug("[znbc] zns.c::zns_zone_mgmt_send action=%u all=%d slba=%lu zone_idx=%u\n", action, all, slba, zone_idx);
    switch (action) {
    case NVME_ZONE_ACTION_OPEN:
        if (all) {
            proc_mask = NVME_PROC_CLOSED_ZONES;
        }
        status = zns_do_zone_op(ns, zone, proc_mask, zns_open_zone, req);
        break;
    case NVME_ZONE_ACTION_CLOSE:
        if (all) {
            proc_mask = NVME_PROC_OPENED_ZONES;
        }
        status = zns_do_zone_op(ns, zone, proc_mask, zns_close_zone, req);
        break;
    case NVME_ZONE_ACTION_FINISH:
        if (all) {
            proc_mask = NVME_PROC_OPENED_ZONES | NVME_PROC_CLOSED_ZONES;
        }
        status = zns_do_zone_op(ns, zone, proc_mask, zns_finish_zone, req);
        break;
    case NVME_ZONE_ACTION_RESET:
        resets = (uintptr_t *)&req->opaque;

        if (all) {
            proc_mask = NVME_PROC_OPENED_ZONES | NVME_PROC_CLOSED_ZONES |
                NVME_PROC_FULL_ZONES;
            #ifdef BALLOON_ZNS
            zns_zone_additional_clear_bz(n);
            #endif
        }
        *resets = 1;
        status = zns_do_zone_op(ns, zone, proc_mask, zns_reset_zone, req);
        (*resets)--;
        return NVME_SUCCESS;
    case NVME_ZONE_ACTION_OFFLINE:
        if (all) {
            proc_mask = NVME_PROC_READ_ONLY_ZONES;
        }
        status = zns_do_zone_op(ns, zone, proc_mask, zns_offline_zone, req);
        break;
    case NVME_ZONE_ACTION_SET_ZD_EXT:
        if (all || !n->zd_extension_size) {
            return NVME_INVALID_FIELD | NVME_DNR;
        }
        zd_ext = zns_get_zd_extension(ns, zone_idx);
        status = dma_write_prp(n, (uint8_t *)zd_ext, n->zd_extension_size, prp1,
                               prp2);
        if (status) {
            return status;
        }
        status = zns_set_zd_ext(ns, zone);
        if (status == NVME_SUCCESS) {
            return status;
        }
        break;
    default:
        status = NVME_INVALID_FIELD;
    }

    if (status) {
        status |= NVME_DNR;
    }

    return status;
}

static bool zns_zone_matches_filter(uint32_t zafs, NvmeZone *zl)
{
    NvmeZoneState zs = zns_get_zone_state(zl);

    switch (zafs) {
    case NVME_ZONE_REPORT_ALL:
        return true;
    case NVME_ZONE_REPORT_EMPTY:
        return zs == NVME_ZONE_STATE_EMPTY;
    case NVME_ZONE_REPORT_IMPLICITLY_OPEN:
        return zs == NVME_ZONE_STATE_IMPLICITLY_OPEN;
    case NVME_ZONE_REPORT_EXPLICITLY_OPEN:
        return zs == NVME_ZONE_STATE_EXPLICITLY_OPEN;
    case NVME_ZONE_REPORT_CLOSED:
        return zs == NVME_ZONE_STATE_CLOSED;
    case NVME_ZONE_REPORT_FULL:
        return zs == NVME_ZONE_STATE_FULL;
    case NVME_ZONE_REPORT_READ_ONLY:
        return zs == NVME_ZONE_STATE_READ_ONLY;
    case NVME_ZONE_REPORT_OFFLINE:
        return zs == NVME_ZONE_STATE_OFFLINE;
    default:
        return false;
    }
}

static uint16_t zns_zone_mgmt_recv(FemuCtrl *n, NvmeRequest *req)
{
    NvmeCmd *cmd = (NvmeCmd *)&req->cmd;
    NvmeNamespace *ns = req->ns;
    uint64_t prp1 = le64_to_cpu(cmd->dptr.prp1);
    uint64_t prp2 = le64_to_cpu(cmd->dptr.prp2);
    /* cdw12 is zero-based number of dwords to return. Convert to bytes */
    uint32_t data_size = (le32_to_cpu(cmd->cdw12) + 1) << 2;
    uint32_t dw13 = le32_to_cpu(cmd->cdw13);
    uint32_t zone_idx, zra, zrasf, partial;
    uint64_t max_zones, nr_zones = 0;
    uint16_t status;
    uint64_t slba, capacity = zns_ns_nlbas(ns);
    NvmeZoneDescr *z;
    NvmeZone *zone;
    NvmeZoneReportHeader *header;
    void *buf, *buf_p;
    size_t zone_entry_sz;

    req->status = NVME_SUCCESS;

    status = zns_get_mgmt_zone_slba_idx(n, cmd, &slba, &zone_idx);
    if (status) {
        return status;
    }

    zra = dw13 & 0xff;
    if (zra != NVME_ZONE_REPORT && zra != NVME_ZONE_REPORT_EXTENDED) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }
    if (zra == NVME_ZONE_REPORT_EXTENDED && !n->zd_extension_size) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    zrasf = (dw13 >> 8) & 0xff;
    if (zrasf > NVME_ZONE_REPORT_OFFLINE) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    if (data_size < sizeof(NvmeZoneReportHeader)) {
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    status = nvme_check_mdts(n, data_size);
    if (status) {
        return status;
    }

    partial = (dw13 >> 16) & 0x01;

    zone_entry_sz = sizeof(NvmeZoneDescr);
    if (zra == NVME_ZONE_REPORT_EXTENDED) {
        zone_entry_sz += n->zd_extension_size;
    }

    max_zones = (data_size - sizeof(NvmeZoneReportHeader)) / zone_entry_sz;
    buf = g_malloc0(data_size);

    zone = &n->zone_array[zone_idx];
    for (; slba < capacity; slba += n->zone_size) {
        if (partial && nr_zones >= max_zones) {
            break;
        }
        if (zns_zone_matches_filter(zrasf, zone++)) {
            nr_zones++;
        }
    }
    header = (NvmeZoneReportHeader *)buf;
    header->nr_zones = cpu_to_le64(nr_zones);

    buf_p = buf + sizeof(NvmeZoneReportHeader);
    for (; zone_idx < n->num_zones && max_zones > 0; zone_idx++) {
        zone = &n->zone_array[zone_idx];
        if (zns_zone_matches_filter(zrasf, zone)) {
            z = (NvmeZoneDescr *)buf_p;
            buf_p += sizeof(NvmeZoneDescr);

            z->zt = zone->d.zt;
            z->zs = zone->d.zs;
            z->zcap = cpu_to_le64(zone->d.zcap);
            z->zslba = cpu_to_le64(zone->d.zslba);
            z->za = zone->d.za;

            if (zns_wp_is_valid(zone)) {
                z->wp = cpu_to_le64(zone->d.wp);
            } else {
                z->wp = cpu_to_le64(~0ULL);
            }

            if (zra == NVME_ZONE_REPORT_EXTENDED) {
                if (zone->d.za & NVME_ZA_ZD_EXT_VALID) {
                    memcpy(buf_p, zns_get_zd_extension(ns, zone_idx),
                           n->zd_extension_size);
                }
                buf_p += n->zd_extension_size;
            }

            max_zones--;
        }
    }

    status = dma_read_prp(n, (uint8_t *)buf, data_size, prp1, prp2);

    g_free(buf);

    return status;
}

static inline bool nvme_csi_has_nvm_support(NvmeNamespace *ns)
{
    switch (ns->ctrl->csi) {
    case NVME_CSI_NVM:
    case NVME_CSI_ZONED:
        return true;
    }

    return false;
}

static uint16_t zns_admin_cmd(FemuCtrl *n, NvmeCmd *cmd)
{
    switch (cmd->opcode) {
    default:
        return NVME_INVALID_OPCODE | NVME_DNR;
    }
}

static uint16_t zns_io_cmd(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd,
                           NvmeRequest *req)
{
    switch (cmd->opcode) {
    case NVME_CMD_READ:
    case NVME_CMD_WRITE:
        return zns_nvme_rw(n, ns, cmd, req,false);
    case NVME_CMD_ZONE_APPEND:
        return zns_nvme_rw(n, ns, cmd, req,true);
    case NVME_CMD_ZONE_MGMT_SEND:
        return zns_zone_mgmt_send(n, req);
    case NVME_CMD_ZONE_MGMT_RECV:
        return zns_zone_mgmt_recv(n, req);
    }

    return NVME_INVALID_OPCODE | NVME_DNR;
}

static void zns_set_ctrl_str(FemuCtrl *n)
{
    static int fsid_zns = 0;
    const char *zns_mn = "FEMU ZMS-SSD Controller [by Misao]";
    const char *zns_sn = "vZNSSD";

    nvme_set_ctrl_name(n, zns_mn, zns_sn, &fsid_zns);
}

static void zns_set_ctrl(FemuCtrl *n)
{
    uint8_t *pci_conf = n->parent_obj.config;

    zns_set_ctrl_str(n);
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, 0x5845);
}

// Add zns init ch, zns init flash and zns init block
// ----------------------------
static void zns_init_blk(struct zns_blk *blk,int num_blk,int blkidx,int flash_type)
{
    blk->nand_type = flash_type;
    blk->next_blk_avail_time = 0;
    blk->page_wp = 0;
}

static void zns_init_plane(struct zns_plane *plane,int num_blk,int flash_type)
{
    plane->blk = g_malloc0(sizeof(struct zns_blk) * num_blk);
    for (int i = 0; i < num_blk; i++) {
        zns_init_blk(&plane->blk[i],num_blk,i,flash_type);
    }
    plane->next_plane_avail_time = 0;
    #ifdef SHOW_EACH_PLANE_TOT_LAT
    plane->plane_r_lat = plane->plane_w_lat = plane->plane_tot_lat = 0;
    #endif
}

static void zns_init_fc(struct zns_fc *fc,uint8_t num_plane,uint8_t num_blk,int flash_type)
{
    fc->plane = g_malloc0(sizeof(struct zns_plane) * num_plane);
    for(int i = 0;i < num_plane;i++)
    {
        zns_init_plane(&fc->plane[i],num_blk,flash_type);
    }
    fc->next_fc_avail_time = 0;
}

static void zns_init_ch(struct zns_ch *ch, uint8_t num_lun,uint8_t num_plane, uint8_t num_blk,int flash_type)
{
    ch->fc = g_malloc0(sizeof(struct zns_fc) * num_lun);
    for (int i = 0; i < num_lun; i++) {
        zns_init_fc(&ch->fc[i],num_plane,num_blk,flash_type);
    }
    ch->next_ch_avail_time = 0;
}

static void zns_init_params(FemuCtrl *n)
{
    struct zns_ssd *id_zns;
    int i;

    id_zns = g_malloc0(sizeof(struct zns_ssd));
    id_zns->num_ch = n->zns_params.zns_num_ch;
    id_zns->num_lun = n->zns_params.zns_num_lun;
    id_zns->num_plane = n->zns_params.zns_num_plane;
    id_zns->num_blk = n->zns_params.zns_num_blk; // nums of blks per plane
    #ifdef NUM_PAGE_FIXED
    id_zns->num_page = 1024;
    #else
    id_zns->num_page = n->ns_size/ZNS_PAGE_SIZE/(id_zns->num_ch*id_zns->num_lun*id_zns->num_blk);
    #endif
    id_zns->lbasz = 1 << zns_ns_lbads(&n->namespaces[0]);
    id_zns->flash_type = n->zns_params.zns_flash_type;

    id_zns->ch = g_malloc0(sizeof(struct zns_ch) * id_zns->num_ch);
    for (i =0; i < id_zns->num_ch; i++) {
        zns_init_ch(&id_zns->ch[i], id_zns->num_lun,id_zns->num_plane,id_zns->num_blk,id_zns->flash_type);
    }

    femu_debug("[znbc] zns.c::zns_init_params : num_ch=%ld num_lun=%ld num_plane=%ld num_blk=%ld num_page=%ld, lbasz=%d flash_type=%d\n",\
    id_zns->num_ch, id_zns->num_lun, id_zns->num_plane, id_zns->num_blk, id_zns->num_page, id_zns->lbasz, id_zns->flash_type); // Can be seen at the top area of build-femu/log.
    
    #ifndef USE_SUBSUPERBLOCK
    id_zns->wp.ch = 0;
    id_zns->wp.lun = 0;
    #else
    //added by znbc: init zns for balloon-zns
    id_zns->wp_bz.ch = 0;
    id_zns->ssblk = g_malloc(sizeof(struct sub_superblock) * id_zns->num_blk * SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO);
    id_zns->num_ssblk = id_zns->num_blk * SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO;
    id_zns->ssblk_size_limit = id_zns->num_ch * id_zns->num_lun * id_zns->num_plane * id_zns->num_page \
    / id_zns->flash_type / SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO;
    for (i = 0; i < id_zns->num_ssblk; i++) {
        id_zns->ssblk[i].used = 0;
        id_zns->ssblk[i].to_zone = 0;
        id_zns->ssblk[i].is_ext = 0;
        id_zns->ssblk[i].lun = i % SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO;
        id_zns->ssblk[i].blk = i / SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO;
        id_zns->ssblk[i].write_pointer = 0;
    }
    #ifdef BALLOON_ZNS_RESIDUE
    id_zns->now_exssblk = id_zns->num_ssblk - 1;
    #endif
    
    #endif

    #ifdef USE_SLOT
    id_zns->profiling_window_size = id_zns->num_ch * id_zns->num_lun * id_zns->num_plane * id_zns->num_page \
    * ZNS_PAGE_SIZE / ZONE_SIZE_TO_PROFILING_WINDOW_SIZE_RATIO;
    #endif

    #ifdef TEST_COMP_EFFECT
    id_zns->tot_ppa_size = id_zns->tot_comp_size = 0;
    #endif
    
    //Misao: init mapping table
    id_zns->l2p_sz = n->ns_size/LOGICAL_PAGE_SIZE;
    id_zns->maptbl = g_malloc0(sizeof(struct ppa) * id_zns->l2p_sz);
    #ifdef USE_SLOT
    id_zns->slots = g_malloc0(sizeof(struct slot_bz) * id_zns->l2p_sz);
    #endif 
    femu_debug("[znbc] zns.c::zns_init_params : n->ns_size=%lu zns->l2p_sz=%lu\n", n->ns_size, id_zns->l2p_sz);
    for (i = 0; i < id_zns->l2p_sz; i++) {
        id_zns->maptbl[i].ppa = UNMAPPED_PPA;
    }

    //Misao: init sram
    #ifndef ZNS_C__ZNS_INIT_PARAMS__PROGRAM_UNIT_FIX
    id_zns->program_unit = ZNS_PAGE_SIZE*id_zns->flash_type*2; //PAGE_SIZE*flash_type*2 planes
    #else
    id_zns->program_unit = ZNS_PAGE_SIZE*id_zns->flash_type*id_zns->num_plane; // program unit should be 1 physical page size plus the num of plane for a lun
    #endif
    id_zns->stripe_uint = id_zns->program_unit*id_zns->num_ch*id_zns->num_lun; // stripe uint should be 1 physical page size plus the num of plane for a ssd
    id_zns->cache.num_wc = ZNS_DEFAULT_NUM_WRITE_CACHE;
    id_zns->cache.write_cache = g_malloc0(sizeof(struct zns_write_cache) * id_zns->cache.num_wc);
    for(i =0; i < id_zns->cache.num_wc; i++)
    {
        id_zns->cache.write_cache[i].sblk = i;
        id_zns->cache.write_cache[i].used = 0;
        id_zns->cache.write_cache[i].cap = (id_zns->stripe_uint/LOGICAL_PAGE_SIZE);
        #ifdef CR_ADAPT_WC
        id_zns->cache.write_cache[i].cap *= WRITE_CACHE_EXPANSION_RATIO;
        id_zns->cache.write_cache[i].cap_size = id_zns->stripe_uint;
        id_zns->cache.write_cache[i].used_size = 0;
        #endif
        id_zns->cache.write_cache[i].lpns = g_malloc0(sizeof(uint64_t) * id_zns->cache.write_cache[i].cap);
    }

    femu_log("===========================================\n");
    femu_log("|        ZMS HW Configuration()           |\n");      
    femu_log("===========================================\n");
    femu_log("|\tnchnl\t: %lu\t|\tchips per chnl\t: %lu\t|\tplanes per chip\t: %lu\t|\tblks per plane\t: %lu\t|\tpages per blk\t: %lu\t|\n",id_zns->num_ch,id_zns->num_lun,id_zns->num_plane,id_zns->num_blk,id_zns->num_page);
    //femu_log("|\tl2p sz\t: %lu\t|\tl2p cache sz\t: %u\t|\n",id_zns->l2p_sz,id_zns->cache.num_l2p_ent);
    femu_log("|\tprogram unit\t: %lu KiB\t|\tstripe unit\t: %lu KiB\t|\t# of write caches\t: %u\t|\t size of write caches (4KiB)\t: %lu\t|\n",id_zns->program_unit/(KiB),id_zns->stripe_uint/(KiB),id_zns->cache.num_wc,(id_zns->stripe_uint/LOGICAL_PAGE_SIZE));
    femu_log("===========================================\n"); 

    #ifdef COMPQAT
    femu_log("| using QAT (not origin femu): \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| using QAT (not origin femu): \e[1;31m NO \e[0m\n"); 
    #endif

    #ifdef BALLOON_ZNS
    femu_log("| using Balloon-ZNS: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| using Balloon-ZNS: \e[1;31m NO \e[0m\n"); 
    #endif

    #ifdef BALLOON_ZNS
    femu_log("===========================================\n"); 
    femu_log("| About Balloon-ZNS mode selection:\n"); 
    
    #ifdef USE_SUBSUPERBLOCK
    femu_log("| Use subsuperblock: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| Use subsuperblock: \e[1;31m NO \e[0m \n"); 
    #endif

    #ifdef USE_SLOT
    femu_log("| Use slot: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| Use slot: \e[1;31m NO \e[0m \n"); 
    #endif

    #ifdef IncPFWD_NoRes
    femu_log("| Use IncPFWD_NoRes method: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| Use IncPFWD_NoRes method: \e[1;31m NO \e[0m \n"); 
    #endif

    #ifdef BALLOON_ZNS_RESIDUE
    femu_log("| Handle residue: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| Handle residue: \e[1;31m NO \e[0m \n"); 
    #endif

    #ifdef SG_LEN_EQU_LOGICAL_PAGE_SIZE
    femu_log("| make the sg length equal to logical page size(not a good choice): \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| make the sg length equal to logical page size(not a good choice): \e[1;31m NO \e[0m\n"); 
    #endif

    #ifdef QAT_LATENCY
    femu_log("| Consider QAT_LATENCY: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| Consider QAT_LATENCY: \e[1;31m NO \e[0m\n"); 
    #endif

    #ifdef NUM_PAGE_FIXED
    femu_log("| Make the num of pages certain, so that we can increase SSD_SIZE_MB to increase num_zones(for rocksdb, which need 32 zones): \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| Make the num of pages certain, so that we can increase SSD_SIZE_MB to increase num_zones(for rocksdb, which need 32 zones): \e[1;31m NO \e[0m\n"); 
    #endif

    #ifdef DIFFERENT_SG_LEN
    femu_log("| can handle different sg length: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| can handle different sg length: \e[1;31m NO \e[0m\n"); 
    #endif

    #ifdef LINKED_SLOT
    femu_log("| use the linked slot design: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| use the linked slot design: \e[1;31m NO \e[0m\n"); 
    #endif

    #ifdef CH_BITS3
    femu_log("| in zns.h, change the CH_BITS to 3: \e[1;32m YES \e[0m \n"); 
    #else
    femu_log("| in zns.h, change the CH_BITS to 3: \e[1;31m NO \e[0m\n"); 
    #endif

    femu_log("===========================================\n"); 
    #endif

    //Misao: use average read latency
    id_zns->timing.pg_rd_lat[SLC] = SLC_READ_LATENCY_NS;
    id_zns->timing.pg_rd_lat[TLC] = TLC_READ_LATENCY_NS;
    id_zns->timing.pg_rd_lat[QLC] = QLC_READ_LATENCY_NS;

    //Misao: do not suppirt partial programing
    id_zns->timing.pg_wr_lat[SLC] = SLC_PROGRAM_LATENCY_NS;
    id_zns->timing.pg_wr_lat[TLC] = TLC_PROGRAM_LATENCY_NS;
    id_zns->timing.pg_wr_lat[QLC] = QLC_PROGRAM_LATENCY_NS;

    //Misao: copy from nand.h
    id_zns->timing.blk_er_lat[SLC] = SLC_BLOCK_ERASE_LATENCY_NS;
    id_zns->timing.blk_er_lat[TLC] = TLC_BLOCK_ERASE_LATENCY_NS;
    id_zns->timing.blk_er_lat[QLC] = QLC_BLOCK_ERASE_LATENCY_NS;

    id_zns->dataplane_started_ptr = &n->dataplane_started;

    n->zns = id_zns;

    //Misao: init ftl
    zftl_init(n);
}

static int zns_init_zone_cap(FemuCtrl *n)
{
    assert(n->zns);
    struct zns_ssd* zns  = n->zns;
    n->zoned = true;
    n->zasl_bs = NVME_DEFAULT_MAX_AZ_SIZE;
    n->zone_size_bs = zns->num_ch*zns->num_lun*zns->num_plane*zns->num_page*ZNS_PAGE_SIZE;
    n->zone_cap_bs = 0;
    n->cross_zone_read = false;
    n->max_active_zones = 0;
    n->max_open_zones = 0;
    #ifdef MAX_ACTIVE_ZONES
    n->max_active_zones = MAX_ACTIVE_ZONES;
    #endif
    #ifdef MAX_OPEN_ZONES
    n->max_open_zones = MAX_OPEN_ZONES;
    #endif
    n->zd_extension_size = 0;

    return 0;
}

static int zns_start_ctrl(FemuCtrl *n)
{
    /* Coperd: let's fail early before anything crazy happens */
    assert(n->page_size == 4096);

    if (!n->zasl_bs) {
        n->zasl = n->mdts;
    } else {
        if (n->zasl_bs < n->page_size) {
            femu_err("ZASL too small (%dB), must >= 1 page (4K)\n", n->zasl_bs);
            return -1;
        }
        n->zasl = 31 - clz32(n->zasl_bs / n->page_size);
    }

    return 0;
}

static void zns_init(FemuCtrl *n, Error **errp)
{
    NvmeNamespace *ns = &n->namespaces[0];

    #ifdef COMPQAT
    qat_init(n);
    #endif
    

    zns_set_ctrl(n);
    zns_init_params(n);

    zns_init_zone_cap(n);

    if (zns_init_zone_geometry(ns, errp) != 0) {
        return;
    }

    zns_init_zone_identify(n, ns, 0);
}

static void zns_exit(FemuCtrl *n)
{
    /*
     * Release any extra resource (zones) allocated for ZNS mode
     */
    #ifdef COMPRQAT
    qat_exit(n);
    #endif
}

int nvme_register_znssd(FemuCtrl *n)
{
    n->ext_ops = (FemuExtCtrlOps) {
        .state            = NULL,
        .init             = zns_init,
        .exit             = zns_exit,
        .rw_check_req     = NULL,
        .start_ctrl       = zns_start_ctrl,
        .admin_cmd        = zns_admin_cmd,
        .io_cmd           = zns_io_cmd,
        .get_log          = NULL,
    };

    return 0;
}
