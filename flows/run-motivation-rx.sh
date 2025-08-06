#!/bin/bash
flows/prep.sh
tina-stack/rx/dpdk-rx -l 3-5 -a 0000:18:00.1 -- -i 100 -l 100 -b 400 -y 1024
