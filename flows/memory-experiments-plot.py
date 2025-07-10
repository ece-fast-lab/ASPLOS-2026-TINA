import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import argparse
# import scienceplots

plt.style.use(['classic'])
# plt.style.use(['science'])
## add very ligy grey grid

plt.rc('axes', titlesize=20)     # Font size for axes titles
plt.rc('axes', labelsize=20, labelweight='bold')     # Font size for x and y labels
plt.rc('xtick', labelsize=17)    # Font size for x tick labels
plt.rc('ytick', labelsize=17)    # Font size for y tick labels
plt.rc('legend', fontsize=15)    # Font size for legend
plt.rc('legend',  numpoints=1)  # Number of points in legend line
plt.rc('grid', color='gray', linewidth=0.25)  # Customize grid lines
## set the font to libertine
plt.rc('font', family='Linux Libertine O', weight='bold')

# Parse command-line arguments
parser = argparse.ArgumentParser(description="Plot latency data from input files.")
parser.add_argument("wss_file", type=str, help="Path to the WSS vs latency file")
parser.add_argument("membw_file", type=str, help="Path to the memory bandwidth vs latency file")
args = parser.parse_args()
# Load WSS vs Latency data
wss_data = pd.read_csv(args.wss_file, sep='\t', skiprows=1, header=None)
wss_data.columns = ["Working Set Size (Bytes)", "Latency SNC (ns)", "Latency NoSNC (ns)"]
wss_data["Working Set Size (MB)"] = wss_data["Working Set Size (Bytes)"] / (1024 * 1024)

# Load Memory Bandwidth vs Latency data
membw_data = pd.read_csv(args.membw_file, sep='\t', skiprows=1, header=None)
membw_data.columns = ["BW_SNC (mbps)", "Latency SNC (ns)", "BW_NoSNC (mbps)", "Latency No-SNC (ns)"]
membw_data["BW_SNC (GB/s)"] = membw_data["BW_SNC (mbps)"].astype(float) / 1000
membw_data["BW_NoSNC (GB/s)"] = membw_data["BW_NoSNC (mbps)"].astype(float) / 1000


colors = {'SNC': '#75bbfd', 'NOSNC': '#040273', 'TiNA': '#93B100'}

# Create side-by-side plots with a shared Y-axis for latency
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8, 3.5), sharey=True)

# Plot Working Set Size vs Latency
ax1.plot(wss_data["Working Set Size (MB)"], wss_data["Latency SNC (ns)"], label="SNC", marker='s', linewidth=4, color=colors["SNC"], markersize=8)
ax1.plot(wss_data["Working Set Size (MB)"], wss_data["Latency NoSNC (ns)"], label="non-SNC", marker='o', linewidth=4, color=colors["NOSNC"], markersize=8)
ax1.set_xscale("log", base=2)
ax1.get_xaxis().set_major_formatter(plt.ScalarFormatter())
## show more ticks
ax1.set_xticks([2**i for i in range(0, 8)])
ax1.set_ylim([0, 150])
ax1.set_xlim([1, 180])

ax1.set_xlabel("Working Set Size (MB)")
ax1.set_ylabel("Latency (ns)")
ax1.set_yticks([0, 25, 50, 75, 100, 125, 150])
ax1.set_axisbelow(True)
ax1.grid()
# Plot Bandwidth vs Latency
ax2.plot(membw_data["BW_SNC (GB/s)"], membw_data["Latency SNC (ns)"], label="SNC", marker='s', linewidth=4, markevery=20, color=colors["SNC"], markersize=8)
ax2.plot(membw_data["BW_NoSNC (GB/s)"], membw_data["Latency No-SNC (ns)"], label="Non-SNC", marker='o', linewidth=4, markevery=20, color=colors["NOSNC"], markersize=8)
ax2.set_xlabel("Memory Bandwidth (GB/s)")
ax2.set_axisbelow(True)
ax2.grid()
ax1.legend(loc='upper center', ncol=2, frameon=False)
# ax2.legend(loc='upper center', ncol=2, frameon=False)
# Combine legend
# lines1, labels1 = ax1.get_legend_handles_labels()

# fig.legend(handles=lines1, labels=labels1,loc='center', ncol=2, bbox_to_anchor=(0.53, 0.93), frameon=False)
# plt.tight_layout(rect=[0, 0, 1, 1])
plt.subplots_adjust(left=0.1, right=0.98, top=0.87, bottom=0.2, wspace=0.1, hspace=0.1)
plt.savefig("memsweep.pdf", dpi=600)
