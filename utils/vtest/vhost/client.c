/*
 * client.c
 *
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
*/

#include <linux/vhost.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>


#include "client.h"

//Todo implement sendmsg and recv wrapper functions
int
client_init_path(Client *client, const char *path) {

    if (!client || !path || strlen(path) == 0) {
        return E_CLIENT_ERR_FARG;
    }

   strncpy(client->socket_path, path, PATH_MAX);

   return E_CLIENT_OK;
}

int
clinet_init_socket(Client *client) {

    if (!client) {
        return E_CLIENT_ERR_FARG;
    }

    client->socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client->socket == -1) {

        return E_CLIENT_ERR_SOCK;
    }
    return E_CLIENT_OK;

}

int
client_connect_socket(Client *client) {

    struct sockaddr_un unix_socket = {0, .sun_path = {0}};
    size_t addrlen = 0;

    if (!client->socket || strlen(client->socket_path) == 0) {
        return E_CLIENT_ERR_FARG;
    }

    unix_socket.sun_family = AF_UNIX;
    strncpy(unix_socket.sun_path, client->socket_path, PATH_MAX);
    addrlen = strlen(unix_socket.sun_path) + sizeof(AF_UNIX);

    if (connect(client->socket, (struct sockaddr *)&unix_socket, addrlen)  == -1) {

        return E_CLIENT_ERR_CONN;
    }

    return E_CLIENT_OK;
}

int
client_disconnect_socket(Client *client) {

    if (!client) {
        return E_CLIENT_ERR_FARG;
    }

    close(client->socket);

    return E_CLIENT_OK;
}

int
vhost_ioctl(Client *client, VhostUserRequest request, void *req_ptr) {

    Client *const cl = client;
    int fd[VHOST_MEMORY_MAX_NREGIONS] = {-2};
    VhostUserMsg message = {0, .flags=0};
    CLIENT_H_RET_VAL ret_val = E_CLIENT_OK;

    if (!client) {
        return E_CLIENT_ERR_FARG;
    }

    /* Function argument pointer (req_ptr) SHOULD not be null */
    switch (request) {
        case VHOST_USER_SET_MEM_TABLE:
        case VHOST_USER_SET_LOG_BASE:
        case VHOST_USER_SET_LOG_FD:
        case VHOST_USER_SET_VRING_KICK:
        case VHOST_USER_SET_VRING_CALL:
        case VHOST_USER_SET_VRING_ERR:
            if (!req_ptr) {
                return E_CLIENT_ERR_FARG;
            }
            break;

        default:
            break;
    }

    message.request = request;
    message.flags &= ~VHOST_USER_VERSION_MASK;
    message.flags |= QEMU_PROT_VERSION;

    /* Set message structure for sending data */
    ret_val = vhost_ioctl_set_send_msg(cl, request, req_ptr, &message, fd);

    if (!(ret_val == E_CLIENT_OK || ret_val == E_CLIENT_VIOCTL_REPLY)) {
        return E_CLIENT_ERR_VIOCTL;
    }
    //todo send msg

    if (ret_val == E_CLIENT_VIOCTL_REPLY) {

        ret_val = vhost_ioctl_set_recv_msg(request, req_ptr, &message);
        //todo recv msg

        /* Set message structure after receive data */
        if (!(ret_val == E_CLIENT_OK)) {
            return E_CLIENT_ERR_VIOCTL;

        }

    }

    return E_CLIENT_OK;
}

int
vhost_ioctl_set_send_msg(Client *client, VhostUserRequest request, void *req_ptr,
                     VhostUserMsg *msg, int *fd ) {

    VhostUserMsg *const message = msg;
    bool msg_has_reply = false;

    size_t fd_num = -1;
    struct vring_file {unsigned int index; int fd;} *file;

    if (!client || msg || fd) {

        return E_CLIENT_ERR_FARG;
    }


    switch (request) {

        case VHOST_USER_NONE:
            break;

        case VHOST_USER_GET_FEATURES:
        case VHOST_USER_GET_VRING_BASE:
            msg_has_reply = true;
            break;

        case VHOST_USER_SET_FEATURES:
        case VHOST_USER_SET_LOG_BASE:
            message->u64 = *((uint64_t *) req_ptr);
            message->size = sizeof(((VhostUserMsg*)0)->u64);
            /* if VHOST_USER_PROTOCOL_F_LOG_SHMFD
            msg_has_reply = true;
            */
            break;

        case VHOST_USER_SET_OWNER:
        case VHOST_USER_RESET_OWNER:
            break;

        case VHOST_USER_SET_MEM_TABLE:
            memcpy(&message->memory, req_ptr, sizeof(VhostUserMemory));
            message->size = sizeof(((VhostUserMemory*)0)->padding);
            message->size += sizeof(((VhostUserMemory*)0)->nregions);

            for (fd_num = 0; fd_num < message->memory.nregions; fd_num++) {
                fd[fd_num] = client->sh_mem_fds[fd_num];
                message->size = sizeof(VhostUserMemoryRegion);
            }
            break;

        case VHOST_USER_SET_LOG_FD:
            fd[++fd_num] = *((int *) req_ptr);
            break;

        case VHOST_USER_SET_VRING_NUM:
        case VHOST_USER_SET_VRING_BASE:
            memcpy(&message->state, req_ptr, sizeof(((VhostUserMsg*)0)->state));
            message->size = sizeof(((VhostUserMsg*)0)->state);
            break;

        case VHOST_USER_SET_VRING_ADDR:
            memcpy(&message->addr, req_ptr, sizeof(((VhostUserMsg*)0)->addr));
            message->size = sizeof(((VhostUserMsg*)0)->addr);
            break;

        case VHOST_USER_SET_VRING_KICK:
        case VHOST_USER_SET_VRING_CALL:
        case VHOST_USER_SET_VRING_ERR:
            file = req_ptr;
            message->u64 = file->index;
            message->size = sizeof(((VhostUserMsg*)0)->u64);
            if (file->fd > 0 ) {
                client->sh_mem_fds[fd_num++] = file->fd;
            }
            break;

        default:
            return E_CLIENT_ERR_IOCTL_SEND;

    }
    if (msg_has_reply) {
       return E_CLIENT_VIOCTL_REPLY;
    }

    return E_CLIENT_OK;
}

int
vhost_ioctl_set_recv_msg(VhostUserRequest request, void *req_ptr, VhostUserMsg *msg) {

    VhostUserMsg *const message = msg;

    if (!msg) {

        return E_CLIENT_ERR_FARG;
    }

    switch (request) {
        case VHOST_USER_GET_FEATURES:
            *((uint64_t *) req_ptr) = message->u64;
            break;
        case VHOST_USER_GET_VRING_BASE:
            memcpy(req_ptr, &message->state, sizeof(struct vhost_vring_state));

        default:
            return E_CLIENT_ERR_IOCTL_REPLY;
    }


    return E_CLIENT_OK;
}




