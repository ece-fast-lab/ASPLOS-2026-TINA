#!/bin/bash

## BURST SWEEP

## listen to ctrl-c for clean exit
trap 'echo RECIEVED CTRL-C;exit' INT

TRACES=($(ls -d resources/traces/* | grep -e cache -e hadoop -e web))
echo "Traces: ${TRACES[@]}"

EXPERIMENTS=($(printf '%s\n' "${TRACES[@]}" | cut -d'/' -f3 | cut -d'.' -f1))
echo "Experiments: ${EXPERIMENTS[@]}"

TIMEOUT=60
DPDK_TX=tina-stack/tx/dpdk-tx
DATA_DIR=DATA/APPS/$1
mkdir -p $DATA_DIR
EXP_LABEL=$2

mkdir -p $DATA_DIR

## loop through all the experiments
for i in ${!EXPERIMENTS[@]}; do

    ## INIT FPGA
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x14 w 1758291
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x18 w 597152
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x1C w 6788608
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x20 w 100

    case $2 in
        "TouchFwd")
            sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x10 w 6250
            ;;
        "KVS")
            sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x10 w 4400
            ;;
        "NAT")
            sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x10 w 20000
            ;;
        "RSA")
            sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x10 w 2450
            ;;
    esac

    echo "Experiment: ${EXPERIMENTS[$i]}"
    TRACE_FILE=${TRACES[$i]}
    OUTFILE="$DATA_DIR/$EXP_LABEL-${EXPERIMENTS[$i]}.out"
    OUTFILE_SAMPLE="$DATA_DIR/$EXP_LABEL-${EXPERIMENTS[$i]}-sample.out"
    sudo timeout $TIMEOUT $DPDK_TX -l 0-2 -a 0000:03:00.0 -- --source-mac=00:0a:35:06:aa:f9 --source-ip=192.168.151.21 --dest-mac=b8:3f:d2:59:5f:e1 --dest-ip=192.168.151.10 --size=1024 -i 100000 -f $TRACE_FILE -O $OUTFILE -R $OUTFILE_SAMPLE
    echo "______ Experiment for $1-$2, trace ${EXPERIMENTS[$i]} done ______ \n"
done
