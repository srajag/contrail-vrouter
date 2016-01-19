/*
 * uvhost.c
 *
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

/*TODO DEALLOCATION */
/*TODO Warning/error msgs */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "util.h"
#include "uvhost.h"
#include "client.h"
#include "sh_mem.h"
#include "virt_queue.h"
#include "virtio_hdr.h"

/* TODO */

/* Copied from reference app, I don't know why we must align to 1MB :-( */
#define VHOST_CLIENT_PAGE_SIZE \
        ALIGN(sizeof(struct uvhost_virtq) + VIRTQ_DESC_BUFF_SIZE * VIRTQ_DESC_MAX_SIZE, 1024 * 1024)
#define ALIGN(v, b) (((long int)v + (long int)b -1) & (-(long int)b))


int
uvhost_alloc_VhostClient(VhostClient **vhost_client) {

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    *vhost_client = (VhostClient *) calloc(1, sizeof(VhostClient));
     return vhost_client? E_UVHOST_OK: E_UVHOST_ERR_ALLOC;
}

int
uvhost_init_VhostClient(VhostClient *vhost_client) {

    VhostClient *const vhost_cl = vhost_client;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    vhost_cl->mem.nregions = VHOST_CLIENT_VRING_MAX_VRINGS;
    vhost_cl->virtq_num = VHOST_CLIENT_VRING_MAX_VRINGS;
    vhost_cl->page_size = VHOST_CLIENT_PAGE_SIZE;

    return E_UVHOST_OK;
}

int inline
uvhost_set_mem_VhostClient(VhostClient *vhost_client) {

    int ret = 0;
    void *sh_mem_addr = NULL;
    char fd_path_buff[PATH_MAX] = {'\0'};
    VhostClient *const vhost_cl = vhost_client;
    VIRT_QUEUE_H_RET_VAL ret_val = E_VIRT_QUEUE_OK;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    for (size_t i = 0; i < vhost_cl->mem.nregions; i++) {

        snprintf(fd_path_buff, PATH_MAX, "%s%d", vhost_cl->client.sh_mem_path, (int)i);

        ret = sh_mem_init_fd(fd_path_buff,
                (vhost_cl->client.sh_mem_fds + i));
        if (ret != E_SH_MEM_OK) {
            return E_UVHOST_ERR_UNK;
        }

        sh_mem_addr = sh_mem_mmap(*(vhost_cl->client.sh_mem_fds + i),
                vhost_cl->page_size);
        if (sh_mem_addr == NULL ) {
            return E_UVHOST_ERR_UNK;
        }

        vhost_cl->mem.regions[i].userspace_addr = (uintptr_t) sh_mem_addr;
        vhost_cl->mem.regions[i].guest_phys_addr = (uintptr_t) sh_mem_addr;
        vhost_cl->mem.regions[i].memory_size = vhost_cl->page_size;
        vhost_cl->mem.regions[i].mmap_offset = 0;

        printf("Ej boha %lu \n", vhost_cl->mem.regions[i].guest_phys_addr);
        memset(fd_path_buff, 0, sizeof(char) * PATH_MAX);
    }

    ret_val = virt_queue_map_all_mem_reqion_virtq(vhost_cl->sh_mem_virtq_table,
           &vhost_client->mem, VHOST_CLIENT_VRING_MAX_VRINGS);
    if (ret_val != E_VIRT_QUEUE_OK) {
        return ret_val;
    }
    return E_UVHOST_OK;
}

VhostClient*
uvhost_create_vhost_client(void) {

    VhostClient *vhost_client = NULL;
    UVHOST_H_RET_VAL uvhost_ret_val = E_UVHOST_OK;
    CLIENT_H_RET_VAL client_ret_val = E_CLIENT_OK;

    uvhost_ret_val = uvhost_alloc_VhostClient(&vhost_client);
    if (uvhost_ret_val != E_UVHOST_OK) {
        return NULL;
        //boze na nebesiach
    }

    uvhost_ret_val = uvhost_init_VhostClient(vhost_client);
    if (uvhost_ret_val != E_UVHOST_OK) {
        return NULL;
        //boze na nebesiach
    }

    client_ret_val = client_init_Client(&vhost_client->client, "/var/run/vrouter/uvh_vif_vm1");
    if (client_ret_val != E_CLIENT_OK) {
        return NULL;
    }

    return vhost_client;
}

int
uvhost_run_vhost_client(void) {

    VhostClient *vhost_client = NULL;
    UVHOST_H_RET_VAL uvhost_ret_val = E_UVHOST_OK;

    vhost_client = uvhost_create_vhost_client();
    if (!vhost_client) {
        return E_UVHOST_ERR;
    }

    uvhost_ret_val = uvhost_set_mem_VhostClient(vhost_client);
    if (uvhost_ret_val != E_UVHOST_OK) {
        return uvhost_ret_val;
        //boze na nebesiach
    }
    //todo This will be problem -> probably needs rewrite structure
  //  utils_add_fd_to_fd_rw_t(&(vhost_client->client.fd_rw_list), FD_TYPE_READ, vhost_client->sh_mem_virtq_table[VHOST_CLIENT_VRING_IDX_RX]->kickfd);


    uvhost_ret_val = uvhost_init_control_communication(vhost_client);
    if (uvhost_ret_val != E_UVHOST_OK) {
        return uvhost_ret_val;
        //boze na nebesiach
    }


    return E_UVHOST_OK;
}


int
uvhost_init_control_communication(VhostClient *vhost_client) {

    VhostClient *const l_vhost_client = vhost_client;
    UVHOST_H_RET_VAL ret_val = E_UVHOST_OK;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    ret_val = uvhost_vhost_init_control_msgs(l_vhost_client);
    if (ret_val != E_UVHOST_OK) {
        return ret_val;
    }

    return E_UVHOST_OK;
}

int
uvhost_vhost_init_control_msgs(VhostClient *vhost_client) {

    Client *l_client = NULL ;
    VhostClient *const l_vhost_client = vhost_client;
    CLIENT_H_RET_VAL ret_val = E_CLIENT_OK;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    l_client = &(l_vhost_client)->client;

    if (!l_client->socket || (strlen(l_client->socket_path) == 0)) {
        return E_UVHOST_ERR_FARG;
    }

    ret_val = client_vhost_ioctl(l_client, VHOST_USER_SET_OWNER, 0);
    if (ret_val != E_CLIENT_OK) {
        return E_UVHOST_ERR;
    }

    ret_val = (client_vhost_ioctl(l_client, VHOST_USER_GET_FEATURES,
               &l_vhost_client->features));
    if (ret_val != E_CLIENT_OK) {
        return E_UVHOST_ERR;
    }

    ret_val = (client_vhost_ioctl(l_client, VHOST_USER_SET_MEM_TABLE,
               &l_vhost_client->mem));
    if (ret_val != E_CLIENT_OK) {
        return E_UVHOST_ERR;
    }

    return E_UVHOST_OK;
}

void
uvhost_safer_free(void **mem) {

    if (mem && *mem) {
        free(*mem);
        *mem = NULL;
    }

    return;
}

