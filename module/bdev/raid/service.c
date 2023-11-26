/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   All rights reserved.
 */

#include "bdev_raid.h"
#include "spdk/env.h"
#include "spdk/bdev.h"
#include "spdk/likely.h"
#include "spdk/log.h"


#include "service.h"

// TODO: Сейчас iovcnt = 1, доделать, чтоб можно было создавать любого количества
#define _MUX_BUF_LENGTH 1
// ----- //

// TODO: мб не надо, переделать.
struct container {
    struct raid_bdev *raid_bdev;
    int idx;
    struct iovec * buff;
} typedef container;

/* Poller functionality */
int 
run_rebuild_poller(void* arg)
{
    struct raid_bdev *raid_bdev = arg;
    SPDK_WARNLOG("poller is working now with: %s!\n", raid_bdev->bdev.name);
    return 0;
}

static inline struct iovec *
alloc_buffer_elem(size_t iovlen, size_t align)
{
    struct iovec *buf;
    buf = spdk_dma_zmalloc(sizeof(struct iovec), 0, NULL);
    if(buf == NULL)
    {
        return NULL;
    }

    buf->iov_len = iovlen;
    buf->iov_base = spdk_dma_zmalloc(sizeof(uint8_t)*(buf->iov_len), 0, NULL);
    if(buf->iov_base == NULL)
    {   
        spdk_dma_free(buf);
        return NULL;
    }

    return buf;
}

static inline void
free_buffer_elem(struct iovec * buf_elem)
{
    spdk_dma_free(buf_elem->iov_base);
    spdk_dma_free(buf_elem);
}


static inline int 
fill_ones_write_request(struct iovec * buf_array, int buf_len)
{   
    // не учитывает align внутри iov_base
    SPDK_WARNLOG("i'm here 2 \n");

    if(buf_len > _MUX_BUF_LENGTH)
    {
        return -1;
    }
    for(size_t i = 0; i<buf_len; i++)
    {
        SPDK_WARNLOG("i'm here 3 \n");
        *((uint8_t *)(buf_array[i].iov_base)) = UINT8_MAX;
        SPDK_WARNLOG("i'm here 4 \n");
        SPDK_WARNLOG("%d", UINT8_MAX);
        SPDK_WARNLOG("i'm here 5 \n");

    }

    return 0;
}

void 
cd_read_func(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
    container *cont = cb_arg;
    if(success)
    {
        SPDK_WARNLOG("test (read) success\n");
        SPDK_WARNLOG("\n\n test = %d \n\n", *((uint8_t*)cont->buff->iov_base));
    } else {
        SPDK_ERRLOG("test (read) fail\n");
    }

    free_buffer_elem(cont->buff);
    spdk_bdev_free_io(bdev_io);
    free(cont);
    SPDK_WARNLOG("test (read) success\n");
}

void 
cd_write_func(struct spdk_bdev_io *bdev_io, bool success, void *cb_arg)
{
    container *cont = cb_arg;
    if(success)
    {
        SPDK_WARNLOG("test (write) success\n");
    } else {
        SPDK_ERRLOG("test (write) fail\n");
    }

    free_buffer_elem(cont->buff);
    spdk_bdev_free_io(bdev_io);
    submit_read_request_base_bdev(cont->raid_bdev, cont->idx);
    free(cont);
}

void
submit_write_request_base_bdev(struct raid_bdev *raid_bdev, uint8_t idx)
{   
    struct spdk_bdev_desc *desc = __base_desc_from_raid_bdev(raid_bdev, idx);
    struct spdk_bdev *base_bdev = spdk_bdev_desc_get_bdev(desc);
    struct spdk_io_channel *ch = spdk_bdev_get_io_channel(desc);
    struct iovec *buffer;
    container *cont;
    int ret;


    buffer = alloc_buffer_elem(base_bdev->blocklen, base_bdev->required_alignment);
    if (fill_ones_write_request(buffer, 1) != 0)
    {
        SPDK_ERRLOG("fill_ones_write_request was fail\n");
    }
    cont = calloc(1, sizeof(container));
    if (cont == NULL){
        SPDK_ERRLOG("cont alloc failed\n");
    }

    cont->buff = buffer;
    cont->idx = 0;
    cont->raid_bdev = raid_bdev;
    ret = spdk_bdev_writev_blocks (desc, ch, buffer, 1, 0, 1, cd_write_func, cont);

    if(spdk_likely(ret == 0))
    {
        SPDK_WARNLOG("submit test (write) success\n");
    } else {
        SPDK_ERRLOG("submit test (write) fail\n");
    }
}

void
submit_read_request_base_bdev(struct raid_bdev *raid_bdev, uint8_t idx)
{
    struct spdk_bdev_desc *desc = __base_desc_from_raid_bdev(raid_bdev, idx);
    struct spdk_bdev *base_bdev = spdk_bdev_desc_get_bdev(desc);
    struct spdk_io_channel *ch = spdk_bdev_get_io_channel(desc);
    struct iovec *buffer;
    container *cont;
    int ret;

    buffer = alloc_buffer_elem(base_bdev->blocklen, base_bdev->required_alignment);

    cont = calloc(1, sizeof(container));
    if (cont == NULL){
        SPDK_ERRLOG("cont alloc failed\n");
    }

    cont->buff = buffer;
    cont->idx = 0;
    cont->raid_bdev = raid_bdev;
    ret = spdk_bdev_readv_blocks(desc, ch, buffer, 1, 0, 1, cd_read_func, cont);

    if(spdk_likely(ret == 0))
    {
        SPDK_WARNLOG("submit test (read) success\n");
    } else {
        SPDK_ERRLOG("submit test (read) fail\n");
    }
}
