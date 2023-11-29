#ifndef SPDK_RAID_SERVICE_INTERNAL_H
#define SPDK_RAID_SERVICE_INTERNAL_H

#include "spdk/queue.h"

#define __base_desc_from_raid_bdev(raid_bdev, idx) (raid_bdev->base_bdev_info[idx].desc)
#define IS_AREA_STR_CLEAR(area_srt) (area_srt)
#define CREATE_AREA_STR_SNAPSHOT(area_srt) (area_srt)
#define LEN_AREA_STR_IN_BIT 64

struct rebuild_elem {
    SLIST_ENTRY(rebuild_elem) list_entry;
    
    /* number of areas that need rebuilding in the area_stripe */
    uint16_t areacnt;

    /* snapshot of coresponding rebuild_matrix element */
    uint64_t area_stripe; // TODO: не переписывать на атомики, просто копировать информацию из атомика64 сюда
};

struct rebuild_progress {
    /* list of area stripes containing broken areas */
    SLIST_HEAD(sl, rebuild_elem) list;

    /* number of area stripes in list */
    uint64_t area_str_cnt;

    /* number of area stripes without broken areas (which were processed) */
    uint64_t clear_area_str_cnt; //TODO: должно быть атомиком?, может инкрементиться параллельно (мб это не так и страшно)
};

void
submit_write_request_base_bdev(struct raid_bdev *raid_bdev, uint8_t idx);

int 
run_rebuild_poller(void* arg);

#endif /* SPDK_RAID_SERVICE_INTERNAL_H */