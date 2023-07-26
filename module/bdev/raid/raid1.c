/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2022 Intel Corporation.
 *   All rights reserved.
 */

#define CHECK_REBUILD_MATRIX_INFO_ON_STOP
#define CHECK_REBUILD_MATRIX_INFO_ON_START
#define CHECK_REBUILD_MATRIX_INFO_ON_get_io_area_range
#define TEST_BASE_BDEV_INDEX
#define TEST_WRITE_MATRIX
#define CHECK_MATH
#define CHECK_READ_ERROR
#define FIND_get_io_area_range
#define BASE_BDEV_NAME_in_COMPLITION
#define default_read_test 

#include "bdev_raid.h"

#include "spdk/likely.h"
#include "spdk/log.h"
#include "spdk/util.h"


struct raid1_info {
	/* The parent raid bdev */
	struct raid_bdev *raid_bdev;
};

/* Find the bdev index of the current IO request */ 
static uint32_t 
get_current_bdev_idx(struct spdk_bdev_io *bdev_io, struct raid_bdev_io *raid_io, uint32_t *bdev_idx)
{

#ifdef TEST_BASE_BDEV_INDEX
//TODO: правильно ли я выбираю num_base_bdevs или надо использовать num_base_bdevs_discovered
	SPDK_NOTICELOG("\n<-| TEST_BASE_BDEV_INDEX!!! |->\n");
	SPDK_NOTICELOG("raid_bdev_name = %s\n", raid_io->raid_bdev->bdev.name);
	SPDK_NOTICELOG("spdk_bdev_name = %s\n", bdev_io->bdev->name);
#endif

	for(uint8_t i = 0; i < raid_io->raid_bdev->num_base_bdevs; i++) {

#ifdef TEST_BASE_BDEV_INDEX
	SPDK_NOTICELOG("raid_io->raid_bdev->base_bdev_info[i].name = %s\n", raid_io->raid_bdev->base_bdev_info[i].name);
	SPDK_NOTICELOG("bdev_io->bdev->name = %s\n", bdev_io->bdev->name);
	SPDK_NOTICELOG("<--------------------------->\n");
#endif

		if(raid_io->raid_bdev->base_bdev_info[i].name == bdev_io->bdev->name) {
			*bdev_idx = i;
			return 0;
		}
	}
	return -ENODEV;
}

/* Allows to define the memory_rebuild_areas that are involved in current io request */
static void
get_io_area_range(struct spdk_bdev_io *bdev_io, struct raid_bdev *raid_bdev, uint64_t *i, uint64_t *n)
{
	/* TODO: 
	 * Сделать проверку + инициализацию полей; raid_bdev->rebuild (все)
	 */

#ifdef CHECK_REBUILD_MATRIX_INFO_ON_get_io_area_range
	SPDK_NOTICELOG("\n<-| RAID_BDEV+REBUILD in get_io_area_range!!! |->\n");
	SPDK_NOTICELOG("strip_size = %lu\n", raid_bdev->strip_size);
	SPDK_NOTICELOG("num_base_bdevs = %lu\n", raid_bdev->num_base_bdevs);
	SPDK_NOTICELOG("num_base_bdevs_discovered = %lu\n", raid_bdev->num_base_bdevs_discovered);
	SPDK_NOTICELOG("blockcnt = %lu\n", raid_bdev->bdev.blockcnt);
	SPDK_NOTICELOG("blocklen = %lu\n", raid_bdev->bdev.blocklen);
	SPDK_NOTICELOG("strips_per_area = %lu\n", raid_bdev->rebuild.strips_per_area);
	SPDK_NOTICELOG("rebuild_flag = %lu\n", raid_bdev->rebuild.rebuild_flag);
	SPDK_NOTICELOG("num_memory_areas = %lu\n", raid_bdev->rebuild.num_memory_areas);
	SPDK_NOTICELOG("<--------------------------->\n");
#endif

	/* blocks */
	uint64_t offset_blocks = bdev_io->u.bdev.offset_blocks;
	uint64_t num_blocks = bdev_io->u.bdev.num_blocks;

	/* blocks -> strips */
	uint64_t offset_strips = (offset_blocks) / raid_bdev->strip_size;
	uint64_t num_strips = SPDK_CEIL_DIV(offset_blocks + num_blocks, raid_bdev->strip_size) - offset_strips;
	
	/* strips -> areas*/
	uint64_t strips_per_area = raid_bdev->rebuild.strips_per_area;

	uint64_t offset_areas = offset_strips / strips_per_area;
	uint64_t num_areas = SPDK_CEIL_DIV(offset_strips + num_strips, strips_per_area) - offset_areas;

#ifdef CHECK_MATH
	SPDK_NOTICELOG("\n<-| CHECK_MATH!!! |->\n");
	SPDK_NOTICELOG("offset_blocks = %lu\n", offset_blocks);
	SPDK_NOTICELOG("num_blocks = %lu\n", num_blocks);
	SPDK_NOTICELOG("offset_strips = %lu\n", offset_strips);
	SPDK_NOTICELOG("num_strips = %lu\n", num_strips);
	SPDK_NOTICELOG("strips_per_area = %lu\n", strips_per_area);
	SPDK_NOTICELOG("offset_areas = %lu\n", offset_areas);
	SPDK_NOTICELOG("num_areas = %lu\n", num_areas);
	SPDK_NOTICELOG("<--------------------------->\n");
#endif

	*i = offset_areas;
	*n = num_areas;
}

/* Write a broken block to the rebuild_matrix */
static void 
write_in_rbm_broken_block(struct spdk_bdev_io *bdev_io, struct raid_bdev_io *raid_io) 
{
	/* TODO:
	 * протестировать.
	 */
	uint64_t offset_areas = 0;
	uint64_t num_areas = 0;
	uint32_t bdev_idx =  0;

#ifdef FIND_get_io_area_range
	SPDK_NOTICELOG("\n|--|get_io_area_range in write_in_rbm_broken_block!!! |--|\n");
#endif

	get_io_area_range(bdev_io, raid_io->raid_bdev, &offset_areas, &num_areas);

	/* возможно, spdk_bdev_io на этом уравне - это spdk_bdev_io-> raid_bdev */
  	get_current_bdev_idx(bdev_io, raid_io, &bdev_idx);

#ifdef TEST_WRITE_MATRIX
	SPDK_NOTICELOG("\n<-| TEST_WRITE_MATRIX!!! |->\n");
	SPDK_NOTICELOG("current io base bdev idx = %lu", bdev_idx);
#endif

	for (uint64_t i = offset_areas; i < offset_areas + num_areas; i++) {
		uint64_t *area = &raid_io->raid_bdev->rebuild.rebuild_matrix[i];
		// if (!CHECK_BIT(*area, bdev_idx)) {
			INSERT_BIT(*area, bdev_idx);

#ifdef TEST_WRITE_MATRIX
	SPDK_NOTICELOG("%lu) current_area = %lu\n", i, *area);
#endif
		// }
  	}
#ifdef TEST_WRITE_MATRIX
	SPDK_NOTICELOG("<--------------------------->\n");
#endif
}	

/* Determine if a device needs a rebuild or not */
static int 
get_bdev_rebuild_status(struct raid_bdev *raid_bdev, struct spdk_bdev_io *bdev_io, uint8_t bdev_idx)
{
	uint64_t offset_areas = 0;
	uint64_t num_areas = 0;

#ifdef FIND_get_io_area_range
	SPDK_NOTICELOG("\n|--|get_io_area_range in get_bdev_rebuild_status!!! |--|\n");
#endif

	get_io_area_range(bdev_io, raid_bdev, &offset_areas, &num_areas);

	for (uint64_t i = offset_areas; i < offset_areas + num_areas; i++) {
		uint64_t area = raid_bdev->rebuild.rebuild_matrix[i];
		if (CHECK_BIT(area, bdev_idx)) {
			return NEED_REBUILD;
		}
  	}
	return NOT_NEED_REBUILD;
}

static void
raid1_bdev_io_completion(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
	struct raid_bdev_io *raid_io = cb_arg;

	spdk_bdev_free_io(bdev_io);

#ifdef BASE_BDEV_NAME_in_COMPLITION
	SPDK_NOTICELOG("\n<- spdk_bdev_io in COMPLITION = %s | status = %d->\n", bdev_io->bdev->name, success);
#endif

	if(!success) {
		write_in_rbm_broken_block(bdev_io, raid_io);

	}
//TODO: надо ли менять флаг SPDK_BDEV_IO_STATUS_FAILED на SPDK_BDEV_IO_STATUS_SUCCESS после прохода по облостям дисков
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


#ifdef CHECK_READ_ERROR
	SPDK_NOTICELOG("\n<-| CHECK_READ_ERROR!!! |->\n");
	SPDK_NOTICELOG("raid_bdev_name = %s\n", raid_bdev->bdev.name);
	SPDK_NOTICELOG("spdk_bdev_name = %s\n", bdev_io->bdev->name);
#endif

	RAID_FOR_EACH_BASE_BDEV(raid_bdev, base_info) {
		base_ch = raid_io->raid_ch->base_channel[idx];
		if (base_ch != NULL) {

#ifdef CHECK_READ_ERROR
	SPDK_NOTICELOG("\n<-| CURRENT BDEV %d |->\n", idx);
#endif
			if (get_bdev_rebuild_status(raid_bdev, bdev_io, idx) == NOT_NEED_REBUILD) {
				break;
			}
			base_ch = NULL;
			/* TODO: Do I need to free the base_ch with the broken-area?*/
		}
		idx++;
	}

#ifdef CHECK_READ_ERROR
	SPDK_NOTICELOG("<--------------------------->\n");
#endif

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

static void
init_rebuild(struct raid_bdev *raid_bdev)
{	

#ifdef default_read_test 
	raid_bdev->rebuild.rebuild_matrix[0] = 1;
#endif

	raid_bdev->rebuild.num_memory_areas = MATRIX_REBUILD_AREAS_IN_USE;
	uint64_t stripcnt = SPDK_CEIL_DIV(raid_bdev->bdev.blockcnt, raid_bdev->strip_size);
	raid_bdev->rebuild.strips_per_area = SPDK_CEIL_DIV(stripcnt, MATRIX_REBUILD_AREAS_IN_USE);
	raid_bdev->rebuild.rebuild_flag = NOT_NEED_REBUILD;
}

static int
raid1_start(struct raid_bdev *raid_bdev)
{
	uint64_t min_blockcnt = UINT64_MAX;
	struct raid_base_bdev_info *base_info;
	struct raid1_info *r1info;

	r1info = calloc(1, sizeof(*r1info));
	if (!r1info) {
		SPDK_NOTICELOG("Failed to allocate RAID1 info device structure\n");
		return -ENOMEM;
	}
	r1info->raid_bdev = raid_bdev;

	RAID_FOR_EACH_BASE_BDEV(raid_bdev, base_info) {
		min_blockcnt = spdk_min(min_blockcnt, spdk_bdev_desc_get_bdev(base_info->desc)->blockcnt);
	}

	raid_bdev->bdev.blockcnt = min_blockcnt;
	raid_bdev->module_private = r1info;

	init_rebuild(raid_bdev);

#ifdef CHECK_REBUILD_MATRIX_INFO_ON_START
	SPDK_NOTICELOG("\n<-| RAID_BDEV+REBUILD on START!!! |->\n");
	SPDK_NOTICELOG("strip_size = %lu\n", raid_bdev->strip_size);
	SPDK_NOTICELOG("num_base_bdevs = %lu\n", raid_bdev->num_base_bdevs);
	SPDK_NOTICELOG("num_base_bdevs_discovered = %lu\n", raid_bdev->num_base_bdevs_discovered);
	SPDK_NOTICELOG("blockcnt = %lu\n", raid_bdev->bdev.blockcnt);
	SPDK_NOTICELOG("blocklen = %lu\n", raid_bdev->bdev.blocklen);
	SPDK_NOTICELOG("strips_per_area = %lu\n", raid_bdev->rebuild.strips_per_area);
	SPDK_NOTICELOG("rebuild_flag = %lu\n", raid_bdev->rebuild.rebuild_flag);
	SPDK_NOTICELOG("num_memory_areas = %lu\n", raid_bdev->rebuild.num_memory_areas);
	SPDK_NOTICELOG("<--------------------------->\n");
#endif

	return 0;
}

static bool
raid1_stop(struct raid_bdev *raid_bdev)
{

#ifdef CHECK_REBUILD_MATRIX_INFO_ON_STOP
	SPDK_NOTICELOG("\n<-| RAID_BDEV+REBUILD on STOP!!! |->\n");
	SPDK_NOTICELOG("strip_size = %lu\n", raid_bdev->strip_size);
	SPDK_NOTICELOG("num_base_bdevs = %lu\n", raid_bdev->num_base_bdevs);
	SPDK_NOTICELOG("num_base_bdevs_discovered = %lu\n", raid_bdev->num_base_bdevs_discovered);
	SPDK_NOTICELOG("blockcnt = %lu\n", raid_bdev->bdev.blockcnt);
	SPDK_NOTICELOG("blocklen = %lu\n", raid_bdev->bdev.blocklen);
	SPDK_NOTICELOG("strips_per_area = %lu\n", raid_bdev->rebuild.strips_per_area);
	SPDK_NOTICELOG("rebuild_flag = %lu\n", raid_bdev->rebuild.rebuild_flag);
	SPDK_NOTICELOG("num_memory_areas = %lu\n", raid_bdev->rebuild.num_memory_areas);
	SPDK_NOTICELOG("<--------------------------->\n");
#endif

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
