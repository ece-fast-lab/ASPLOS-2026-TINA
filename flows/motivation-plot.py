#!/usr/bin/env python3
import os
import glob
import pandas as pd
import matplotlib.pyplot as plt
import argparse
import sys
import numpy as np
import statistics
from matplotlib.patches import Patch
import matplotlib
import matplotlib.patches as mpatches
from mpl_toolkits.axes_grid1.inset_locator import inset_axes, mark_inset

DEFAULT_COLORS = {'SNC': '#75bbfd', 'non-SNC': '#040273', 'TiNA': '#93B100'}
HATCH_STYLES= {'SNC': '', 'non-SNC': '', 'TiNA': ''}
figure_size_global = (8 , 4)
TRACES= ['Web', 'Cache', 'Hadoop']
APPS=['L2TouchFwd', 'KVS', 'NAT', 'RSA']

def configure_plot_style():
    plt.style.use(['classic'])
    # plt.style.use(['science'])
    ## add very ligy grey grid

    plt.rc('axes', titlesize=20)     # Font size for axes titles
    plt.rc('axes', labelsize=20, labelweight='bold')     # Font size for x and y labels
    plt.rc('xtick', labelsize=18)    # Font size for x tick labels
    plt.rc('ytick', labelsize=18)    # Font size for y tick labels
    plt.rc('legend', fontsize=18)    # Font size for legend
    plt.rc('legend',  numpoints=1)  # Number of points in legend line
    plt.rc('grid', color='gray', linewidth=0.25)  # Customize grid lines
    ## set the font to libertine
    plt.rc('font', family='Linux Libertine O', weight='bold')
    matplotlib.rcParams['text.latex.preamble']=r"\usepackage{amsmath} \boldmath"


def parse_filename(file_path):
    basename = os.path.basename(file_path)
    parts = basename.split('-')
    burst_size = parts[1].split('.')[0]
    if ("trace" in burst_size):
        burst_size = burst_size.split('_')[1]
    try:
        burst_size = int(burst_size)
    except ValueError:
        pass
        # print(f"Invalid burst size in filename: {basename}")

    if 'NOSNC' in parts:
        category = 'non-SNC'
    elif 'TINA' in parts:
        category = 'TiNA'
    elif 'SNC' in parts:
        category = 'SNC'
    else:
        category = 'Unknown'
    return burst_size, category

def read_latency_files(folder_path, extension=".latency"):
    latency_files = glob.glob(os.path.join(folder_path, f'*{extension}'))
    latency_files = [file for file in latency_files if 'sample' not in file]
    data = []
    for file in latency_files:
        try:
            burst_size, category = parse_filename(file)
        except ValueError as e:
            print(f"Skipping file {file}: {e}")
            continue
        # print(f"Processing file: {file}")
        latencies = np.loadtxt(file)
        latencies = latencies/1000
        latencies = [lat for lat in latencies if lat < 10000]
        for latency in latencies:
            data.append((burst_size, category, latency))
    df = pd.DataFrame(data, columns=['Burst Size', 'Category', 'Latency'])

    return df

def process_file(file):
    try:
        burst_size, category = parse_filename(file)
    except ValueError as e:
        print(f"Skipping file {file}: {e}")
        return None
    print(f"Processing file: {file}")
    latencies = np.loadtxt(file)
    latencies = latencies / 1000
    p99 = np.percentile(latencies, 99)
    return (burst_size, category, p99)   

def read_load_latency(folder_path, extension=".out"):
    latency_files = glob.glob(os.path.join(folder_path, f'*{extension}'))
    latency_files = [file for file in latency_files if 'sample' not in file]
    data = []
    import concurrent.futures

    data = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=16) as executor:
        results = executor.map(process_file, latency_files)
        for result in results:
            if result is not None:
                data.append(result)
    df = pd.DataFrame(data, columns=['Load', 'Category', 'Latency'])
    print(df)
    return df 

def plot_load_latnecy(df, output_file="load_latency.pdf", colors=DEFAULT_COLORS):
    plt.figure(figsize=figure_size_global, dpi=600)
    loads = sorted(df['Load'].unique())
    categories = sorted(df['Category'].unique())
    categories = ['SNC','non-SNC', 'TiNA']
    marker = 's'
    ## for each category plot a line
    for category in categories:
        subset = df[df['Category'] == category]
        ## sort by load
        subset = subset.sort_values(by='Load')
        ## for SNC, plot upto 37
        if category == 'SNC':
            subset = subset[subset['Load'] <= 40]
            marker = 's'
        if category == 'non-SNC':
            subset = subset[subset['Load'] <= 44]
            marker = 'o'
        if category == 'TiNA':
            subset = subset[subset['Load'] <= 44]
            marker = 'v'
        plt.plot(subset['Load'], subset['Latency'], label=category, color=colors.get(category, 'gray'), marker=marker, linewidth=4, markersize=9)

    plt.yscale('log')
    plt.xlabel('Load (Gbps)')
    plt.ylabel('Latency ($\mu$s)')
    plt.grid()
    ## add legend
    plt.legend(ncols=1, loc='upper left', frameon=False, )
    plt.xlim(30, 45)
    ## break x axis at 5

    plt.subplots_adjust(left=0.1, right=0.98, top=0.95, bottom=0.15, wspace=0.1, hspace=0.1)
    plt.savefig(output_file, dpi=600)
    plt.close()

def plot_latency_boxplot(df, output_file="latency_spread_boxplot-motivation.pdf", colors=DEFAULT_COLORS, box_gap=0.25, width=0.1, line_width=1, categories = ['SNC','non-SNC', 'TiNA']):
    plt.figure(figsize=figure_size_global, dpi=600)
    burst_sizes = sorted(df['Burst Size'].unique())
    ## set order of categories like this SNC, non-SNC, TiNA

    for i, burst_size in enumerate(burst_sizes):
        for j, category in enumerate(categories):
            subset = df[(df['Burst Size'] == burst_size) & (df['Category'] == category)]
            if subset.empty:
                continue
            min_val = subset['Latency'].quantile(0.25)
            q50 = subset['Latency'].quantile(0.50)
            q90 = subset['Latency'].quantile(0.90)
            q99 = subset['Latency'].quantile(0.99)
            q999 = subset['Latency'].quantile(0.999)
            num_categories = len(categories)
            offset = -box_gap * (num_categories - 1) / 2 + j * box_gap
            x_pos = i + offset
            plt.plot([x_pos, x_pos], [min_val, q50], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos, x_pos], [q99, q999], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos - width, x_pos + width], [min_val, min_val], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos - width, x_pos + width], [q50, q50], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos - width, x_pos + width], [q90, q90], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos - width, x_pos + width], [q99, q99], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos - width, x_pos + width], [q999, q999], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos - width, x_pos - width], [q50, q99], color="black", linestyle='-', linewidth=line_width)
            plt.plot([x_pos + width, x_pos + width], [q50, q99], color="black", linestyle='-', linewidth=line_width)
            inset = 0.005
            plt.fill_between([x_pos - width + inset, x_pos + width - inset], q90 + inset, q99 - inset, color=colors.get(category), hatch=HATCH_STYLES.get(category), edgecolor="black", alpha=1)
            plt.fill_between([x_pos - width + inset, x_pos + width - inset], q50 + inset, q99 - inset, color=colors.get(category), hatch=HATCH_STYLES.get(category), edgecolor="black", alpha=1)
    plt.xlabel('Burst Length ($\mu$s)')
    plt.ylabel('Latency ($\mu$s)')
    tick_positions = range(len(burst_sizes))
    tick_labels = [burst_sizes[i] if i % 2 == 0 else '' for i in range(len(burst_sizes))]
    plt.xticks(tick_positions, tick_labels)
    plt.xlim(-1, len(burst_sizes))
    plt.ylim(0, 1190)
    # plt.yscale('log')
    handles = []
    for cat in categories:
        if cat in colors:
            hatch = HATCH_STYLES.get(cat, '')
            handles.append(mpatches.Patch(facecolor=colors[cat], edgecolor="black", hatch=hatch, label=cat))
    plt.legend(handles=handles, ncols=len(handles), loc='upper center', frameon=False, bbox_to_anchor=(0.5, 1.05))

    plt.subplots_adjust(left=0.115, right=0.98, top=0.95, bottom=0.16, wspace=0.1, hspace=0.1)
    plt.grid(axis='y')

    ###### Generate Zoom In inset
    axins = plt.gca().inset_axes([0.1, 0.32, 0.35, 0.55])
    axins.grid(axis='y')
    for i, burst_size in enumerate(burst_sizes):
        for j, category in enumerate(categories):
            subset = df[(df['Burst Size'] == burst_size) & (df['Category'] == category)]
            if subset.empty:
                print(f"Empty subset for {burst_size} and {category}")
                continue
            min_val = subset['Latency'].quantile(0.25)
            q50 = subset['Latency'].quantile(0.50)
            q90 = subset['Latency'].quantile(0.90)
            q99 = subset['Latency'].quantile(0.99)
            q999 = subset['Latency'].quantile(0.999)
            num_categories = len(categories)
            offset = -box_gap * (num_categories - 1) / 2 + j * box_gap
            x_pos = i + offset
            axins.plot([x_pos, x_pos], [min_val, q50], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos, x_pos], [q99, q999], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos - width, x_pos + width], [min_val, min_val], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos - width, x_pos + width], [q50, q50], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos - width, x_pos + width], [q90, q90], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos - width, x_pos + width], [q99, q99], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos - width, x_pos + width], [q999, q999], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos - width, x_pos - width], [q50, q99], color="black", linestyle='-', linewidth=line_width)
            axins.plot([x_pos + width, x_pos + width], [q50, q99], color="black", linestyle='-', linewidth=line_width)
            inset = 0.005
            axins.fill_between([x_pos - width + inset, x_pos + width - inset], q90 + inset, q99 - inset, color=DEFAULT_COLORS.get(category),  hatch=HATCH_STYLES.get(category), edgecolor="black", alpha=1)
            axins.fill_between([x_pos - width + inset, x_pos + width - inset], q50 + inset, q99 - inset, color=DEFAULT_COLORS.get(category),  hatch=HATCH_STYLES.get(category), edgecolor="black", alpha=1)
    axins.set_xticks([])
    axins.set_yticks([0, 50, 100, 150, 200], ['0', '50', '100', '150', '200'])
    # axins.yaxis.tick_right()
    axins.set_xlim(-0.4, 4.5)
    axins.set_ylim(10, 225)
    box, c1, c2 = mark_inset(plt.gca(), axins, loc1=2, loc2=4, fc="none", ec="0.5")
    # plt.setp([c1,c2], linestyle=":", linewidth=2)
    # plt.setp(box, linewidth=2, linestyle=":")

    plt.savefig(output_file, dpi=600)
    plt.close()



def plot_latency_curve_trace(df, output_file, label="", color="darkgreen", bar_width=25, ylim=(-0.55, 0.55), 
        yticks = [-0.5, -0.3, -0.1  ,0.1, 0.3, 0.5], ytick_label = ['50%', '70%', '90%', '110%', '130%', '150%']):
    fig, ax = plt.subplots(1, 2, figsize=(8,4), dpi=200)
    burst_sizes = sorted(df['Burst Size'].unique())
    print("BURST SIZES")
    print(burst_sizes)
    print(df)
    diff_data = []
    diff_data_nosnc = []
    for burst_size in burst_sizes:
        subset_snc = df[(df['Burst Size'] == burst_size) & (df['Category'] == 'SNC')]
        subset_nosnc = df[(df['Burst Size'] == burst_size) & (df['Category'] == 'non-SNC')]
        subset_TiNA = df[(df['Burst Size'] == burst_size) & (df['Category'] == 'TiNA')]
        if subset_snc.empty or subset_nosnc.empty:
            continue
        quantile_snc = subset_snc['Latency'].quantile(0.99)
        quantile_nosnc = subset_nosnc['Latency'].quantile(0.99)
        quantile_TiNA = subset_TiNA['Latency'].quantile(0.99)
        diff = (quantile_TiNA) / quantile_snc
        diff_nosnc = (quantile_TiNA) / quantile_nosnc
        diff_data.append((burst_size, diff))
        diff_data_nosnc.append((burst_size, diff_nosnc))
    diff_df = pd.DataFrame(diff_data, columns=['Burst Size', 'Latency Difference'])
    diff_df_nosnc = pd.DataFrame(diff_data_nosnc, columns=['Burst Size', 'Latency Difference'])
    offset = bar_width / 2
    
    # x_loc = np.arange(0, len(diff_df['Burst Size']) * 100, 100)
    # print(diff_df['Burst Size'])
    ## get all burst sizes
    ba = [b -0 for b in burst_sizes]
    bb = [b + 0 for b in burst_sizes]
    ax[0].bar(ba, diff_df['Latency Difference'], edgecolor='black', color=DEFAULT_COLORS['SNC'], hatch=HATCH_STYLES['SNC'], width=bar_width)
    ax[1].bar(bb, diff_df_nosnc['Latency Difference'], edgecolor='black', color=DEFAULT_COLORS['non-SNC'], hatch=HATCH_STYLES['non-SNC'], width=bar_width)
    # ax.set_xticks([0, 100, 200], diff_df['Burst Size'])
    ax[0].set_xlim(70, 830)
    ax[0].axhline(1, color='red', linestyle='--', linewidth=2)
    ax[1].set_xlim(70, 830)
    ax[1].axhline(1, color='red', linestyle='--', linewidth=2)
    # ax.set_title(label)
    # ax.set_ylim(*ylim)
    ax[0].grid(axis='y')
    ax[1].grid(axis='y')
    # ax.set_yticks(yticks, ytick_label)
    
    ax[0].set_xlabel('Burst Length ($\mu$s)')
    ax[1].set_xlabel('Burst Length ($\mu$s)')
    ax[0].set_ylabel('Norm. p99 Latency')
    ax[0].set_xticks(range(200, 801, 200))
    ax[1].set_xticks(range(200, 801, 200))
    handles = []
    handles.append(mpatches.Patch(edgecolor="black", facecolor=DEFAULT_COLORS['SNC'], hatch=HATCH_STYLES['SNC'], label="TiNA/SNC"))
    ax[0].legend(handles=handles, ncols=2, loc='upper center', frameon=False, bbox_to_anchor=(0.5, 1.05))
    handles = []
    handles.append(mpatches.Patch(edgecolor="black", facecolor=DEFAULT_COLORS['non-SNC'], hatch=HATCH_STYLES['non-SNC'], label="TiNA/non-SNC"))
    ax[1].legend(handles=handles, ncols=1, frameon=False, bbox_to_anchor=(.99, 1.05))
    # fig.legend(handles=handles, ncols=2, loc='upper center', bbox_to_anchor=(0.55, 0.97), frameon=False)
    
    plt.tight_layout()
    
    
    plt.savefig(output_file, dpi=600)


def plot_latency_curve_motivation_all(df, ax, label="", color=DEFAULT_COLORS['SNC'], bar_width=25, ylim=(0, 1.5), idx=0):
    burst_sizes = sorted(df['Burst Size'].unique())
    diff_data = []
    for burst_size in burst_sizes:
        subset_snc = df[(df['Burst Size'] == burst_size) & (df['Category'] == 'SNC')]
        subset_nosnc = df[(df['Burst Size'] == burst_size) & (df['Category'] == 'non-SNC')]

        if subset_snc.empty or subset_nosnc.empty:
            continue
        quantile_snc = subset_snc['Latency'].quantile(0.99)
        quantile_nosnc = subset_nosnc['Latency'].quantile(0.99)

        diff = (quantile_snc) / quantile_nosnc
        diff_data.append((burst_size, diff))

    diff_df = pd.DataFrame(diff_data, columns=['Burst Size', 'Latency Difference'])

    offset = 0
    x = diff_df[diff_df['Latency Difference'] >= 1]['Burst Size'].min()
    # ax.axvspan(70, x, color=DEFAULT_COLORS['SNC'], alpha=0.5)
    # ax.axvspan(x, 830, color=DEFAULT_COLORS['non-SNC'], alpha=0.5)
    ax.bar(diff_df['Burst Size'] - offset, diff_df['Latency Difference'], color=color, width=bar_width)
    ## find x value for y value >= 1
    ## color plot left of x in light grey

    ax.set_xticks(range(200, 801, 200))
    ax.set_xticklabels([str(x) for x in range(200, 801, 200)])
    ax.set_xlim(70, 830)
    ax.set_ylim(*ylim)
    ax.axhline(1, color='darkred', linestyle='--', linewidth=2)
    ax.set_yticks([0, 0.25, 0.5, 0.75, 1, 1.25, 1.5], ['0', '', '0.5', '', '1', '', '1.5'])
    ax.grid(axis='y')
    ax.set_xlabel('Burst Length ($\mu$s)')
    if (idx == 0):
        ax.set_ylabel('Norm. p99 Latency')
    ## add text to the plot top left
    if idx == 0:
        ax.text(0.05, 0.95, "1.5 GHz", transform=ax.transAxes, fontsize=18, verticalalignment='top')
    else:
        ax.text(0.05, 0.95, "2.0 GHz", transform=ax.transAxes, fontsize=18, verticalalignment='top')





def plot_apps():
    dir_list = ["DATA/DYNAMIC", "DATA/KVS", "DATA/NAT", "DATA/RSA"]
    # dir_list = ["DATA/DYNAMIC"]
    hatchstyle = ['', '', '']
    alpha_vals = [1, 0.6, 0.3]
    linecolors = {
        "OverSNC": DEFAULT_COLORS['SNC'],
        "OverNoSNC": DEFAULT_COLORS['non-SNC']
    }
    bar_width = 20
    entry_seperation = 40
    single_seperation = [60, 80, 100]
    mid_point = statistics.median(single_seperation)    
    overall_seperation = entry_seperation + bar_width * len(single_seperation)

    
    xloc = [
        single_seperation,
        [x + overall_seperation for x in single_seperation],
        [x + overall_seperation*2 for x in single_seperation],
        [x + overall_seperation*3 for x in single_seperation]
    ]
    
    # plt.figure(figsize=figure_size_global, dpi=600)
    new_fig_size = (figure_size_global[0], figure_size_global[1] * 1.5)
    fig, ax = plt.subplots(2, 2, figsize=new_fig_size, dpi=600)
    for axidx, k in enumerate([.5,.99]):

        for i, directory in enumerate(dir_list):
            df_compare = read_latency_files(directory, extension=".out")
            if df_compare.empty:
                print(f"No valid latency data found in {directory}.")
                sys.exit(1)
    
            ##Draw
            assert(len(df_compare['Burst Size'].unique()) == 3)
            for z, traces in enumerate(["web_1x", "cache_1x", "hadoop_1x"]):
                snc = df_compare[(df_compare['Category'] == 'SNC') & (df_compare['Burst Size'] == traces)]
                nosnc = df_compare[(df_compare['Category'] == 'non-SNC') & (df_compare['Burst Size'] == traces)]
                TiNA = df_compare[(df_compare['Category'] == 'TiNA') & (df_compare['Burst Size'] == traces)]
                if snc.empty or nosnc.empty or TiNA.empty:
                    continue
                
                quantile_snc = snc['Latency'].quantile(k)
                quantile_nosnc = nosnc['Latency'].quantile(k)
                quantile_TiNA = TiNA['Latency'].quantile(k)
                # diff_snc = (quantile_TiNA - quantile_snc) / quantile_snc
                # diff_nosnc = (quantile_TiNA - quantile_nosnc) / quantile_nosnc
                
                TiNA_snc = quantile_TiNA / quantile_snc
                TiNA_nosnc = quantile_TiNA  / quantile_nosnc
                ## print burst size, TiNA_snc, TiNA_nosnc
                ## format to 2 decimal places
                # snc_a = round(snc['Latency'].quantile(.95), 2)
                # nosnc_a = round(nosnc['Latency'].quantile(.95), 2)
                # # TiNA_a = round(TiNA['Latency'].quantile(.95), 2)
                # print(traces, snc_a, nosnc_a, TiNA_a) 
                ax[axidx][0].bar([xloc[i][z]], [TiNA_snc], color=(DEFAULT_COLORS['SNC'],alpha_vals[z]) , width=bar_width, hatch=hatchstyle[z], edgecolor="black", linewidth=1)
                ax[axidx][1].bar([xloc[i][z]], [TiNA_nosnc], color=(DEFAULT_COLORS['non-SNC'],alpha_vals[z]), width=bar_width, hatch=hatchstyle[z], edgecolor="black", linewidth=1)
                
                
        ax[0][0].text(0.3, 0.95, "TiNA/SNC", transform=ax[0][0].transAxes, fontsize=18, verticalalignment='top')
        ax[0][1].text(0.25, 0.95, "TiNA/non-SNC", transform=ax[0][1].transAxes, fontsize=18, verticalalignment='top')

  
   
        
        for f in range(2):
            ax[axidx][f].axhline(1.0, color='red', linestyle='--', linewidth=2)
            ax[axidx][f].set_xticks([(mid_point + i*overall_seperation) for i in range(4)], ['L2', 'KVS', 'NAT', 'RSA'])
            ax[axidx][f].set_xlim(0, single_seperation[-1] + overall_seperation*3 + single_seperation[0])
            ax[axidx][f].grid(axis='y')

        ax[axidx][1].set_yticks([0, 0.25, 0.5, 0.75, 1, 1.25], ['', '', '', '', '', ''])
        ax[axidx][0].set_yticks([0, 0.25, 0.5, 0.75, 1, 1.25], ['0', '.25', '0.5', '.75', '1', '1.25'])

        if (k == .5):
            ax[axidx][0].set_ylabel('Norm. p50 Latency')
        else:
            ax[axidx][0].set_ylabel('Norm. p99 Latency')
   
   
    handles_traces = [[
        mpatches.Patch(color=(DEFAULT_COLORS['SNC'], alpha_vals[0]), label='T1'),
        mpatches.Patch(color=(DEFAULT_COLORS['SNC'], alpha_vals[1]), label='T2'),
        mpatches.Patch(color=(DEFAULT_COLORS['SNC'], alpha_vals[2]), label='T3'),
    ],[
        mpatches.Patch(color=(DEFAULT_COLORS['non-SNC'], alpha_vals[0]), label='T1'),
        mpatches.Patch(color=(DEFAULT_COLORS['non-SNC'], alpha_vals[1]), label='T2'),
        mpatches.Patch(color=(DEFAULT_COLORS['non-SNC'], alpha_vals[2]), label='T3'),
    ]]
    for f in range(2):
        ax[0][f].legend(handles=handles_traces[f], 
            ncols=3, 
            loc='lower center',
            borderpad=0.2,
            frameon=False,
            columnspacing=0.5,
            bbox_to_anchor=(0.5, 0.98)
        )
        
    plt.subplots_adjust(left=0.11, right=0.99, top=0.93, bottom=0.06, wspace=0.0, hspace=0.23)
    plt.savefig("app_latency_all.pdf", dpi=600)


def plot_app_cdf():
    #################################
    ## plot CDF for Dynamic
    #################################
    df_l2 = read_latency_files("DATA/DYNAMIC", extension=".out")
    df_kvs = read_latency_files("DATA/KVS", extension=".out")
    df_nat = read_latency_files("DATA/NAT", extension=".out")
    df_rsa = read_latency_files("DATA/RSA", extension=".out")
    dfs = [df_l2, df_kvs, df_nat, df_rsa]
    ## for each DF only keep hadoop burst size
    for i, df in enumerate(dfs):
        df = df[df['Burst Size'] == 'hadoop_1x']
        dfs[i] = df
    

    ylim1 = [
        [3800, 6000],
        [0, 0],   
        [0, 0],
        [7000, 10000]
    ]
    ylim2 = [
        [0, 1800],
        [0, 8000],
        [0, 20],
        [0, 4000]
    ]
    yticks1 = [
        [[4000, 6000], ['4000', '6000']],
        [[], []],
        [[], []],
        [[x for x in range(7500, 10001, 1000)], [str(x) for x in range(7500, 10001, 1000)]],
    ]
    yticks2 = [
        [[0, 500, 1000, 1500], ['0', '500', '1000', '1500']],
        [[x for x in range(0, 8000, 1500)], [str(x) for x in range(0, 8000, 1500)]],
        [[x for x in range(0, 20, 5)], [str(x) for x in range(0, 20, 5)]],
        [[x for x in range(0, 3500, 1500)], [str(x) for x in range(0, 3500, 1500)]],
    ]
    graph_ratio = [
        [1, 2.5],
        [1, 2.5],
        [1, 2.5],
        [2.5, 1]
    ]



    burst_sizes = sorted(df['Burst Size'].unique())
    print(burst_sizes)


    for z, df in enumerate(dfs):
        fig, ax = plt.subplots(1, 1, figsize=(6,4), dpi=600)
        snc = df[(df['Category'] == 'SNC')]
        nosnc = df[(df['Category'] == 'non-SNC')]
        TiNA = df[(df['Category'] == 'TiNA')]
        if snc.empty or nosnc.empty or TiNA.empty:
            continue
        ## plot CDF
        # Compute and plot the empirical CDF for each category in the current burst
        for category, color in DEFAULT_COLORS.items():
            if category not in ['SNC', 'non-SNC', 'TiNA']:
                continue
            # Get latency values for the category
            if category == 'SNC':
                latencies = snc['Latency'].values
            elif category == 'non-SNC':
                latencies = nosnc['Latency'].values
            elif category == 'TiNA':
                latencies = TiNA['Latency'].values
            if len(latencies) == 0:
                continue
            # Sort data and compute CDF
            sorted_latencies = np.sort(latencies)
            cdf = np.arange(1, len(sorted_latencies)+1) / float(len(sorted_latencies))
            ax.plot(sorted_latencies, cdf, color=color, lw=3.5, label=category)
        
        ## x axis log
        ax.set_xscale('log', base=10)
        ax.set_xlabel('Latency $\mu$s', fontsize=26, fontweight='bold')
        ax.set_xlim(1, 10000)
        ax.set_ylim(-0.02, 1.02)
        ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])
        # plt.yscale('log')
        # ply.
        fig.supylabel("CDF", fontsize=26, fontweight='bold')
        ax.grid(True)
        ## set xtick and y tick label font size
        ax.tick_params(axis='both', which='major', labelsize=24)

        plt.legend(ncols=1, loc='lower right', frameon=False)
        if z == 3:
            plt.legend(ncols=1, loc='lower left', frameon=False)
            

        # plt.tight_layout(rect=[0, 0, 1, 0.95])
        plt.subplots_adjust(left=0.15, right=0.96, top=0.98, bottom=0.20, wspace=0.0, hspace=0.0)
        app_name = ['L2TouchFwd', 'KVS', 'NAT', 'Crypto'][z] + "_CDF.pdf"
        plt.savefig(app_name, dpi=600)
        plt.close()



        #!#######################################
        #!# plot percentile for Dynamic
        #!#######################################
        if (z == 1 or z == 2):
            fig, ax = plt.subplots(1, 1, figsize=(6,4), dpi=600)

            offset =0 
            for category, color in DEFAULT_COLORS.items():
                if category not in ['SNC', 'non-SNC', 'TiNA']:
                    continue
                if category == 'SNC':
                    latencies = snc['Latency'].values
                    offset = -.2
                elif category == 'non-SNC':
                    latencies = nosnc['Latency'].values
                    offset = 0
                elif category == 'TiNA':
                    latencies = TiNA['Latency'].values
                    offset = .2
                if len(latencies) == 0:
                    continue
                ## plot bar chart where x axis is percentile and y axis is latency
                percentiles = [50, 90, 95, 99]
                lat = np.percentile(latencies, percentiles)
                ## offset for each category
                x_pos = np.arange(len(percentiles)) + offset
                ax.set_xlim(-0.5, 3.5)
                ax.set_xticks(x_pos - offset, ['p50', 'p90', 'p95', 'p99'])
                ax.tick_params(axis='both', which='major', labelsize=24)
                ax.bar(x_pos, lat, color=color, label=category, width=0.2)

            ax.legend(
                ncol=2, 
                loc='upper left', 
                columnspacing=.5,
                
                frameon=False
            )

            ax.set_ylim(ylim2[z][0], ylim2[z][1])
            ax.set_yticks(yticks2[z][0], yticks2[z][1])
            ax.grid(axis='y')

            fig.supylabel('Latency ($\mu$s)', fontsize=26, fontweight='bold')
            plt.subplots_adjust(left=0.21, right=0.98, top=0.95, bottom=0.10, wspace=0.0, hspace=0.11)    
            # plt.subplots_adjust(left=0.13, right=0.98, top=0.95, bottom=0.1, wspace=0.0, hspace=0.11)
            plt.savefig(app_name.replace("CDF", "percentile"), dpi=600)
            plt.close()


        # #!#######################################
        # #!# plot percentile for Dynamic -- Breakline Version
        # #!#######################################
        else:
            
            fig, (ax1,ax2) = plt.subplots(2, 1, figsize=(6,4), dpi=600, sharex=True, gridspec_kw={'height_ratios': graph_ratio[z]})
            fig.subplots_adjust(hspace=0.1)
            ## get 20, 40, 60, 80 , 99 percentile of the 3 categories

            offset =0 
            for category, color in DEFAULT_COLORS.items():
                if category not in ['SNC', 'non-SNC', 'TiNA']:
                    continue
                if category == 'SNC':
                    latencies = snc['Latency'].values
                    offset = -.2
                elif category == 'non-SNC':
                    latencies = nosnc['Latency'].values
                    offset = 0
                elif category == 'TiNA':
                    latencies = TiNA['Latency'].values
                    offset = .2
                if len(latencies) == 0:
                    continue
                ## plot bar chart where x axis is percentile and y axis is latency
                percentiles = [50, 90, 95, 99]
                lat = np.percentile(latencies, percentiles)
                ## offset for each category
                x_pos = np.arange(len(percentiles)) + offset
                ax1.set_xlim(-0.5, 3.5)
                ax2.set_xlim(-0.5, 3.5)
                ax1.set_xticks(x_pos - offset, ['p50', 'p90', 'p95', 'p99'])
                ax2.set_xticks(x_pos - offset, ['p50', 'p90', 'p95', 'p99'])
                ax1.tick_params(axis='both', which='major', labelsize=24)
                ax2.tick_params(axis='both', which='major', labelsize=24)
                ax1.bar(x_pos, lat, color=color, label=category, width=0.2)
                ax2.bar(x_pos,  lat, color=color, label=category, width=.2)
            
            # hide the spines between ax and ax2
            ax1.spines.bottom.set_visible(False)
            ax1.legend(
                ncol=2, 
                loc='upper left', 
                columnspacing=.5,
                frameon=False
            )
            ax2.spines.top.set_visible(False)
            ax1.xaxis.tick_top()
            ax1.tick_params(labeltop=False)  # don't put tick labels at the top
            ax2.xaxis.tick_bottom()    
            ax2.set_ylim(ylim2[z][0], ylim2[z][1])
            ax1.set_ylim(ylim1[z][0], ylim1[z][1])
            ax1.set_yticks(yticks1[z][0], yticks1[z][1])
            ax2.set_yticks(yticks2[z][0], yticks2[z][1])
            ax1.grid(axis='y')
            ax2.grid(axis='y')
            d = .5  # proportion of vertical to horizontal extent of the slanted line
            kwargs = dict(marker=[(-1, -d), (1, d)], markersize=12,
                        linestyle="none", color='k', mec='k', mew=1, clip_on=False)
            ax1.plot([0, 1], [0, 0], transform=ax1.transAxes, **kwargs)
            ax2.plot([0, 1], [1, 1], transform=ax2.transAxes, **kwargs)
            
            # if z == 0:
            fig.supylabel('Latency ($\mu$s)', fontsize=26, fontweight='bold')
            plt.subplots_adjust(left=0.21, right=0.98, top=0.95, bottom=0.10, wspace=0.0, hspace=0.11)      
            # else:
                # plt.subplots_adjust(left=0.13, right=0.98, top=0.95, bottom=0.10, wspace=0.0, hspace=0.11)                
            
            plt.savefig(app_name.replace("CDF", "percentile"), dpi=600)
            plt.close()

def main():
    parser = argparse.ArgumentParser(description="Plot latency boxplots and difference curves from latency files.")
    parser.add_argument("--single_input_dir", default="DATA/BURST_SWEEP")
    
    ####!Motivation
    parser.add_argument("--curve_motivation_output", default="latency_difference_curve_motivation_T0T3000.pdf")
    parser.add_argument("--boxplot_output_motivation", default="plots/fig4.pdf")
    
    ####!Evaluation
    parser.add_argument("--boxplot_output_s8", default="plots/fig9a.pdf")
    parser.add_argument("--load_input_dir", default="DATA/LOAD")
    parser.add_argument("--load_output", default="load_latency_s8.pdf")
    
    parser.add_argument("--compare_trace", default="DATA/STATIC-0NS")
    parser.add_argument("--compare_trace_output", default="plots/fig9b.pdf.pdf")
    
    parser.add_argument("--compare_dirs", nargs='+', default=["DATA/STATIC-0NS", "DATA/STATIC-1000NS", "DATA/STATIC-2000NS", "DATA/STATIC-3000NS"])
    parser.add_argument("--compare_output", default="latency_difference_curve_Touch1234.pdf")

    args = parser.parse_args()

    configure_plot_style()



    # #########! 2. Motivation Boxplot
    df = read_latency_files(args.single_input_dir, extension=".latency")
    if df.empty:
        print(f"No valid latency data found in {args.single_input_dir}.")
        sys.exit(1)
        
    plot_latency_boxplot(df, output_file=args.boxplot_output_motivation, categories=['SNC', 'non-SNC'])
    plot_latency_boxplot(df, output_file=args.boxplot_output_s8, categories=['SNC', 'non-SNC', 'TiNA'])
    plot_latency_curve_trace(df, output_file=args.compare_trace_output) 
    


if __name__ == '__main__':
    main()
