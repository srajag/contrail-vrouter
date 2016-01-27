/*
 * uvhost.c
 *
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

/*TODO Warning/error msgs */
/*TODO close fds - kick and call */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/un.h>

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



int
uvhost_alloc_VhostClient(VhostClient **vhost_client) {

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    *vhost_client = (VhostClient *) calloc(1, sizeof(VhostClient));
     if(!*vhost_client) {
         return  E_UVHOST_ERR_ALLOC;
     }

     for (size_t i = 0; i < VHOST_CLIENT_VRING_MAX_VRINGS; i++) {
         (*vhost_client)->virtq_control[i] = calloc(1, sizeof(virtq_control));
         if ((*vhost_client)->virtq_control[i] == NULL) {
            return E_UVHOST_ERR_ALLOC;
         }
     }

     return E_UVHOST_OK;
}

int
uvhost_dealloc_VhostClient(VhostClient *vhost_client) {

    for (size_t i = 0 ; i < VHOST_CLIENT_VRING_MAX_VRINGS; i++) {
        uvhost_safe_free(vhost_client->virtq_control[i]);
    }
    uvhost_safe_free(vhost_client);

    return E_UVHOST_OK;

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
    char fd_path_buff[UNIX_PATH_MAX] = {'\0'};
    VhostClient *const vhost_cl = vhost_client;
    VIRT_QUEUE_H_RET_VAL ret_val = E_VIRT_QUEUE_OK;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    for (size_t i = 0; i < vhost_cl->mem.nregions; i++) {

        snprintf(fd_path_buff, UNIX_PATH_MAX, "%s.%d", vhost_cl->client.sh_mem_path, (int)i);

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

        memset(fd_path_buff, 0, sizeof(char) * UNIX_PATH_MAX);
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
uvhost_unset_sh_mem_VhostClient(VhostClient *vhost_client) {

    char fd_path_buff[UNIX_PATH_MAX] = {'\0'};
    VhostClient *const vhost_cl = vhost_client;
    int ret = 0;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    for (size_t i = 0; i < vhost_client->mem.nregions; i++) {
        snprintf(fd_path_buff, UNIX_PATH_MAX, "%s.%d", vhost_cl->client.sh_mem_path, (int)i);

        ret = sh_mem_unmmap((void *)vhost_cl->mem.regions[i].guest_phys_addr,
                vhost_cl->mem.regions[i].memory_size);
        ret = sh_mem_unlink(fd_path_buff);

        memset(fd_path_buff, 0, sizeof(char) * UNIX_PATH_MAX);
    }

    return E_UVHOST_OK;
}

int
uvhost_delete_VhostClient(VhostClient *vhost_client) {

    UVHOST_H_RET_VAL uvhost_ret_val = E_UVHOST_OK;
    CLIENT_H_RET_VAL client_ret_val = E_CLIENT_OK;

    VhostClient *const vhost_cl = vhost_client;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    client_ret_val = client_disconnect_socket(&vhost_cl->client);
    client_ret_val = client_close_fds(&vhost_cl->client);

    uvhost_unset_sh_mem_VhostClient(vhost_cl);
    uvhost_ret_val = uvhost_dealloc_VhostClient(vhost_cl);


    return uvhost_ret_val;
}

static struct AppHandler vhost_net_app_handler = {
    .context = 0,
    .poll_handler = uvhost_poll_client_tx
};

int
uvhost_run_vhost_client(void) {

    VhostClient *vhost_client = NULL;
    UVHOST_H_RET_VAL uvhost_ret_val = E_UVHOST_OK;
    VIRT_QUEUE_H_RET_VAL virt_queue_ret_val = E_VIRT_QUEUE_OK;

    vhost_client = uvhost_create_vhost_client();
    if (!vhost_client) {
        return E_UVHOST_ERR;
    }

    uvhost_ret_val = uvhost_set_mem_VhostClient(vhost_client);
    if (uvhost_ret_val != E_UVHOST_OK) {
        return uvhost_ret_val;
        //boze na nebesiach
    }

    uvhost_ret_val = uvhost_init_control_communication(vhost_client);
    if (uvhost_ret_val != E_UVHOST_OK) {
        return uvhost_ret_val;
        //boze na nebesiach
    }

    virt_queue_ret_val = virt_queue_map_uvhost_virtq_2_virtq_control(vhost_client);

    utils_add_fd_to_fd_rw_t(&(vhost_client->client.fd_rw_list), FD_TYPE_READ,
            vhost_client->sh_mem_virtq_table[VHOST_CLIENT_VRING_IDX_RX]->kickfd,
            (void *)vhost_client, uvhost_kick_client);

    memcpy(&vhost_client->client.vhost_net_app_handler,
           &vhost_net_app_handler, sizeof(struct AppHandler));

    vhost_client->client.vhost_net_app_handler.context = vhost_client;

    size_t iter = 10;
    while(iter) {
        (vhost_client->client.vhost_net_app_handler.

            poll_handler(vhost_client->client.vhost_net_app_handler.context)
        );
    }

    uvhost_delete_VhostClient(vhost_client);

    return E_UVHOST_OK;
}

int
uvhost_kick_client(struct fd_rw_element *fd_rw_element) {

    VhostClient *vhost_client = (VhostClient *)fd_rw_element->context;
    int kickfd = fd_rw_element->fd;
    ssize_t return_val;
    uint64_t kick_it = 0;

    if (!fd_rw_element) {
        return E_UVHOST_ERR_FARG;
    }

    return_val = read(kickfd, &kick_it, sizeof(kick_it));
    if (return_val < 0) {

        printf("Error read < 0 \n");
        //TODO
        return E_UVHOST_OK;

    } else if (return_val == 0 ) {

        printf("Error read == 0 \n");
        //TODO
        return E_UVHOST_OK;
    }
    virt_queue_process_avail_virt_queue((vhost_client->virtq_control),
            VHOST_CLIENT_VRING_IDX_RX);

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

static int
send_packet(VhostClient *vhost_client) {

   static char data_send [55] = { 0xde,0xad, 0xbe, 0xef, 0x00, 0x01, 0xde, 0xad, 0xbe, 0xef, 0x00, 0x02, 0x00,0x08, 0x00, 0x45, 0xcc, 0x00, 0x1d, 0x09, 0xf0, 0x92, 0xab, 0x08, 0x06, 0x00, 0x01,
0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0x00, 0x1d, 0x09, 0xf0, 0x92, 0xab, 0x0a, 0x0a, 0x0a, 0x01,
0x00, 0x1a, 0x6b, 0x6c, 0x0c, 0xcc, 0x0a, 0x0a, 0x0a, 0x02};
    int ret = 0;

    ret =virt_queue_put_vring(vhost_client->virtq_control,
            VHOST_CLIENT_VRING_IDX_TX, data_send, 55);
    if (ret != E_VIRT_QUEUE_OK) {
        return E_UVHOST_ERR;
    }


    return E_UVHOST_OK;//virt_queue_kick(vhost_client->virtq_control, VHOST_CLIENT_VRING_IDX_TX);
}


int
uvhost_poll_client_tx(void *context) {

    VhostClient *vhost_client = (VhostClient *) context;
    uint32_t vq_id = VHOST_CLIENT_VRING_IDX_TX;
    int ret = 0;
/*
    char data_send[] = { 0x00, 0xde, 0xad, 0xbe, 0xef,
        0x00, 0x02, 0xde, 0xad, 0xbe, 0xef, 0x11, 0x01,
        0x08, 0x06, 0x99, 0x99 };
*/

  //size_t data_len = sizeof(data_send);

    virt_queue_process_used_virt_queue(vhost_client->virtq_control, vq_id);
    ret = send_packet(vhost_client);
    if (ret != E_UVHOST_OK) {
        return E_UVHOST_ERR;
    }

    return E_UVHOST_OK;
}

int
uvhost_vhost_init_control_msgs(VhostClient *vhost_client) {

    Client *l_client = NULL ;
    VhostClient *const l_vhost_client = vhost_client;
    CLIENT_H_RET_VAL client_ret_val = E_CLIENT_OK;
    VIRT_QUEUE_H_RET_VAL virt_queue_ret_val = E_VIRT_QUEUE_OK;

    if (!vhost_client) {
        return E_UVHOST_ERR_FARG;
    }

    l_client = &(l_vhost_client)->client;

    if (!l_client->socket || (strlen(l_client->socket_path) == 0)) {
        return E_UVHOST_ERR_FARG;
    }

    client_ret_val = client_vhost_ioctl(l_client, VHOST_USER_SET_OWNER, 0);
    if (client_ret_val != E_CLIENT_OK) {
        return E_UVHOST_ERR;
    }

    client_ret_val = (client_vhost_ioctl(l_client, VHOST_USER_GET_FEATURES,
               &l_vhost_client->features));
    if (client_ret_val != E_CLIENT_OK) {
        return E_UVHOST_ERR;
    }

    client_ret_val = (client_vhost_ioctl(l_client, VHOST_USER_SET_MEM_TABLE,
               &l_vhost_client->mem));
    if (client_ret_val != E_CLIENT_OK) {
        return E_UVHOST_ERR;
    }

    virt_queue_ret_val = virt_queue_set_host_virtq_table(l_vhost_client->sh_mem_virtq_table,
                VHOST_CLIENT_VRING_MAX_VRINGS, l_client);

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

