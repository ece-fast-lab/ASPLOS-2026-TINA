#!/bin/bash

## BURST SWEEP

## listen to ctrl-c for clean exit
trap 'echo RECIEVED CTRL-C;exit' INT

# Generate finer BURSTDURATIONS using seq
FINE_BURSTDURATIONS=$(seq 100 50 800)

# Target load
LOAD=0.01

# Arrays to store BURSTDURATIONS and BURSTGAPS
BURSTDURATIONS=()
BURSTGAPS=()

# Calculate corresponding BURSTGAPS and store them
for DUR in $FINE_BURSTDURATIONS; do
  # Calculate the burst gap to maintain the desired load
  GAP=$(echo "scale=2; ($DUR / $LOAD) - $DUR" | bc)
  
  # Store values in arrays
  BURSTDURATIONS+=("$DUR")
  BURSTGAPS+=("$GAP")
done

# Print the arrays
echo "BURSTDURATIONS: ${BURSTDURATIONS[@]}"
echo "BURSTGAPS: ${BURSTGAPS[@]}"

TIMEOUT=20
DPDK_TX=/home/sa10/dpdk-framework/dpdk-tx/dpdk-tx
DATA_DIR=DATA/BURST_SWEEP
EXP_LABEL=$1

mkdir -p $DATA_DIR
rx_pcie_monitor_command='sudo pcm-pcie -e -B -csv 0.1'
rx_pcie_monitor_command="$SSH_RX_COMMAND_PREFIX $rx_pcie_monitor_command $SSH_RX_COMMAND_SUFFIX"

for i in "${!BURSTDURATIONS[@]}"; do
    BURST=${BURSTDURATIONS[i]}
    GAP=${BURSTGAPS[i]}
    OUTFILE="$DATA_DIR/$EXP_LABEL-$BURST.latency"
    OUTFILE_SAMPLE="$DATA_DIR/$EXP_LABEL-$BURST.sampling"
    OUTFILE_PCIE="$DATA_DIR/$EXP_LABEL-$BURST.pcie"


    ## INIT FPGA
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x10 w 7250
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x14 w 1558291
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x18 w 1097152
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x1C w 5788608
    sudo externals/pcimem/pcimem /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0 0x20 w 100

    ###### Actual TX
    sudo timeout $TIMEOUT $DPDK_TX -l 0-2 -a 0000:03:00.0 -- --source-mac=00:0a:35:06:aa:f9 --source-ip=192.168.151.21 --dest-mac=b8:3f:d2:59:5f:e1 --dest-ip=192.168.151.10 --size=1024 -i 100000 -d "$BURST" -g "$GAP" -O $OUTFILE -R $OUTFILE_SAMPLE
    
    # ###### Kill PCIE Monitor
    # sudo kill -9 $RX_PCIE_MONITOR_PID 2> /dev/null

    echo "______ Experiment for $BURST, iteration $i done ______ \n"
done
