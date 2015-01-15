#!/bin/bash
##
## Start Monitoring Script
##

. 00.config.sh

#################################################################
## Add Interfaces
#sudo ${VIF} --add vm1 --mac ${VM1_MAC} --type virtual --vrf 0 --id 0
sudo ${VIF} --add ${VROUTER1_1_PCI_DBDF} --mac ${VROUTER1_1_MAC} \
    --type physical --vrf 0 --id 1 --pci
sudo ${VIF} --add ${VROUTER1_MON0} --type monitoring --vif 1 --id 2
sudo ${VIF} --add ${VROUTER1_MON1} --type monitoring --vif 1 --id 3
sudo ${VIF} --list

## Configure interfaces up
sudo ifconfig ${VROUTER1_MON0} up
sudo ifconfig ${VROUTER1_MON1} up

## Start tcpdump
sudo tcpdump -i ${VROUTER1_MON0} -n
