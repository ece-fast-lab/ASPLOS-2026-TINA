#ifndef DPDK_EXP_PKT_H
#define DPDK_EXP_PKT_H
#include <rte_ethdev.h>
#include "../rx/dpdk_perf.h"
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"
struct dpdk_exp_pkt
{
    rte_ether_hdr ether_hdr;
    rte_ipv4_hdr ipv4_hdr;
    rte_udp_hdr udp_hdr;
    //42 Bytes Already

    rte_mbuf_timestamp_t soft_timestamp;
    uint16_t magic;
    //52 Bytes Already

    uint8_t rx_ring_sample_index_array[4];  //!Can't be larger than 4 --> 32bits
    uint16_t rx_ring_sample_num_array[4];   //!Can't be larger than 4 --> 32bits
    
    //64 Bytes Already
    uint64_t fpga_tx_timestamp;
    uint64_t fpga_rx_timestamp;

    uint16_t ddr_processed_pkt_count;
    uint16_t sec_processed_pkt_count;
    uint64_t bytes_us;

    #if defined(ENABLE_REMOTE_PERF)
        uint64_t perf_event_values[numEvents];
    #endif

}__attribute__((__packed__));

#pragma GCC diagnostic pop

#endif //DPDK_EXP_PKT_H
