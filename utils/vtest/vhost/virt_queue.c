
//TODO: ALIGN, Align in virtq, vring DONE
//Tuesday: Communication between driver and host

#include <string.h>
#include <sys/eventfd.h>
#include <stdio.h>

#include "virt_queue.h"
#include "uvhost.h"
#include "client.h"

int
virt_queue_map_all_mem_reqion_virtq(struct uvhost_virtq *virtq[], VhostUserMemory *mem,
                                     size_t virtq_number) {

    VIRT_QUEUE_H_RET_VAL ret_val = E_VIRT_QUEUE_OK;

    if (!virtq) {
        return E_VIRT_QUEUE_ERR_FARG;
    }    for (size_t i = 0; i < virtq_number; i++) {


        ret_val = (virt_queue_map_mem_reqion_virtq((&(virtq[i])),
                    mem->regions[i].guest_phys_addr));
        if (ret_val != E_VIRT_QUEUE_OK){
            return ret_val;
        }
    }

    return E_VIRT_QUEUE_OK;
}

int
virt_queue_map_mem_reqion_virtq(struct uvhost_virtq **virtq, uint64_t guest_phys_addr) {

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
virt_queue_map_vring(struct uvhost_virtq **virtq, void *base_virtq_addr) {
    printf("Ej boha %p \n",base_virtq_addr);

    struct uvhost_virtq *virtq_map = (struct uvhost_virtq *)base_virtq_addr;
    uintptr_t desc_addr = (uintptr_t)((uintptr_t *)virtq_map + sizeof(struct uvhost_virtq));

    if (!virtq || !base_virtq_addr) {

        return E_VIRT_QUEUE_ERR_FARG;
    }

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

int
virtq_queue_set_host_vring(Client *client, struct set_host_virtq set_virtq) {

    if (!client) {
        return E_VIRT_QUEUE_ERR_FARG;
    }

    if (client_vhost_ioctl(client, VHOST_USER_SET_VRING_NUM, &(set_virtq.num)) != E_CLIENT_OK) {
        return E_VIRT_QUEUE_ERR_HOST_VIRTQ;
    }

    if (client_vhost_ioctl(client, VHOST_USER_SET_VRING_BASE, &(set_virtq.base)) != E_CLIENT_OK) {
        return E_VIRT_QUEUE_ERR_HOST_VIRTQ;
    }

    if (client_vhost_ioctl(client, VHOST_USER_SET_VRING_KICK, &(set_virtq.kick)) != E_CLIENT_OK) {
        return E_VIRT_QUEUE_ERR_HOST_VIRTQ;
    }

    if (client_vhost_ioctl(client, VHOST_USER_SET_VRING_CALL, &(set_virtq.call)) != E_CLIENT_OK) {
        return E_VIRT_QUEUE_ERR_HOST_VIRTQ;
    }

    if (client_vhost_ioctl(client, VHOST_USER_SET_VRING_ADDR, &(set_virtq.addr)) != E_CLIENT_OK) {
        return E_VIRT_QUEUE_ERR_HOST_VIRTQ;
    }

    return E_VIRT_QUEUE_OK;
}

int
virtq_set_host_virtq_table(uvhost_virtq **virtq, size_t virtq_table_size, Client *client) {

    VIRT_QUEUE_H_RET_VAL ret_val = E_VIRT_QUEUE_OK;
    struct set_host_virtq set_virtq_init;

    if (!virtq || !*virtq || !client) {

        return E_VIRT_QUEUE_ERR_FARG;
    }

    memset(&(set_virtq_init), 0, sizeof(struct set_host_virtq));

    for (size_t i = 0; i < virtq_table_size; i++) {

        set_virtq_init.num.index = i;
        set_virtq_init.num.num = VIRTQ_DESC_MAX_SIZE;

        set_virtq_init.base.index = i;
        set_virtq_init.num.num = 0;

        set_virtq_init.kick.index = i;
        set_virtq_init.kick.fd = eventfd(0, EFD_NONBLOCK);

        set_virtq_init.call.index = i;
        set_virtq_init.call.fd = eventfd(0, EFD_NONBLOCK);

        if (set_virtq_init.kick.fd < 0 || set_virtq_init.kick.fd < 0) {
            return E_VIRT_QUEUE_ERR_HOST_VIRTQ;
        }

       set_virtq_init.addr.index = i;
       set_virtq_init.addr.desc_user_addr = (uintptr_t)&virtq[i]->desc;
       set_virtq_init.addr.avail_user_addr = (uintptr_t)&virtq[i]->avail;
       set_virtq_init.addr.used_user_addr = (uintptr_t)&virtq[i]->used;
       set_virtq_init.addr.log_guest_addr = (uintptr_t)NULL;
       set_virtq_init.addr.flags = 0;

       ret_val = virtq_queue_set_host_vring(client, set_virtq_init);
       if (ret_val != E_VIRT_QUEUE_OK) {
            return ret_val;
       }
       memset(&(set_virtq_init), 0, sizeof(struct set_host_virtq));

    }

    return E_VIRT_QUEUE_OK;
}

