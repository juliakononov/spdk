/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2022 Intel Corporation.
 *   All rights reserved.
 */

#include "bdev_raid.h"

#include "spdk/likely.h"
#include "spdk/log.h"

#define BIT_CHECK(number, shift) (number & (1UL << shift))
#define BIT_INSERT(number, shift) (number |= 1UL << shift)
#define KB_TO_BYTES(number) (number << 10)

struct raid1_info {
	/* The parent raid bdev */
	struct raid_bdev *raid_bdev;
};

/* Find the bdev index of the current IO request */ 
static uint32_t 
get_current_bdev_io_index(struct spdk_bdev_io *bdev_io, struct raid_bdev_io *raid_io){
	//TODO: правильно ли я выбираю num_base_bdevs или надо использовать num_base_bdevs_discovered
	for(uint8_t i = 0; i < raid_io->raid_bdev->num_base_bdevs; i++) {
		if(raid_io->raid_bdev->base_bdev_info[i].name == bdev_io->bdev->name) {
			return i;
		}
	}
	return -ENODEV;
}

/* Write a broken block to the rebuild_matrix */
static void 
write_in_rbm_broken_block(struct spdk_bdev_io *bdev_io, struct raid_bdev_io *raid_io) 
{
	/* blocks */
	uint64_t blockcnt = raid_io->raid_bdev->bdev.blockcnt;
	uint32_t blocklen = raid_io->raid_bdev->bdev.blocklen;

	uint64_t offset_blocks = bdev_io->u.bdev.offset_blocks;
	uint64_t num_blocks = bdev_io->u.bdev.num_blocks;

	/* blocks -> strips */
	uint32_t strip_size_b = KB_TO_BYTES(raid_io->raid_bdev->strip_size_kb);
	uint64_t stripcnt = (blockcnt * blocklen) / strip_size_b;

	uint64_t offset_strips = (offset_blocks * blocklen) / strip_size_b;
	uint64_t num_strips = ((offset_blocks + num_blocks + strip_size_b - 1) / strip_size_b) - offset_strips;
	
	/* strips -> areas*/
	uint64_t num_memory_areas = raid_io->raid_bdev->raid_rebuild.num_memory_areas;
  	uint64_t stripcnt_in_memory_area = stripcnt / num_memory_areas;

	uint64_t offset_areas = offset_strips / stripcnt_in_memory_area;
	uint64_t num_areas = ((offset_strips + num_strips + stripcnt_in_memory_area -1) / stripcnt_in_memory_area) - offset_areas;


  	uint32_t bdev_io_index = get_current_bdev_io_index(bdev_io, raid_io);
	for (int cur_memory_area = offset_areas, cur_memory_area < offset_areas + num_areas, cur_memory_area++) {
		uint64_t broken_memory_area = raid_io->raid_bdev->raid_rebuild.rebuild_matrix[cur_memory_area];
		if (!BIT_CHECK(broken_memory_area, bdev_io_index)) {
			BIT_INSERT(broken_memory_area, bdev_io_index);
		}
  	}
}	

static void
raid1_bdev_io_completion(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
	struct raid_bdev_io *raid_io = cb_arg;

	spdk_bdev_free_io(bdev_io);

	if(!success) {
		write_in_rbm_broken_block(bdev_io, raid_io)
	}

	raid_bdev_io_complete_part(raid_io, 1, success ?
				   SPDK_BDEV_IO_STATUS_SUCCESS :
				   SPDK_BDEV_IO_STATUS_FAILED);
}

static void raid1_submit_rw_request(struct raid_bdev_io *raid_io);

static void
_raid1_submit_rw_request(void *_raid_io)
{
	struct raid_bdev_io *raid_io = _raid_io;

	raid1_submit_rw_request(raid_io);
}

static void
raid1_init_ext_io_opts(struct spdk_bdev_io *bdev_io, struct spdk_bdev_ext_io_opts *opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->size = sizeof(*opts);
	opts->memory_domain = bdev_io->u.bdev.memory_domain;
	opts->memory_domain_ctx = bdev_io->u.bdev.memory_domain_ctx;
	opts->metadata = bdev_io->u.bdev.md_buf;
}

static int
raid1_submit_read_request(struct raid_bdev_io *raid_io)
{
	struct raid_bdev *raid_bdev = raid_io->raid_bdev;
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx(raid_io);
	struct spdk_bdev_ext_io_opts io_opts;
	uint8_t idx = 0;
	struct raid_base_bdev_info *base_info;
	struct spdk_io_channel *base_ch = NULL;
	uint64_t pd_lba, pd_blocks;
	int ret;

	RAID_FOR_EACH_BASE_BDEV(raid_bdev, base_info) {
		base_ch = raid_io->raid_ch->base_channel[idx];
		if (base_ch != NULL) {
			break;
		}
		idx++;
	}

	if (base_ch == NULL) {
		raid_bdev_io_complete(raid_io, SPDK_BDEV_IO_STATUS_FAILED);
		return 0;
	}

	pd_lba = bdev_io->u.bdev.offset_blocks;
	pd_blocks = bdev_io->u.bdev.num_blocks;

	raid_io->base_bdev_io_remaining = 1;

	raid1_init_ext_io_opts(bdev_io, &io_opts);
	ret = spdk_bdev_readv_blocks_ext(base_info->desc, base_ch,
					 bdev_io->u.bdev.iovs, bdev_io->u.bdev.iovcnt,
					 pd_lba, pd_blocks, raid1_bdev_io_completion,
					 raid_io, &io_opts);

	if (spdk_likely(ret == 0)) {
		raid_io->base_bdev_io_submitted++;
	} else if (spdk_unlikely(ret == -ENOMEM)) {
		raid_bdev_queue_io_wait(raid_io, spdk_bdev_desc_get_bdev(base_info->desc),
					base_ch, _raid1_submit_rw_request);
		return 0;
	}

	return ret;
}

static int
raid1_submit_write_request(struct raid_bdev_io *raid_io)
{
	struct raid_bdev *raid_bdev = raid_io->raid_bdev;
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx(raid_io);
	struct spdk_bdev_ext_io_opts io_opts;
	struct raid_base_bdev_info *base_info;
	struct spdk_io_channel *base_ch;
	uint64_t pd_lba, pd_blocks;
	uint8_t idx;
	uint64_t base_bdev_io_not_submitted;
	int ret = 0;

	pd_lba = bdev_io->u.bdev.offset_blocks;
	pd_blocks = bdev_io->u.bdev.num_blocks;

	if (raid_io->base_bdev_io_submitted == 0) {
		raid_io->base_bdev_io_remaining = raid_bdev->num_base_bdevs;
	}

	raid1_init_ext_io_opts(bdev_io, &io_opts);
	for (idx = raid_io->base_bdev_io_submitted; idx < raid_bdev->num_base_bdevs; idx++) {
		base_info = &raid_bdev->base_bdev_info[idx];
		base_ch = raid_io->raid_ch->base_channel[idx];

		if (base_ch == NULL) {
			// TODO: тестить rebuild_matrix
			/* skip a missing base bdev's slot */
			raid_io->base_bdev_io_submitted++;
			raid_bdev_io_complete_part(raid_io, 1, SPDK_BDEV_IO_STATUS_SUCCESS);
			continue;
		}

		ret = spdk_bdev_writev_blocks_ext(base_info->desc, base_ch,
						  bdev_io->u.bdev.iovs, bdev_io->u.bdev.iovcnt,
						  pd_lba, pd_blocks, raid1_bdev_io_completion,
						  raid_io, &io_opts);
		if (spdk_unlikely(ret != 0)) {
			if (spdk_unlikely(ret == -ENOMEM)) {
				raid_bdev_queue_io_wait(raid_io, spdk_bdev_desc_get_bdev(base_info->desc),
							base_ch, _raid1_submit_rw_request);
				return 0;
			}

			base_bdev_io_not_submitted = raid_bdev->num_base_bdevs -
						     raid_io->base_bdev_io_submitted;
			raid_bdev_io_complete_part(raid_io, base_bdev_io_not_submitted,
						   SPDK_BDEV_IO_STATUS_FAILED);
			return 0;
		}

		raid_io->base_bdev_io_submitted++;
	}

	if (raid_io->base_bdev_io_submitted == 0) {
		ret = -ENODEV;
	}

	return ret;
}

static void
raid1_submit_rw_request(struct raid_bdev_io *raid_io)
{
	struct spdk_bdev_io *bdev_io = spdk_bdev_io_from_ctx(raid_io);
	int ret;

	switch (bdev_io->type) {
	case SPDK_BDEV_IO_TYPE_READ:
		ret = raid1_submit_read_request(raid_io);
		break;
	case SPDK_BDEV_IO_TYPE_WRITE:
		ret = raid1_submit_write_request(raid_io);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (spdk_unlikely(ret != 0)) {
		raid_bdev_io_complete(raid_io, SPDK_BDEV_IO_STATUS_FAILED);
	}
}

static int
raid1_start(struct raid_bdev *raid_bdev)
{
	uint64_t min_blockcnt = UINT64_MAX;
	struct raid_base_bdev_info *base_info;
	struct raid1_info *r1info;

	r1info = calloc(1, sizeof(*r1info));
	if (!r1info) {
		SPDK_ERRLOG("Failed to allocate RAID1 info device structure\n");
		return -ENOMEM;
	}
	r1info->raid_bdev = raid_bdev;

	RAID_FOR_EACH_BASE_BDEV(raid_bdev, base_info) {
		min_blockcnt = spdk_min(min_blockcnt, spdk_bdev_desc_get_bdev(base_info->desc)->blockcnt);
	}

	raid_bdev->bdev.blockcnt = min_blockcnt;
	raid_bdev->module_private = r1info;

	return 0;
}

static bool
raid1_stop(struct raid_bdev *raid_bdev)
{
	struct raid1_info *r1info = raid_bdev->module_private;

	free(r1info);

	return true;
}

static struct raid_bdev_module g_raid1_module = {
	.level = RAID1,
	.base_bdevs_min = 2,
	.base_bdevs_constraint = {CONSTRAINT_MIN_BASE_BDEVS_OPERATIONAL, 1},
	.memory_domains_supported = true,
	.start = raid1_start,
	.stop = raid1_stop,
	.submit_rw_request = raid1_submit_rw_request,
};
RAID_MODULE_REGISTER(&g_raid1_module)

SPDK_LOG_REGISTER_COMPONENT(bdev_raid1)
