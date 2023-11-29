#ifndef SPDK_RAID_SERVICE_INTERNAL_H
#define SPDK_RAID_SERVICE_INTERNAL_H

#include "spdk/queue.h"

#define __base_desc_from_raid_bdev(raid_bdev, idx) (raid_bdev->base_bdev_info[idx].desc)
#define IS_AREA_STR_CLEAR(area_srt) (area_srt)
#define CREATE_AREA_STR_SNAPSHOT(area_srt) (area_srt)
#define fl(rebuild) &(rebuild->rebuild_flag)
#define LEN_AREA_STR_IN_BIT 64

// ->
#define _BASE_TYPE uint64_t
#define _BIT_PROECTION(name) _BASE_TYPE name[MATRIX_REBUILD_SIZE / (sizeof(_BASE_TYPE)*8)]
#define _GET_IDX(x) (x / (sizeof(_BASE_TYPE)*8))
#define _GET_SHFT(x) (x % (sizeof(_BASE_TYPE)*8))
// <-

struct rebuild_progress {
    /* 
     * bit proection of rebuild matrix, 
     * where each bit corresponds one line(area stripe) in rebuild matrix 
     * (if the line contains broken areas, corresponding bit equels 1 othewise 0) 
     */
    _BIT_PROECTION(area_proection);

    /* true if one part of the rebuild cycle is completed */
    bool partly_completed;

    /* number of areas stripes with broken areas */
    uint64_t area_str_cnt; 

    /* number of area stripes without broken areas (which were processed) */
    uint64_t clear_area_str_cnt; //TODO: должно быть атомиком?, может инкрементить параллельно (мб это не так и страшно)
};

void
submit_write_request_base_bdev(struct raid_bdev *raid_bdev, uint8_t idx);

int 
run_rebuild_poller(void* arg);

#endif /* SPDK_RAID_SERVICE_INTERNAL_H */