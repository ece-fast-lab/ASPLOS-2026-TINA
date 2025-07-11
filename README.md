# ASPLOS-2026-TINA

This repository contains the code, and scripts to reproduce the key results from "TiNA: Tiered Network Buffer Architecture for Fast Networking in Chiplet-based CPUs"

# Directory Structure
- `flows/`: Contains scripts for generating plots and analyzing data.
- `data/`: Generated raw data will be stored here.
- `plots/`: Generated plots will be stored here.
- `resources/`: Contains inputs like network traffic traces
- `tina-hw/`: Contains the hardware design files for the TiNA architecture.
- `tina-sw/`: Contains the software components for the TiNA stack and the benchmarks.

# Requirements
- Python 3.8 or higher
- Required Python packages can be installed using the `requirements.txt` file.
- DPDK Version 22.07
- Xilinx Vivado for Synthesizing and Programming the FPGA
- MLNX OFED for Mellanox NICs
- linux-tools for cpupower for locking the CPU frequency

# Running the Code
All experiments need to be run with SNC on and off. Since this needs to be done manuall and needs a reboot, we recommend first collecting the data with SNC off (Mostly baseline numbers), and then collecting the data with SNC on (Baseline and TiNA numbers). While the directions here are grouped by each experiment, the data collection should be done in two passes. (First run the SNC off portion of ALL experiments, and then run the SNC on portion of ALL experiments).

## Figure 3: Memory Bandwidth and Latency
To generate Figure 3, run the following command from the root of the repository:

### SNC Off
```bash
sudo flows/memory-experiments.sh NOSNC LAT
sudo flows/memory-experiments.sh NOSNC BW
```

### SNC On
```bash
sudo flows/memory-experiments.sh SNC LAT
sudo flows/memory-experiments.sh SNC BW
```

After running the above commands, you can generate the plot by running:

```bash
python flows/memory-experiments-plot.py
```

This will generate a plot in the `plots/` directory showing the memory bandwidth and latency for both SNC and Non-SNC configurations.


## Figure 4: SNC vs Non-SNC Network Latency