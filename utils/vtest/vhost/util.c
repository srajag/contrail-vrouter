/*
 * util.c
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/select.h>
#include "util.h"

//todo select()

int
utils_init_fd_rw_t(fd_rw_t *fd_rw_list, struct timeval tv) {

    fd_rw_t *const l_fd_rw_list = fd_rw_list;

    if (!fd_rw_list) {
        return E_UTILS_ERR_FARG;
    }

    FD_ZERO(&(fd_rw_list->rwfd_set[FD_TYPE_READ]));
    FD_ZERO(&(fd_rw_list->rwfd_set[FD_TYPE_WRITE]));

    l_fd_rw_list->rwfdmax = -2;
    l_fd_rw_list->tv = tv;

    for (size_t j = 0; j  < FD_TYPE_MAX; j++ ) {
        for (size_t i = 0; i < FD_LIST_SIZE; i++) {
            l_fd_rw_list->rwfds[j][i].fd = -2;
            l_fd_rw_list->rwfds[j][i].fd_arg = NULL;
            l_fd_rw_list->rwfds[j][i].fd_handler = 0;
        }
    }

    return E_UTILS_OK;
}

int
utils_add_fd_to_fd_rw_t(fd_rw_t *fd_rw_list, fd_type fd_type, int fd,
                         void* fd_handler_arg, fd_handler fd_handler) {

    fd_rw_t *const l_fd_rw_list = fd_rw_list;
    fd_rw_element *l_fd_rw_element = NULL;

    if (!fd_rw_list) {
        return E_UTILS_ERR_FARG;
    }

    if ((fd_type == FD_TYPE_READ) || (fd_type == FD_TYPE_WRITE)) {

        l_fd_rw_element = l_fd_rw_list->rwfds[FD_TYPE_READ];

    } else {

        return E_UTILS_ERR_FARG;
    }

    for (size_t i = 0; i < FD_LIST_SIZE; i++) {

        if (!(l_fd_rw_element->fd < 0)) {
            continue;
        }

        l_fd_rw_element->fd = fd;
        l_fd_rw_element->fd_arg = fd_handler_arg;
        l_fd_rw_element->fd_handler = fd_handler;

        if (l_fd_rw_list->rwfdmax < fd) {
            l_fd_rw_list->rwfdmax = fd;
        }
        return E_UTILS_OK;
    }

    return E_UTILS_ERR_FD_ADD;
}

