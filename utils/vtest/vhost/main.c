#include <stdio.h>
#include <stdlib.h>

#include "uvhost.h"
#include "util.h"
#include <string.h>

int main (int argc, char **argv) {

// init client
// send or receive
//
    Vhost_Client *vhost_client_send = NULL;
    Vhost_Client *vhost_client_recv = NULL;

    uvhost_run_vhost_client(&vhost_client_send, "/var/run/vrouter/uvh_vif_vm1", CLIENT_TYPE_TX);
    uvhost_run_vhost_client(&vhost_client_recv, "/var/run/vrouter/uvh_vif_vm2", CLIENT_TYPE_RX);


    char data_send_buf[50] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02,
       0xCC, 0x00, 0x1D, 0x09, 0xF0 ,0x92 ,0xAB ,0x08 ,0x06 ,0x00 ,0x01 ,0x08 ,0x00};
    uint64_t data_dest_buf = 0;

    size_t data_len_send = sizeof(data_send_buf);
    size_t data_len_recv = 0;

    unsigned int decrementer = 100000;

    while(decrementer--) {
        data_len_send = ((data_len_send + decrementer) % 30) + 20;
        vhost_client_send->client.vhost_net_app_handler.poll_func_handler(vhost_client_send->client.vhost_net_app_handler.context, &data_send_buf, &data_len_send);
        vhost_client_recv->client.vhost_net_app_handler.poll_func_handler(vhost_client_recv->client.vhost_net_app_handler.context, &data_dest_buf,&data_len_recv);
        printf("data_dest_buf: %zu data_len_recv: %zu \n", data_dest_buf, data_len_recv);
    }

    uvhost_delete_Vhost_Client(vhost_client_recv);
    uvhost_delete_Vhost_Client(vhost_client_send);

    return EXIT_SUCCESS;
}
