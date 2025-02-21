/* 
    some options for balloon-zns
    if want to use origin femu, need to disable COMPQAT and BALLOON_ZNS
    to test rocksdb, need to enable NUM_PAGE_256 and change SSD_SIZE_MB  in run-zns.sh to increase num_zones to 32
*/

#define FEMU_DEBUG_NVME // just for testing
// #define RESIDUE_NUMBER_COUNT 

#define COMPQAT // just enable qat and do compress, should enable BALLOON_ZNS to have real effects

#ifdef COMPQAT
    #define NO_FUNC
    #define DIFFERENT_SG_LEN // eg. rocksdb test. 
#endif

#define BALLOON_ZNS // need to enable COMPQAT and NO_FUNC first

#define SHOW_FLUSH_MAXLAT

#ifdef BALLOON_ZNS
    //#define BALLOON_ZNS_RESIDUE // this should always be open after relevant code is completed
    #define SG_LEN_EQU_LOGICAL_PAGE_SIZE // Assuming that the length of sg is the logical page length
    //#define BIGGER_SLOT_SIZE // Add a SLOT_SIZE_BASE, otherwise the residue number will be large. See zns.c::zns_nvme_rw.
    //#define LINKED_SLOT // not enable
#endif

#ifdef BALLOON_ZNS
/* added by znbc, here we specify that the size of a sub-superblock is 1/4 of the size of a superblock because num_lun is 4*/
#define SUPERBLOCK_TO_SUBSUPERBLOCK_RATIO 4

/* added by znbc*/
#define ZONE_SIZE_TO_PROFILING_WINDOW_SIZE_RATIO 8
#define CR_VALUE_PERCENTILE 70
#define INITIAL_SLOT_SIZE_TO_PAGE_SIZE_PERCENTILE 50

#define SLOT_SIZE_BASE 256
#endif

#define NUM_PAGE_256 // for rocksdb !!! To make the num of pages certain(256), so that we can increase SSD_SIZE_MB to increase num_zones. Also need to change run-zns.sh


//#define MAX_ACTIVE_ZONES 16
//#define MAX_OPEN_ZONES 16
