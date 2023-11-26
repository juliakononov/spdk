#ifndef SPDK_RAID_SERVICE_INTERNAL_H
#define SPDK_RAID_SERVICE_INTERNAL_H

#define __base_desc_from_raid_bdev(raid_bdev, idx) (raid_bdev->base_bdev_info[idx].desc)

void
submit_write_request_base_bdev(struct raid_bdev *raid_bdev, uint8_t idx);

int 
run_rebuild_poller(void* arg);

#endif /* SPDK_RAID_SERVICE_INTERNAL_H */