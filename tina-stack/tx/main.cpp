
#include "main.hpp"

#include <iostream>
#include <algorithm>
#include <rte_ethdev.h>
#include <vector>
#include <fstream>
#include <assert.h>
#include <iomanip>      // std::setw


/*******************************************************************************/
/************************************ RX For latency ****************************/
static int lcore_rx_burst(void *arg)
{
    unsigned int lcore_id = rte_lcore_id();
    unsigned int lcore_index = rte_lcore_index(lcore_id);
    unsigned int rx_index = lcore_index - rte_lcore_count() / 2 - 1;
    printf("[RX CORE] -- lcore %2u (lcore index %2u, RX index %2u starts to recieve packets\n", lcore_id, lcore_index, rx_index);
    struct rte_mbuf *bufs[BURST_SIZE];
    while (likely(keep_sending))
    {
        uint16_t nb_rx_pkts = rte_eth_rx_burst(port_id, rx_index, bufs, BURST_SIZE);
        // uint64_t latency = 0;
        if (likely(nb_rx_pkts != 0))
        {
            for (int i = 0; i < nb_rx_pkts; ++i)
            {
                if (latency_data.size() < SAMPLE_COUNT)
                {
                    latency_data[rx_index].push_back(get_latency(bufs[i]));
                }

                for (auto numa_idx = 0; numa_idx < 4; numa_idx++){
                    dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(bufs[i], dpdk_exp_pkt *);
                    auto rx_index = pkt->rx_ring_sample_index_array[numa_idx];

                    if (rx_occ_samples[rx_index].size() < SAMPLE_COUNT && 
                        rx_index != INVALID_RX_SAMPLE_ID &&
                        magic_found(bufs[i]))
                    {
                        rx_sample_point_t sample_point;

                        sample_point.sample = pkt->rx_ring_sample_num_array[numa_idx];
                        sample_point.rx_timestamp = pkt->fpga_rx_timestamp * 4;
                        #if defined(ENABLE_REMOTE_PERF)
                            for (size_t i = 0; i < numEvents; i++){
                                sample_point.perf_event_values[i] = pkt->perf_event_values[i];
                            }
                        #endif
                        sample_point.ddr_processed_pkt_count = pkt->ddr_processed_pkt_count;
                        sample_point.sec_processed_pkt_count = pkt->sec_processed_pkt_count;

                        rx_occ_samples[rx_index].push_back(sample_point);
                    }
                }

            }

            // get the last pkt as dpdk_exp_pkt
            dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(bufs[nb_rx_pkts - 1], dpdk_exp_pkt *);
            if (enable_c_rate)
            {
                pcimem_write(dev, 0x10, 'w', pkt->bytes_us);
            }

            rte_pktmbuf_free_bulk(bufs, nb_rx_pkts);
        }
    }
    return 0;
}

/*******************************************************************************/
/******************************** Burst TX Mode ********************************/
/*******************************************************************************/

static void usleep_high_precision(uint64_t us)
{
    uint64_t ns = us * 1000;
    if (ns == 0)
        return;
    // convert ns to tsc cycles
    uint64_t tsc_cycles = (tsc_hz * ns) / 1000000000;
    double start = rte_rdtsc();
    double end = start + tsc_cycles;
    while (rte_rdtsc() < end)
        ;
}

// Generate TX packets in a logical core
static int lcore_tx_burst(void *arg)
{

    unsigned int lcore_id = rte_lcore_id();
    unsigned int tx_index = rte_lcore_index(lcore_id) - 1;

    printf("[Bursting TX Mode] -- lcore %2u (lcore index %2u, TX index %2u starts to send packets\n", lcore_id, rte_lcore_index(lcore_id), tx_index);

    if (rte_eth_dev_socket_id(port_id) != (int)rte_socket_id())
        rte_exit(EXIT_FAILURE, "Port %u is on remote NUMA node to lcore %u\n", port_id, lcore_id);

    rte_srand(rte_get_timer_cycles() + lcore_id * 1024);

    //******************************************
    //********* Allocate and initialize packets
    //******************************************
    struct rte_mempool *mbuf_pool = (struct rte_mempool *)arg;
    // create 1 gb of mbufs
    struct rte_mbuf* mbufs[BURST_SIZE];
    if (rte_pktmbuf_alloc_bulk(mbuf_pool, mbufs, BURST_SIZE) != 0)
        rte_exit(EXIT_FAILURE, "Fail to allocate %u mbufs on lcore %2u\n", BURST_SIZE, lcore_id);

    for (size_t i = 0; i < BURST_SIZE; i++)
    {
        uint16_t src_port = rte_rand_max(SRC_PORT_RAND_MAX);
        uint16_t dst_port = FPGA_SWITCH_ID_PORT;

        // send all to the same dest ip
        create_udp_pkt(mbufs[i], pkt_size, src_mac, dst_mac, src_ip, dst_ip, i + 1, src_port, dst_port);
    }

    //******************************************
    //***************** To support rate limiting
    //******************************************

    uint64_t curr_burst_idx = 0;
    uint64_t pkts_in_curr_burst = 0;
    uint64_t bursts_sent = 0;
    while (likely(keep_sending))
    {
        pkts_in_curr_burst = pkt_per_burst_limited_percore_vec[curr_burst_idx];

        if (software_timestamp)
        {
            for (int64_t i = 0; i < BURST_SIZE; i++)
            {
                timestamp_packet(mbufs[i], 0);
            }
        }
        // get start time
        uint64_t start_time = rte_get_tsc_cycles();
        //**** Send Pkts from Mbufs ****/
        size_t nb_tx_pkts = 0;
        while (nb_tx_pkts < pkts_in_curr_burst)
        {
            int left_pkts = pkts_in_curr_burst - nb_tx_pkts;
            nb_tx_pkts += rte_eth_tx_burst(port_id, tx_index, mbufs, std::min(32, left_pkts));
        }

        uint64_t end_time = rte_get_tsc_cycles();
        double duration = (end_time - start_time) * 1000000.0 / tsc_hz;
        int64_t delta = burst_duration_us_vec[curr_burst_idx] - duration;
        if (delta > 0)
        {
            usleep_high_precision(int(delta));
        }

        //**** Refrash Src_Port Random Value */
        for (auto i = 0; i < BURST_SIZE; i++)
        {
            dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(mbufs[i], dpdk_exp_pkt *);
            pkt->udp_hdr.src_port = rte_rand_max(SRC_PORT_RAND_MAX);
            if (disable_random_src_ip != 0)
            {
                pkt->ipv4_hdr.src_addr = rand() % 4294967295 + 1; // Random 32-bit integer
            }
        }

        //**** Rate Limiting Related
        lcore_tx_pkts[tx_index] += nb_tx_pkts;
        usleep_high_precision(burst_gap_us_vec[curr_burst_idx]);
        curr_burst_idx = (curr_burst_idx + 1) % burst_duration_us_vec.size();
        bursts_sent++;
    }
    printf("lcore %2u sent %lu bursts\n", lcore_id, bursts_sent);
    return 0;
}

/*******************************************************************************/
/************************************ Main *************************************/
/*******************************************************************************/

int main(int argc, char **argv)
{
    std::cout << "==================== EAL ====================" << std::endl;
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
    }
    rte_timer_subsystem_init();
    tsc_hz = rte_get_tsc_hz();

    const char *sysfile = "/sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0/resource0";
    off_t target_offset = 0x1C;
    size_t map_length = 4096;

    dev = pcimem_init(sysfile, target_offset, map_length);
    if (!dev)
    {
        fprintf(stderr, "Failed to initialize PCI memory mapping.\n");
        return 1;
    }

    std::cout << "============== DPDK-RX General ==============" << std::endl;
    argc -= ret;
    argv += ret;

    // Parse application arguments
    ret = parse_args(argc, argv);
    if (ret < 0) 
        rte_exit(EXIT_FAILURE, "Invalid application arguments\n");


    print_config();

    //*******************************
    //******* MBufs and Rings Setup
    //*******************************
    unsigned int tx_lcore_count = rte_lcore_count() / 2;
    unsigned int rx_lcore_count = (rte_lcore_count() / 2);

    printf("TX Lcore Count:         %u\n", tx_lcore_count);
    printf("RX Lcore Count:         %u\n", rx_lcore_count);

    if (tx_lcore_count > MAX_TX_CORES)
        rte_exit(EXIT_FAILURE, "We only support up to %u lcores\n", MAX_TX_CORES);


    for (size_t i = 0; i < tx_lcore_count; i++) {
        lcore_tx_pkts_snapshot[i] = 0;
        lcore_tx_pkts[i] = 0;
    }


    #define NUM_MBUFS 8192
    printf("SETTING MBUF SIZE TO %u\n", RTE_ETHER_MAX_JUMBO_FRAME_LEN + RTE_PKTMBUF_HEADROOM);
    uint32_t num_mbufs = NUM_MBUFS * (tx_lcore_count + rx_lcore_count);
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
                                                            num_mbufs,
                                                            MBUF_CACHE_SIZE,
                                                            0,
                                                            16 * 1024,
                                                            rte_socket_id());

    if (!mbuf_pool)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    printf("MBUF_POOL size:         %u\n", num_mbufs);
    printf("TX RING COUNT:          %u\n", tx_lcore_count);
    printf("RX RING COUNT:          %u\n", rx_lcore_count);


    if (port_init(port_id, mbuf_pool, tx_lcore_count, rx_lcore_count, TX_RING_SIZE, RX_RING_SIZE) != 0)
    {
        rte_exit(EXIT_FAILURE, "Cannot init port %hu\n", port_id);
    }


    signal(SIGINT, stop_tx);
    signal(SIGTERM, stop_tx);
    signal(SIGKILL, [](int signal){printf("Receiving SIGKILL, skil cleaniing part & exiting...\n"); exit(1);});

    uint64_t hz = rte_get_timer_hz();
    uint64_t interval_cycles = (uint64_t) (monitor_interval_ms * hz / 1000.0);

    //*******************************
    //***************** Monitor Setup
    //*******************************

    printf("CPU Frequency:              %lu MHz\n", hz/1000000);
    printf("Monitor Interval:           %u ms\n", monitor_interval_ms);
    printf("Monitor Interval Cycles:    %lu\n", interval_cycles);
    
    uint32_t num_rx_core = rte_lcore_count() - tx_lcore_count;
    for (size_t i = 0; i < num_rx_core; i++)
    {
        latency_data.push_back(std::vector<uint64_t>());
        latency_data[i].reserve(SAMPLE_COUNT);
    }

    //*******************************
    //******************* Pkts Setup
    //*******************************
    std::cout << "==================== Package ====================" << std::endl;
    printf("Packet-dpdk useful part Size:   %lu bytes\n", sizeof(dpdk_exp_pkt));
    printf("Timestamp Method is             %s\n", software_timestamp ? "SW" : "FPGA_HW");
    printf("Packet Size:                    %u bytes\n", pkt_size);
    std::cout << std::endl;

    auto pkt_size_on_cable = pkt_size + RTE_ETHER_CRC_LEN + 20;
    double max_pps_on_link = ((int64_t)LINE_RATE_GBPS * (int64_t)1000 * (int64_t)1000 * (int64_t)1000) / ((pkt_size_on_cable) * 8.0);
    max_pps_on_link_limited_percore = static_cast<uint32_t>(max_pps_on_link / tx_lcore_count);

    printf("Max PPS On Link:                        %f\n", max_pps_on_link);
    printf("Max PPS Rate Limited Per Core:          %d\n", max_pps_on_link_limited_percore);
    std::cout << std::endl;

    if (burst_duration_us_vec.empty()){
        printf("Burst Duration is not set, so we will use 100us as default\n");
        burst_duration_us_vec.push_back(100);
    }

    if (burst_gap_us_vec.empty()){
        printf("Burst Gap is not set, so we will use 0us as default\n");
        burst_gap_us_vec.push_back(0);
    }

    printf("Burst Duration Vec:                     [0]: %u us, Size: %lu\n", burst_duration_us_vec[0], burst_duration_us_vec.size());
    printf("Burst Gap Vec:                          [0]: %u us, Size: %lu\n", burst_gap_us_vec[0], burst_gap_us_vec.size());



    for (size_t i = 0; i < burst_duration_us_vec.size(); i++){

        auto &burst_dur_us = burst_duration_us_vec[i];
        double total_pkt_per_burst = max_pps_on_link * (burst_dur_us / 1000000.0);         
        double pkt_per_burst_percore = total_pkt_per_burst / tx_lcore_count;

        pkt_per_burst_limited_percore_vec.push_back(std::round(pkt_per_burst_percore));

        if (i == 0){
            printf("--- For Burst[0]:\n");
            printf("--- Total Pkt Per Burst:                %f\n", total_pkt_per_burst);
            printf("--- Pkt Per Burst Limited Per Core:     %f\n", pkt_per_burst_percore);
        }
    }
    std::cout << std::endl;

    curr_working_pkt_per_burst_lp.store(pkt_per_burst_limited_percore_vec[0]);

    //*******************************
    //***************** Start Threads
    //*******************************

    for (size_t i = 1; i < tx_lcore_count + 1; i++)
        rte_eal_remote_launch(lcore_tx_burst, mbuf_pool, i);

    for (size_t i = tx_lcore_count + 1; i < rte_lcore_count(); i++)
        rte_eal_remote_launch(lcore_rx_burst, NULL, i);

    
    //**********************************************************
    //******************************************** Major Loops
    //**********************************************************

    uint64_t prev_tsc, cur_tsc;
    prev_tsc = rte_get_timer_cycles();
    while (likely(keep_sending))
    {

        cur_tsc = rte_get_timer_cycles();
        if ((cur_tsc - prev_tsc) < interval_cycles) 
            continue;
        prev_tsc = cur_tsc;


        uint64_t total_tx_pkts = 0;
        for (size_t i = 0; i < tx_lcore_count; i++)
        {
            uint64_t tmp = lcore_tx_pkts[i];
            total_tx_pkts += (tmp - lcore_tx_pkts_snapshot[i]);
            lcore_tx_pkts_snapshot[i] = tmp;
        }

        // Packet rate (millions packets per second)
        float pkt_rate = total_tx_pkts / (monitor_interval_ms / 1000.0) / 1000000.0;
        // Throughput (Gigabits per second). We should consider the following overheads:
        // preamble (7B), start frame delimiter (1B), CRC (4B) and interpacket gap (12B).
        float link_tput = pkt_rate * pkt_size * 8;
        float line_tput = pkt_rate * pkt_size_on_cable * 8;

        printf("TX: %8.4f M/%.3fs, link-tput: %8.1f Mbps, line-tput %8.1f Mbps\n", total_tx_pkts/1000000.0, monitor_interval_ms/1000.0, link_tput, line_tput);
   }


    //*******************************
    //************************* Exits
    //*******************************

    // Wait until all thread exit
   rte_eal_mp_wait_lcore();

   pcimem_cleanup(dev);

   ret = rte_eth_dev_stop(port_id);
   if (ret != 0)
       printf("rte_eth_dev_stop: err=%d, port=%d\n", ret, port_id);
   rte_eth_dev_close(port_id);

   // Clean up the EAL
   rte_eal_cleanup();

   //*******************************
   //************************ Statis
   //*******************************

   uint64_t total_tx_pkts = 0;
   for (size_t i = 0; i < tx_lcore_count; i++)
       total_tx_pkts += lcore_tx_pkts[i];

   printf("Send %" PRId64 " packets in total\n", total_tx_pkts);
   for (size_t i = 0; i < tx_lcore_count; i++)
   {
       printf("TX ring %2lu sends %" PRId64 " packets (%0.3f%%)\n",
              i, lcore_tx_pkts[i], lcore_tx_pkts[i] * 100.0 / total_tx_pkts);
   }

    // dump latency data to file
    // print file name
    if (!latency_outfile.empty()){
        std::cout << "Latency data is saving to " << latency_outfile << std::endl;
        std::ofstream latency_file(latency_outfile);
        for (uint64_t i = 0; i < latency_data.size(); ++i)
        {
            for (uint64_t j = 0; j < latency_data[i].size(); ++j)
            {
                latency_file << latency_data[i][j] << std::endl;
            }
        }
        latency_file.close();
    }

    if (!rx_occ_outfile.empty()){
        std::ofstream rx_occ_file(rx_occ_outfile);
        for (auto& [key, value]: rx_occ_samples)
        {
            if (value.size() == 0)  continue;

            uint64_t prev_timestamp = value[0].rx_timestamp;
            uint16_t prev_sample = value[0].sample;

            rx_occ_file << "------ For RX_Index "<< key << " ------" << std::endl;
            for (auto& i: value)
            {
                int32_t nic_sample_diff = ((int32_t)i.sample - (int32_t)prev_sample);
                int32_t nic_ingress = (nic_sample_diff > 0) ? (nic_sample_diff + i.ddr_processed_pkt_count + i.sec_processed_pkt_count) : 0;
                auto timestamp_diff = i.rx_timestamp - prev_timestamp;
                auto nic_ingress_rate = (nic_ingress == 0) ? 0 : timestamp_diff/nic_ingress;
                int32_t cpu_fetch_rate_ddr = (i.ddr_processed_pkt_count == 0) ? 0 : timestamp_diff/i.ddr_processed_pkt_count;       
                int32_t cpu_fetch_rate_sec = (i.sec_processed_pkt_count == 0) ? 0 : timestamp_diff/i.sec_processed_pkt_count;         

                rx_occ_file << "After " << std::setw(8) << timestamp_diff
                    << "ns -- "  << std::setw(5) << i.sample 
                    << " -- NIC Ingress: " << std::setw(6) << nic_ingress << "(" << std::setw(6) << nic_ingress_rate << ")"
                    << " -- CPU DDR Fetch:" << std::setw(6) << i.ddr_processed_pkt_count << "(" << std::setw(6) << cpu_fetch_rate_ddr << ")"
                    << " -- CPU Sec Fetch:" << std::setw(6) << i.sec_processed_pkt_count << "(" << std::setw(6) << cpu_fetch_rate_sec << ")";

                #if defined(ENABLE_REMOTE_PERF)
                    rx_occ_file << " -- Perf: ";
                    for (size_t pi = 0; pi < numEvents; pi++){
                        rx_occ_file << std::setw(10) << i.perf_event_values[pi] << " ";
                    }
                #endif

                rx_occ_file << std::endl;

                prev_timestamp = i.rx_timestamp;
                prev_sample = i.sample;
            }
        }
        rx_occ_file.close();
    }

    return EXIT_SUCCESS;
}

