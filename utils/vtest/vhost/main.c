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


    char data[50] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x02,
       0xCC, 0x00, 0x1D, 0x09, 0xF0 ,0x92 ,0xAB ,0x08 ,0x06 ,0x00 ,0x01 ,0x08 ,0x00};

    size_t data_len = sizeof(data);
    size_t data_len_recv = 1500;
    unsigned int incrementer = 1000000;
    while(incrementer) {
        data_len = ((data_len + incrementer) % 30) + 20;
        incrementer++;
        vhost_client_send->client.vhost_net_app_handler.poll_func_handler(vhost_client_send->client.vhost_net_app_handler.context, &data, &data_len);
        vhost_client_recv->client.vhost_net_app_handler.poll_func_handler(vhost_client_recv->client.vhost_net_app_handler.context, &data,&data_len_recv);        
    }

    return EXIT_SUCCESS;
}
