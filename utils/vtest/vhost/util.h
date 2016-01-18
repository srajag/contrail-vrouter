/*
 * util.h
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#ifndef UTIL_H
#define UTIL_H

#include <sys/time.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>


#define VHOST_USER_HDR_SIZE (sizeof(struct virtio_net_hdr))
#define VHOST_MEMORY_MAX_NREGIONS    8

#define FD_LIST_SIZE (16)
typedef int (*fd_handler)(int fd, void *arg);

typedef enum {
    FD_TYPE_READ = 0,
    FD_TYPE_WRITE,
    FD_TYPE_MAX
} fd_type;

typedef struct {
    int fd;
    void *fd_arg;
    fd_handler fd_handler;
} fd_rw_element;

typedef struct {
   fd_rw_element rwfds[FD_TYPE_MAX][FD_LIST_SIZE];
   fd_set rwfd_set[FD_TYPE_MAX];
   /* For select() purpose. */
   int rwfdmax;
   struct timeval tv;
} fd_rw_t;

typedef int (*AvailHandler)(void* context, void* buf, size_t size);
typedef int (*MapHandler)(void* context, uint64_t addr);

struct ProcessHandler {
    void* context;
    AvailHandler avail_handler;
    MapHandler  map_handler;
};

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
    char sh_mem_path[PATH_MAX];
    int sh_mem_fds[VHOST_MEMORY_MAX_NREGIONS];
    client_status status;
    fd_rw_t fd_rw_list;
} Client;


typedef enum {
    E_UTILS_OK = EXIT_SUCCESS,
    E_UTILS_ERR_ALLOC,
    E_UTILS_ERR_UNK,
    E_UTILS_ERR_FARG,
    E_UTILS_ERR,
    E_UTILS_ERR_FD_ADD,
    E_UTILS_LAST
} UTILS_H_RET_VAL;

int utils_init_fd_rw_t(fd_rw_t *fd_rw_list, struct timeval tv);
int utils_add_fd_to_fd_rw_t(fd_rw_t *fd_rw_list, fd_type fd_type, int fd,
                         void* fd_handler_arg, fd_handler fd_handler);
#endif

