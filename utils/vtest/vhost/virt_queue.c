
//TODO: ALIGN, Align in virtq, vring DONE
//Tuesday: Communication between driver and host

#include "virt_queue.h"
#include "uvhost.h"

int
virt_queue_map_all_mem_reqion_virtq(uvhost_virtq **virtq, VhostUserMemory *mem,
                                     size_t virtq_number) {

    int ret = 0;

    if (!virtq || !*virtq) {
        return E_VIRT_QUEUE_ERR_FARG;
    }

    for (size_t i = 0; i < virtq_number; i++) {

        ret = (virt_queue_map_mem_reqion_virtq((virtq + i),
                    mem->regions[i].guest_phys_addr));
       //TODO: if (ret)
       // handle error
    }

    return E_VIRT_QUEUE_OK;
}

int
virt_queue_map_mem_reqion_virtq(uvhost_virtq **virtq, uint64_t guest_phys_addr) {

    if (!virtq) {
        return E_VIRT_QUEUE_ERR_FARG;
    }

    virt_queue_map_vring(virtq, (void *)guest_phys_addr);
    if (!*virtq) {
        //TODO handle error

        return E_VIRT_QUEUE_ERR_MAP_REG;
    }

    return E_VIRT_QUEUE_OK;
}

int
virt_queue_map_vring(uvhost_virtq **virtq, void *base_virtq_addr) {

    uvhost_virtq *const virtq_map = (uvhost_virtq *)base_virtq_addr;
    uintptr_t desc_addr = (uintptr_t)((uintptr_t *)virtq_map + sizeof(uvhost_virtq));

    for (size_t i = 0; i < VIRTQ_DESC_MAX_SIZE; i++) {

       virtq_map->desc[i].len = VIRTQ_DESC_BUFF_SIZE;
       virtq_map->desc[i].flags = VIRTIO_DESC_F_WRITE;
       virtq_map->desc[i].next = i + 1;
       virtq_map->desc[i].addr = desc_addr;

       desc_addr = desc_addr + VIRTQ_DESC_BUFF_SIZE;
    }

    virtq_map->avail.idx = 0;
    virtq_map->used.idx = 0;
    virtq_map->desc[VIRTQ_DESC_MAX_SIZE - 1].next = VIRTQ_IDX_NONE;

    *virtq = virtq_map;

    return E_VIRT_QUEUE_OK;
}


