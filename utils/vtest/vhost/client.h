
/*
 * client.h
 *
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
*/
#ifndef CLIENT_H
#define CLIENT_H

#include <limits.h>
#include <sys/socket.h>

#include "uvhost.h"
#include "util.h"

/* FDs First initialization value
 *
 * The value is set, cause to be sure, that FD is not opened and/or
 * the function shm_open does not returns an error.
 *
 */
#define CLIENT_INIT_FD_VAL (-2)
#define QEMU_PROT_VERSION (0x01)

typedef enum client_status {
    CREATED = 1,
    INITIALIZED,
    MEM_INITIALIZED,
    DEVICE_CONNECTED,
    CLEANING,
    CLEAR,
    UNKNOWN_ERROR,
    LAST
} client_status;

typedef struct {
    char socket_path[PATH_MAX];
    int socket;
    char shm_mem_path[PATH_MAX];
    int sh_mem_fds[VHOST_MEMORY_MAX_NREGIONS];
    client_status status;
    fd_rw_t fd_rw_list;
} Client;

typedef enum {
    E_CLIENT_OK = EXIT_SUCCESS,
    E_CLIENT_ERR,
    E_CLIENT_ERR_ALLOC,
    E_CLIENT_ERR_UNK,
    E_CLIENT_ERR_FARG,
    E_CLIENT_ERR_SOCK,
    E_CLIENT_ERR_CONN,
    E_CLIENT_ERR_IOCTL_SEND,
    E_CLIENT_ERR_IOCTL_REPLY,
    E_CLIENT_ERR_VIOCTL,
    E_CLIENT_VIOCTL_REPLY,
    E_CLIENT_LAST
} CLIENT_H_RET_VAL;

int client_disconnect_socket(Client *client);
int client_connect_socket(Client *client);
int client_init_socket(Client *client);
int client_init_Client(Client *client, const char *path);
int client_init_path(Client *client, const char *path);
int client_vhost_ioctl(Client *client, VhostUserRequest request, void *req_ptr);
int client_vhost_ioctl_set_send_msg(Client *client, VhostUserRequest request,
                         void *req_ptr, VhostUserMsg *msg, int *fd, size_t *fd_num);
int client_vhost_ioctl_set_recv_msg(VhostUserRequest request, void *req_ptr, VhostUserMsg *msg);
int client_vhost_ioctl_recv_fds_handler(struct cmsghdr *cmsgh, int *fds, size_t *fd_num);
int client_vhost_ioctl_send_fds(VhostUserMsg *msg, int fd, int *fds, size_t fd_num);
int client_vhost_ioctl_recv_fds(int fd, VhostUserMsg *msg, int *fds, size_t *fd_num);
#endif

