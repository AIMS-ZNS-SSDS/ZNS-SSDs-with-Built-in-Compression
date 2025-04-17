/* 
    some options for balloon-zns
    if want to use origin femu, need to disable COMPQAT and BALLOON_ZNS
    to test rocksdb, need to enable NUM_PAGE_256 and change SSD_SIZE_MB  in run-zns.sh to increase num_zones to 32
*/

/* ============= Core Options =============*/
#define COMPQAT // just enable qat and do compress, should enable BALLOON_ZNS to have real effects
#define BALLOON_ZNS // need to enable COMPQAT and NO_FUNC first
#define IncPFWD_NoRes
#ifdef BALLOON_ZNS
    #define USE_SLOT
    #define USE_SUBSUPERBLOCK // an idea in Balloon-ZNS
#endif

#ifdef USE_SLOT
    #define BALLOON_ZNS_RESIDUE // this should always be open after relevant code is completed
#endif

/* ======================================== */

/* ========== Important Options ===========*/
#ifdef COMPQAT
    #define NO_FUNC
    #define DIFFERENT_SG_LEN // eg. rocksdb test. 
    #define QAT_LATENCY
    #ifdef QAT_LATENCY
        #define QAT_COMPRESSION_LATENCY_NS 25000
        #define QAT_DECOMPRESSION_LATENCY_NS 15000
    #endif
#endif

#ifdef BALLOON_ZNS
    #define SG_LEN_EQU_LOGICAL_PAGE_SIZE // Assuming that the length of sg is the logical page length
    //#define BIGGER_SLOT_SIZE // Add a SLOT_SIZE_BASE, otherwise the residue number will be large. See zns.c::zns_nvme_rw.
    //#define LINKED_SLOT // unrealized
#endif

#ifdef USE_SUBSUPERBLOCK
    /* added by znbc, here we specify that the size of a sub-superblock is 1/4 of the size of a superblock because num_lun is 4*/
    #define SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO 4
#endif

#ifdef USE_SLOT
    #define ZONE_SIZE_TO_PROFILING_WINDOW_SIZE_RATIO 8
    #define CR_VALUE_PERCENTILE 70
    #define INITIAL_SLOT_SIZE_TO_PAGE_SIZE_PERCENTILE 85

    #define SLOT_SIZE_BASE 256
    #define WRITE_CACHE_EXPANSION_RATIO 5
#endif

#define NUM_PAGE_256 // for rocksdb !!! To make the num of pages per block certain(256), so that we can increase SSD_SIZE_MB to increase num_zones. Also need to change run-zns.sh

//#define MAX_ACTIVE_ZONES 16
//#define MAX_OPEN_ZONES 16

/* ======================================== */

/* ============= Test Options =============*/
#define FEMU_DEBUG_NVME
// #define RESIDUE_NUMBER_COUNT 
#define SHOW_FLUSH_MAXLAT
//#define ADD_LARGE_LAT_FOR_TEST // Add additional large delay in `zftl.c::zns_write` and observe changes in test results
#define SHOW_EACH_PLANE_TOT_LAT
/* ======================================== */

/* ============= Fix Options =============*/
//#define CH_BITS3 // in zns.h, change the CH_BITS to 3
// Sometimes I(znbc) think there is a programming error but not sure so I use a macro
#define ZNS_C__ZNS_INIT_PARAMS__PROGRAM_UNIT_FIX 

/* ======================================== */
