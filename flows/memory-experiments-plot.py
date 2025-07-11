import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import argparse
import re
# import scienceplots

plt.style.use(['classic'])

plt.rc('axes', titlesize=20)     # Font size for axes titles
plt.rc('axes', labelsize=20, labelweight='bold')     # Font size for x and y labels
plt.rc('xtick', labelsize=17)    # Font size for x tick labels
plt.rc('ytick', labelsize=17)    # Font size for y tick labels
plt.rc('legend', fontsize=15)    # Font size for legend
plt.rc('legend',  numpoints=1)  # Number of points in legend line
plt.rc('grid', color='gray', linewidth=0.25)  # Customize grid lines
## set the font to libertine
plt.rc('font', family='Linux Libertine O', weight='bold')

def parse_mlc_latency_log(file_path):
    buffer_sizes = []
    latencies = []
    
    with open(file_path, 'r') as f:
        content = f.read()
    
    # Split into individual test runs
    test_runs = content.split('Intel(R) Memory Latency Checker - v3.11b')[1:]
    
    for run in test_runs:
        # Extract buffer size
        buffer_match = re.search(r'Using buffer size of ([\d.]+)MiB', run)
        if buffer_match:
            buffer_size = float(buffer_match.group(1))
            buffer_sizes.append(buffer_size)
            
            # Extract latency (ns value in parentheses)
            latency_match = re.search(r'\(\s*([\d.]+)\s*ns\)', run)
            if latency_match:
                latency = float(latency_match.group(1))
                latencies.append(latency)
            else:
                latencies.append(None)
    
    return pd.DataFrame({
        'Working Set Size (MB)': buffer_sizes,
        'Latency (ns)': latencies
    })

def parse_mlc_bandwidth_log(file_path):
    latencies = []
    bandwidths = []
    
    with open(file_path, 'r') as f:
        content = f.read()
    
    # Split into individual test runs
    test_runs = content.split('Intel(R) Memory Latency Checker - v3.11b')[1:]
    
    for run in test_runs:
        lines = run.split('\n')
        for line in lines:
            # Match lines with delay, latency, and bandwidth
            if re.match(r'\s*\d+\s+[\d.]+\s+[\d.]+', line.strip()):
                parts = line.strip().split()
                if len(parts) >= 3:
                    try:
                        latency = float(parts[1])
                        bandwidth = float(parts[2])
                        latencies.append(latency)
                        bandwidths.append(bandwidth)
                    except ValueError:
                        continue
    
    return pd.DataFrame({
        'Latency (ns)': latencies,
        'Bandwidth (MB/s)': bandwidths
    })

# Parse command-line arguments
parser = argparse.ArgumentParser(description="Plot latency data from log files.")
parser.add_argument("data_dir", type=str, help="Directory containing the memory experiment log files")
args = parser.parse_args()

import os
from scipy.interpolate import interp1d
import numpy as np

# Define expected file names
expected_files = {
    'nosnc_lat': 'NOSNC_mlc_exp_lat.log',
    'nosnc_bw': 'NOSNC_mlc_exp_bw.log', 
    'snc_lat': 'SNC_mlc_exp_lat.log',
    'snc_bw': 'SNC_mlc_exp_bw.log'
}

# Check if all files exist
file_paths = {}
for key, filename in expected_files.items():
    file_path = os.path.join(args.data_dir, filename)
    if not os.path.exists(file_path):
        print(f"Error: Required file not found: {file_path}")
        exit(1)
    file_paths[key] = file_path

print(f"Found all required files in {args.data_dir}")

# Load data from log files
nosnc_lat_data = parse_mlc_latency_log(file_paths['nosnc_lat'])
nosnc_bw_data = parse_mlc_bandwidth_log(file_paths['nosnc_bw'])
snc_lat_data = parse_mlc_latency_log(file_paths['snc_lat'])
snc_bw_data = parse_mlc_bandwidth_log(file_paths['snc_bw'])

# Merge the latency data on working set size
wss_data = pd.merge(snc_lat_data, nosnc_lat_data, on='Working Set Size (MB)', suffixes=(' SNC', ' NoSNC'))

# For bandwidth data, convert to GB/s and create the expected format
membw_data = pd.DataFrame({
    'BW_SNC (GB/s)': snc_bw_data['Bandwidth (MB/s)'] / 1000,
    'Latency SNC (ns)': snc_bw_data['Latency (ns)'],
    'BW_NoSNC (GB/s)': nosnc_bw_data['Bandwidth (MB/s)'] / 1000,
    'Latency No-SNC (ns)': nosnc_bw_data['Latency (ns)']
})


colors = {'SNC': '#75bbfd', 'NOSNC': '#040273', 'TiNA': '#93B100'}

# Create side-by-side plots with a shared Y-axis for latency
fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(8, 3.5), sharey=True)

# Plot Working Set Size vs Latency
ax1.plot(wss_data["Working Set Size (MB)"], wss_data["Latency (ns) SNC"], label="SNC", marker='s', linewidth=4, color=colors["SNC"], markersize=8)
ax1.plot(wss_data["Working Set Size (MB)"], wss_data["Latency (ns) NoSNC"], label="non-SNC", marker='o', linewidth=4, color=colors["NOSNC"], markersize=8)
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
num_markers = 5
x_snc = np.linspace(membw_data["BW_SNC (GB/s)"].min(), membw_data["BW_SNC (GB/s)"].max(), num_markers)
x_nosnc = np.linspace(membw_data["BW_NoSNC (GB/s)"].min(), membw_data["BW_NoSNC (GB/s)"].max(), num_markers)

# Interpolate latency values at these x-values
f_snc = interp1d(membw_data["BW_SNC (GB/s)"], membw_data["Latency SNC (ns)"], kind='linear', fill_value="extrapolate")
f_nosnc = interp1d(membw_data["BW_NoSNC (GB/s)"], membw_data["Latency No-SNC (ns)"], kind='linear', fill_value="extrapolate")

# Plot the full lines
ax2.plot(membw_data["BW_SNC (GB/s)"], membw_data["Latency SNC (ns)"], label="SNC", linewidth=4, color=colors["SNC"])
ax2.plot(membw_data["BW_NoSNC (GB/s)"], membw_data["Latency No-SNC (ns)"], label="Non-SNC", linewidth=4, color=colors["NOSNC"])

ax2.plot(x_snc, f_snc(x_snc), marker='s', linestyle='None', color=colors["SNC"], markersize=8)
ax2.plot(x_nosnc, f_nosnc(x_nosnc), marker='o', linestyle='None', color=colors["NOSNC"], markersize=8)
ax2.set_xlabel("Memory Bandwidth (GB/s)")
ax2.set_xlim(0,90)
ax2.set_axisbelow(True)
ax2.grid()
ax1.legend(loc='upper center', ncol=2, frameon=False)

plt.subplots_adjust(left=0.1, right=0.98, top=0.87, bottom=0.2, wspace=0.1, hspace=0.1)
## make folder plots 
if not os.path.exists("plots"):
    os.makedirs("plots")
plt.savefig("plots/fig3.pdf", dpi=600)
