#define COMPQAT
#define NO_FUNC

#define FEMU_DEBUG_NVME

#define BALLOON_ZNS // need to enable COMPQAT and NO_FUNC first

#define SHOW_FLUSH_MAXLAT

#ifdef BALLOON_ZNS
    #define BALLOON_ZNS_RESIDUE
#endif

#define MAX_ACTIVE_ZONES 16
#define MAX_OPEN_ZONES 16