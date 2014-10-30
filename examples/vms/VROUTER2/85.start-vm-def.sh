#!/bin/bash
## TODO: unfinished
QEMU_DIR=~/qemu/qemu-2.1.0/build/native

${QEMU_DIR}/x86_64-softmmu/qemu-system-x86_64 -cpu host -smp 4 \
    -m 1024 -enable-kvm -drive if=virtio,file=vm.qcow2,cache=none \
