/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2018 Intel Corporation.
 *   All rights reserved.
 */

#include "spdk/env.h"
#include "spdk/bdev.h"
#include "spdk/likely.h"
#include "spdk/log.h"
#include "spdk/util.h"

#include "bdev_raid.h"
#include "service.h"

#define DEBUG__

/* ============================ TESTS ==================================== */

// TODO: Сейчас iovcnt = 1, доделать, чтоб можно было создавать любого количества
#define _MUX_BUF_LENGTH 1
// ----- //

// TODO: мб не надо, переделать.
struct container {
    struct raid_bdev *raid_bdev;
    int idx;
    struct iovec * buff;
} typedef container;


static inline struct iovec *
alloc_continuous_buffer_part(size_t iovlen, size_t align)
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
free_continuous_buffer_part(struct iovec * buf_elem)
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

    free_continuous_buffer_part(cont->buff);
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

    free_continuous_buffer_part(cont->buff);
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


    buffer = alloc_continuous_buffer_part(base_bdev->blocklen, base_bdev->required_alignment);
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

    buffer = alloc_continuous_buffer_part(base_bdev->blocklen, base_bdev->required_alignment);

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
/* ======================================================================== */

/* ======================= Poller functionality =========================== */

static inline void
_free_sg_buffer_part(struct iovec *vec_array, uint32_t len)
{ //TODO: проверить, что оно работает (в for совсем неуверен)
    struct iovec *base_vec;

    for(base_vec = vec_array; base_vec < vec_array + len; base_vec++)
    {
        spdk_dma_free(base_vec->iov_base);
    }
}

static inline void
free_sg_buffer (struct iovec **vec_array, uint32_t len)
{
    /* usage: struct iovec *a; free_sg_buffer(&a, b); */

    _free_sg_buffer_part(*vec_array, len);
    spdk_dma_free(*vec_array);
    *vec_array = NULL;
}

static inline struct iovec *
allocate_sg_buffer(uint32_t elem_size, uint32_t elemcnt, uint32_t elem_per_vec)
{
    uint32_t split_steps = SPDK_CEIL_DIV(elemcnt, elem_per_vec);
    uint32_t full_split_steps = elemcnt / elem_per_vec;
    uint32_t tail_split_size_in_blocks = elemcnt - (full_split_steps * elem_per_vec);

    struct iovec *vec_array = spdk_dma_zmalloc(sizeof(struct iovec)*split_steps, 0, NULL);
    if(vec_array == NULL)
    {
        return NULL;
    }

    if (split_steps != full_split_steps)
    {
        vec_array[0].iov_len = elem_size*tail_split_size_in_blocks;
        vec_array[0].iov_base = (void*)spdk_dma_zmalloc(sizeof(uint8_t)*vec_array[0].iov_len, 0, NULL);
        if(vec_array[0].iov_base == NULL)
        {
            spdk_dma_free(vec_array);
            return NULL;
        }
    }

    for (uint32_t i = 1; i < split_steps; i++)
    { //TODO: люблю лажать с индексами, проверить, что все ок
        vec_array[i].iov_len = elem_size*elem_per_vec;
        vec_array[i].iov_base = (void*)spdk_dma_zmalloc(sizeof(uint8_t)*vec_array[i].iov_len, 0, NULL);
        if(vec_array[i].iov_base == NULL)
        {
            _free_sg_buffer_part(vec_array, i);
            spdk_dma_free(vec_array);
            return NULL;
        }
    }
    return vec_array;
}

/*
TODO: надо сделать какую-то крутую функция калбэка (наверное, нужен частичный и полный)
При полном, надо обнулять флаги REBUILD_FLAG_NEED_REBUILD и REBUILD_FLAG_IN_PROGRESS
*/

int
run_rebuild_poller(void* arg)
{ //TODO: в целом реализация наивная (без учета многопоточности) - не очень пока понимаю, как ее прикрутить.
    struct raid_bdev *raid_bdev = arg;
    struct raid_rebuild *rebuild = raid_bdev->rebuild;

#ifdef DEBUG__
    SPDK_WARNLOG("poller is working now with: %s!\n", raid_bdev->bdev.name);
#endif

    if(rebuild == NULL)
    {
       SPDK_WARNLOG("%s doesn't have rebuild struct!\n", raid_bdev->bdev.name);
       return 1;
    }

    if(!SPDK_TEST_BIT(&rebuild->rebuild_flag, REBUILD_FLAG_INITIALIZED))
    {
        /* the rebuild structure has not yet been initialized */
        return 0;
    }

    if (!SPDK_TEST_BIT(&(rebuild->rebuild_flag), REBUILD_FLAG_IN_PROGRESS))
    {
        /* The recovery process is not complete */
        //TODO: я правильно понял, что мне надо обработать этот кейс, так как поллер будет запускаться даже тогда, когда я еще не завершил обработку.
        return 0;
    }

    if (!SPDK_TEST_BIT(&(rebuild->rebuild_flag), REBUILD_FLAG_NEED_REBUILD))
    {
        /* The raid doesn't need to start rebuild process */
        return 0;
    }

    if (raid_bdev->module->rebuild_request != NULL)
    {
        raid_bdev->module->rebuild_request(raid_bdev);
    }

    // uint64_t

    for (uint64_t i = 0; i < rebuild->num_memory_areas ; i++)
    {

    }

    return 0;
}
