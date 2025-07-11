#ifndef _MAIN_H_
#define _MAIN_H_

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_timer.h>
#include <rte_random.h>
#include <math.h>
#include <pthread.h>
#include <atomic>
#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <unordered_map>
#include <sstream>
#include "pcim.hpp"

#include "./dpdk_exp_pkt.h"
#include "../dpdk-rx/dpdk_perf.h"
/*****************************************************************************************************/
/***************************************** Connection Setup ******************************************/
/*****************************************************************************************************/

#define TX_RING_SIZE 4096
#define RX_RING_SIZE 4096
#define LINE_RATE_GBPS 100
#define MBUF_CACHE_SIZE 512
#define FPGA_SWITCH_ID_PORT 100
#define SRC_PORT_RAND_MAX 65535

#define BURST_SIZE 32
#define MAX_TX_CORES 16
#define INVALID_RX_SAMPLE_ID 255

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .mtu = RTE_ETHER_MAX_JUMBO_FRAME_LEN,
    },
    .txmode = {.mq_mode = RTE_ETH_MQ_TX_NONE}};

static uint16_t port_id = 0;
static uint32_t max_pps_on_link_limited_percore = 0;
static uint32_t disable_random_src_ip = 0;
static uint64_t tsc_hz;
static bool enable_c_rate = false;
static pcimem_dev_t *dev = NULL;

/****************** Burst Mode TX ******************/
static std::vector<uint64_t> pkt_per_burst_limited_percore_vec;

// static unsigned int burst_gap_us = 0;

static std::atomic<uint64_t> burst_counter(0);
static std::atomic<uint64_t> curr_working_pkt_per_burst_lp(0);

static std::atomic<bool> sync_start(0);

static std::vector<unsigned int> burst_duration_us_vec;
static std::vector<unsigned int> burst_gap_us_vec;
/***************************************************/

static int parse_burst_file(char *arg)
{
    std::ifstream input(arg);
    std::string line;

    if (!input.is_open())
    {
        fprintf(stderr, "BURST_FILE %s cannot be opened\n", arg);
        return -1;
    }

    if (!burst_duration_us_vec.empty() || !burst_gap_us_vec.empty())
    {
        fprintf(stderr, "BURST_FILE %s is already loaded, or you manually defined single burst_duration\n", arg);
        return -1;
    }

    int count = 0;
    while (std::getline(input, line))
    {
        std::istringstream iss(line);
        unsigned int duration, gap;
        if (!(iss >> duration >> gap))
        {
            fprintf(stderr, "Invalid format in BURST_FILE %s at line %d\n", arg, count + 1);
            return -1;
        }        
        burst_duration_us_vec.push_back(duration);
        burst_gap_us_vec.push_back(gap);
        count++;
    }
    if (burst_duration_us_vec.empty() || burst_gap_us_vec.empty())
    {
        fprintf(stderr, "BURST_FILE %s is empty\n", arg);
        return -1;
    }

    std::cout << "Read " << count << " burst from file " << arg << std::endl;

    return 0;
}

/*****************************************************************************************************/
/******************************************* Package Setup *******************************************/
/*****************************************************************************************************/

static struct rte_ether_addr src_mac;
static struct rte_ether_addr dst_mac;
static rte_be32_t src_ip;
static rte_be32_t dst_ip;

static uint16_t pkt_size = RTE_ETHER_MIN_LEN - RTE_ETHER_CRC_LEN;

/*****************************************************************************************************/
/******************************************* Monitor Setup *******************************************/
/*****************************************************************************************************/

static uint64_t lcore_tx_pkts_snapshot[MAX_TX_CORES];
static uint64_t lcore_tx_pkts[MAX_TX_CORES];

/*****************************************************************************************************/
/************************************** RX Side Sampling Setup ***************************************/
/*****************************************************************************************************/
static uint8_t software_timestamp = 0;
static std::string latency_outfile = "";
#define SAMPLE_COUNT 100000
std::vector<std::vector<uint64_t>> latency_data;

static std::string rx_occ_outfile = "";

typedef struct rx_sample_point{
    uint16_t sample;
    uint64_t rx_timestamp;

    #if defined(ENABLE_REMOTE_PERF)
        uint64_t perf_event_values[numEvents];
    #endif
    
    uint16_t ddr_processed_pkt_count;
    uint16_t sec_processed_pkt_count;
} rx_sample_point_t;

std::unordered_map<uint32_t, std::vector<rx_sample_point_t>> rx_occ_samples;

/*****************************************************************************************************/
/******************************************* General Setup *******************************************/
/*****************************************************************************************************/

static volatile int keep_sending = 1;
static unsigned int monitor_interval_ms = 1000;

static int parse_port(char *arg)
{
    long n;
    char **endptr = NULL;

    n = strtol(arg, endptr, 10);
    if (n < 0)
    {
        fprintf(stderr, "PORT should be a non-negative integer argument\n");
        return -1;
    }

    port_id = (uint16_t)n;
    uint16_t nb_ports = rte_eth_dev_count_avail();
    if (port_id >= nb_ports)
    {
        fprintf(stderr, "PORT should be smaller than %hu (total # of available ports)\n", nb_ports);
        return -1;
    }

    return 0;
}

static void print_usage(const char *prgname)
{
    printf("%s [EAL options] -- [-p PORT] [-i INTERVAL] [-s SIZE] [-r RATE] -B MAC -E MAC -j IP -J IP\n"
           "    -p, --port=<PORT>               port to send packets (default %hu)\n"
           "    -i, --monitor_interval=<ms>     mseconds between periodic reports, only appliable when call_main is disabled (default %u)\n"
           "    -s, --size=<SIZE>               packet (excluding Ethernet CRC) has <SIZE> bytes\n"
           "        --disable_random_src_ip     don't use random src_ip for each core and pkg, used for FPGA related Exp\n"
           "    -B, --source-mac=<MAC>          source MAC address by this format XX:XX:XX:XX:XX:XX\n"
           "    -E, --dest-mac=<MAC>            destination MAC address by this format XX:XX:XX:XX:XX:XX\n"
           "    -j, --source-ip=<IP>            source IP address by this format X.X.X.X\n"
           "    -J, --dest-ip=<IP>              destination IP address by this format X.X.X.X\n"
           "    -d, --brust_duration=<us>       burst duration in us (default is 100us)\n"
           "    -g, --brust_gap=<us>            burst gap in us (default is 0us, aka steady state)\n"
           "    -f, --burst_file=<file>         file for looping different burst durations, everyline with one number(no comma)\n"
           "    -O, --latency_outfile=<file>    file to write latency data to \n"
           "    -R  --rx_sample_outfile=<file>  file to write rx sample data to \n"
           "    -S, --software-timestamp        use software timestamping\n"
           "   -c, --enable_c_rate              enable c rate (default no)\n"
           "    -h, --help                      print usage of the program\n",
           prgname, port_id, monitor_interval_ms);
}

static int parse_args(int argc, char **argv)
{
    struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {"enable_c_rate", no_argument, 0, 'c'},
        {"monitor_interval_ms", required_argument, 0, 'i'},
        {"size", required_argument, 0, 's'},
        {"disable_random_src_ip", no_argument, 0, 'z'},
        {"source-mac", required_argument, 0, 'B'},
        {"dest-mac", required_argument, 0, 'E'},
        {"source-ip", required_argument, 0, 'j'},
        {"dest-ip", required_argument, 0, 'J'},
        {"brust_duration", required_argument, 0, 'd'},
        {"brust_gap", required_argument, 0, 'g'},
        {"burst_file", required_argument, 0, 'f'},
        {"latency-outfile", required_argument, 0, 'O'},
        {"rx_sample_outfile", required_argument, 0, 'R'},
        {"software-timestamp", no_argument, 0, 'S'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    char short_options[] = "p:i:s:r:R:B:E:j:J:d:g:f:O:S:c:h";
    char *prgname = argv[0];

    int nb_required_args = 0;
    int ret;
    int c;
    char** endptr = NULL;

    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) != EOF) 
    {
    
        switch (c)
        {

        case 'p':
            ret = parse_port(optarg);
            if (ret < 0)
            {
                return -1;
            }
            break;

        case 'i':
            monitor_interval_ms = (unsigned long)strtoul(optarg, endptr, 10);  
	    break;

        case 's':
	    {
	        pkt_size = (unsigned long)strtoul(optarg, endptr, 10);  

            if (pkt_size == 0)
            {
                fprintf(stderr, "PKT_SIZE should be a positive integer argument\n");
                return -1;
            }

            uint16_t min_pkt_size = RTE_ETHER_MIN_LEN - RTE_ETHER_CRC_LEN;
            uint16_t max_pkt_size = RTE_ETHER_MAX_JUMBO_FRAME_LEN - RTE_ETHER_CRC_LEN;
            if (pkt_size < min_pkt_size || pkt_size > max_pkt_size)
            {
                fprintf(stderr, "PKT_SIZE should be in [%hu, %hu]\n", min_pkt_size, max_pkt_size);
                return -1;
            }
	        break;
	    }

        case 'z':
            disable_random_src_ip = 1;
            break;

        case 'B':
            if (rte_ether_unformat_addr(optarg, &src_mac) < 0)
            {
                fprintf(stderr, "Invalid SRC_MAC %s\n", optarg);
                return -1;
            }

            nb_required_args++;
            break;

        case 'E':
            if (rte_ether_unformat_addr(optarg, &dst_mac) < 0)
            {
                fprintf(stderr, "Invalid DST_MAC %s\n", optarg);
                return -1;
            }

            nb_required_args++;
            break;

        case 'j':
            if (inet_pton(AF_INET, optarg, &src_ip) <= 0)
            {
                fprintf(stderr, "Invalid SRC_IP %s\n", optarg);
                return -1;
            }

            nb_required_args++;
            break;

        case 'J':
            if (inet_pton(AF_INET, optarg, &dst_ip) <= 0)
            {
                fprintf(stderr, "Invalid DST_IP %s\n", optarg);
                return -1;
            }

            nb_required_args++;
            break;

        case 'd':
        {
            uint32_t n = (uint32_t)strtoul(optarg, endptr, 10);
            if (n == 0)
            {
                fprintf(stderr, "BURST_DURATION should be a positive integer argument\n");
                return -1;
            }

            if (!burst_duration_us_vec.empty())
            {
                fprintf(stderr, "You already loaded certain duration, probably from a file?\n");
                return -1;
            }

            burst_duration_us_vec.push_back(n);
            break;
        }

        case 'g':
        {
            uint32_t n = (uint32_t)strtoul(optarg, endptr, 10);

            if (!burst_gap_us_vec.empty())
            {
                fprintf(stderr, "You already loaded certain duration, probably from a file?\n");
                return -1;
            }

            burst_gap_us_vec.push_back(n);
            break;
        }

        case 'f':
            ret = parse_burst_file(optarg);
            if (ret < 0)
            {
                return -1;
            }
            break;

        case 'O':
            latency_outfile = std::string(optarg);
            if (latency_outfile.empty() || latency_outfile == "0")
            {
                fprintf(stderr, "LATENCY_OUTFILE %s is empty\n", optarg);
                return -1;
            }
            break;

        case 'R':
            rx_occ_outfile = std::string(optarg);
            if (rx_occ_outfile.empty() || rx_occ_outfile == "0")
            {
                fprintf(stderr, "RX_OCC_OUTFILE %s is empty\n", optarg);
                return -1;
            }
            break;


        case 'S':
            software_timestamp = 1;
            break;

        case 'c':
            enable_c_rate = true;
            break;

        case 'h':
        default:
            print_usage(prgname);
            return -1;
        }
    }

    if (nb_required_args != 4)
    {
        fprintf(stderr, "We need <source_mac>, <dest_mac>, <source_ip>, <dest_ip>\n");
        print_usage(prgname);
        return -1;
    }

    if (optind >= 0)
    {
        argv[optind - 1] = prgname;
    }

    // reset getopt lib
    optind = 1;

    return 0;
}

bool magic_found(const struct rte_mbuf *mbuf)
{
    dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(mbuf, dpdk_exp_pkt *);
    return (pkt->magic == 0xdead);
}

void set_magic(struct rte_mbuf *mbuf)
{
    dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(mbuf, dpdk_exp_pkt *);
    pkt->magic = 0xdead;
}

void clear_magic(const struct rte_mbuf *mbuf)
{
    dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(mbuf, dpdk_exp_pkt *);
    pkt->magic = 0;
}

uint64_t get_soft_tx_timestamp(const struct rte_mbuf *mbuf)
{
    dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(mbuf, dpdk_exp_pkt *);
    return pkt->soft_timestamp;
}

void timestamp_packet(struct rte_mbuf *mbuf, rte_mbuf_timestamp_t delta)
{
    set_magic(mbuf);
    int64_t time = rte_get_timer_cycles();

    dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(mbuf, dpdk_exp_pkt *);
    pkt->soft_timestamp = time + delta;
}



uint64_t get_latency(const struct rte_mbuf *mbuf)
{

    if (software_timestamp)
    {
        if (!magic_found(mbuf))
        {
            return uint64_t(-1);
        }
        uint64_t tx_ts = get_soft_tx_timestamp(mbuf);
        uint64_t rx_ts = rte_get_timer_cycles();
        uint64_t delta = (rx_ts - tx_ts) * 1E9 / rte_get_timer_hz();;
        clear_magic(mbuf);
        return delta;
    } else {

        dpdk_exp_pkt *pkt = rte_pktmbuf_mtod(mbuf, dpdk_exp_pkt *);
        uint64_t tx = pkt->fpga_tx_timestamp;
        uint64_t rx = pkt->fpga_rx_timestamp;
        // printf("TX: %lu, RX: %lu, diff: %lu\n", tx, rx, rx - tx);

        assert(rx == *rte_pktmbuf_mtod_offset(mbuf, uint64_t *, 72));
        assert(tx == *rte_pktmbuf_mtod_offset(mbuf, uint64_t *, 64));

        // return tx;
        uint64_t delta = rx - tx;
        return delta * 4;
    }
}

void print_mac(struct rte_ether_addr mac)
{
    printf("%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8
           ":%02" PRIx8 ":%02" PRIx8 ":%02" PRIx8 "\n",
           mac.addr_bytes[0], mac.addr_bytes[1], mac.addr_bytes[2],
           mac.addr_bytes[3], mac.addr_bytes[4], mac.addr_bytes[5]);
}

void print_ip(rte_be32_t ip)
{
    struct in_addr addr = {.s_addr = ip};
    printf("%s\n", inet_ntoa(addr));
}

static void print_config()
{
    printf("Port:                   %u\n", port_id);
    printf("Interval:               %u msec\n", monitor_interval_ms);
    printf("Packet Size:            %u bytes\n", pkt_size);
    printf("Src MAC:        ");
    print_mac(src_mac);
    printf("Dst MAC:        ");
    print_mac(dst_mac);
    printf("Src IP:         ");
    print_ip(src_ip);
    printf("Dst IP:         ");
    print_ip(dst_ip);

}



// Initialzie a port. Return 0 on success.
//  port: ID of the port to initialize
//  mbuf_pool: packet buffer pool for RX packets
//  nb_tx_rings: number of TX rings
//  nb_rx_rings: number of RX rings
//  tx_ring_size: number of transmission descriptors to allocate for the TX ring
//  rx_ring_size: number of transmission descriptors to allocate for the RX ring
static int port_init(uint16_t port,
                     struct rte_mempool *mbuf_pool,
                     uint16_t nb_tx_rings,
                     uint16_t nb_rx_rings,
                     uint16_t tx_ring_size,
                     uint16_t rx_ring_size)
{
    struct rte_eth_conf port_conf = port_conf_default;
    uint16_t nb_txd = tx_ring_size;
    uint16_t nb_rxd = rx_ring_size;
    int retval;
    struct rte_eth_dev_info dev_info;

    printf("Init port %hu\n", port);

    if (!rte_eth_dev_is_valid_port(port))
    {
        return -1;
    }

    // Get device information
    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0)
    {
        fprintf(stderr, "Error during getting device (port %u) info: %s\n", port, strerror(-retval));
        return retval;
    }
    // printf("PCI address: %s\n", dev_info.device->name);

    // Enable HW RX TS
    if (dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_TIMESTAMP)
    {
        port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;
    }

    // Configure RSS
    port_conf.rx_adv_conf.rss_conf.rss_hf &= dev_info.flow_type_rss_offloads;
    if (port_conf.rx_adv_conf.rss_conf.rss_hf !=
        port_conf_default.rx_adv_conf.rss_conf.rss_hf)
    {
        printf("Port %u modifies RSS hash function based on hardware support,"
               "requested:%#" PRIx64 " configured:%#" PRIx64 "\n",
               port,
               port_conf_default.rx_adv_conf.rss_conf.rss_hf,
               port_conf.rx_adv_conf.rss_conf.rss_hf);
    }

    // Configure the Ethernet device
    retval = rte_eth_dev_configure(port, nb_rx_rings, nb_tx_rings, &port_conf);
    if (retval != 0)
    {
        fprintf(stderr, "Error during rte_eth_dev_configure\n");
        return retval;
    }

    // Adjust # of descriptors for each TX/RX ring
    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
    if (retval != 0)
    {
        fprintf(stderr, "Error during rte_eth_dev_adjust_nb_rx_tx_desc\n");
        return retval;
    }

    int socket_id = rte_eth_dev_socket_id(port);
    // printf("Socket ID = %d\n", socket_id);

    // TX setup
    for (uint16_t q = 0; q < nb_tx_rings; q++)
    {
        retval = rte_eth_tx_queue_setup(port, q, nb_txd, socket_id, NULL);
        if (retval < 0)
        {
            fprintf(stderr, "Error during rte_eth_tx_queue_setup for queue %hu\n", q);
            return retval;
        }
    }
    printf("Set up %hu TX rings (%hu descriptors per ring)\n", nb_tx_rings, nb_txd);

    // RX setup
    for (uint16_t q = 0; q < nb_rx_rings; q++)
    {
        retval = rte_eth_rx_queue_setup(port, q, nb_rxd, socket_id, NULL, mbuf_pool);
        if (retval < 0)
        {
            fprintf(stderr, "Error during rte_eth_rx_queue_setup for queue %hu\n", q);
            return retval;
        }
    }
    printf("Set up %hu RX rings (%hu descriptors per ring)\n", nb_rx_rings, nb_rxd);

    // Start the Ethernet port.
    retval = rte_eth_dev_start(port);
    if (retval < 0)
    {
        fprintf(stderr, "Error during rte_eth_dev_start\n");
        return retval;
    }

    // Display the port MAC address
    struct rte_ether_addr addr;
    retval = rte_eth_macaddr_get(port, &addr);
    if (retval != 0)
    {
        fprintf(stderr, "Error during rte_eth_macaddr_get\n");
        return retval;
    }
    printf("Port %hu MAC: ", port);
    print_mac(addr);

    // Enable RX in promiscuous mode for the Ethernet device.
    retval = rte_eth_promiscuous_enable(port);
    if (retval != 0)
    {
        fprintf(stderr, "Error during rte_eth_promiscuous_enable\n");
        return retval;
    }

    // /*Enable timestamp*/
    // retval = rte_eth_timesync_enable(port);
    // if (retval < 0)
    // {
    //     rte_exit(EXIT_FAILURE, "rte_eth_timesync_enable:err=%d, port=%u\n",
    //              retval, port);
    // }

    return 0;
}

// Create an UDP packet.
static void create_udp_pkt(struct rte_mbuf *pkt,
                           uint16_t pkt_len,
                           struct rte_ether_addr src_mac_addr,
                           struct rte_ether_addr dst_mac_addr,
                           rte_be32_t src_ip_addr,
                           rte_be32_t dst_ip_addr,
                           uint16_t ip_id,
                           uint16_t src_port,
                           uint16_t dst_port)
{

    /** Should better use dpdk_exp_pkt struct here also **/

    uint16_t ip_pkt_len = pkt_len - sizeof(struct rte_ether_hdr);
    uint16_t udp_pkt_len = ip_pkt_len - sizeof(struct rte_ipv4_hdr);

    struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
    size_t offset = sizeof(struct rte_ether_hdr);
    struct rte_ipv4_hdr *ip_hdr = rte_pktmbuf_mtod_offset(pkt,
                                                          struct rte_ipv4_hdr *,
                                                          offset);
    offset = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr);
    struct rte_udp_hdr *udp_hdr = rte_pktmbuf_mtod_offset(pkt,
                                                          struct rte_udp_hdr *,
                                                          offset);

    eth_hdr->src_addr = src_mac_addr;
    eth_hdr->dst_addr = dst_mac_addr;
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    ip_hdr->src_addr = src_ip_addr;
    ip_hdr->dst_addr = dst_ip_addr;
    ip_hdr->version_ihl = RTE_IPV4_VHL_DEF;
    ip_hdr->type_of_service = 2;
    ip_hdr->total_length = rte_cpu_to_be_16(ip_pkt_len);
    ip_hdr->packet_id = rte_cpu_to_be_16(ip_id);
    ip_hdr->fragment_offset = 0;
    ip_hdr->time_to_live = 64;
    ip_hdr->next_proto_id = IPPROTO_UDP;
    // ip_hdr->hdr_checksum = 0;
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);

    udp_hdr->src_port = rte_cpu_to_be_16(src_port);
    udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
    udp_hdr->dgram_len = rte_cpu_to_be_16(udp_pkt_len);
    udp_hdr->dgram_cksum = 0;
    udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hdr);

    dpdk_exp_pkt *dpdk_pkt = rte_pktmbuf_mtod(pkt, dpdk_exp_pkt *);
    memset(&(dpdk_pkt->rx_ring_sample_index_array), INVALID_RX_SAMPLE_ID, sizeof(dpdk_exp_pkt));

    uint8_t *data = rte_pktmbuf_mtod_offset(pkt, uint8_t *, sizeof(dpdk_exp_pkt));
    for (size_t i = 0; i < pkt_len - sizeof(dpdk_exp_pkt); i++)
    {
        data[i] = (uint8_t)rand();
    }

    pkt->data_len = pkt_len;
    pkt->pkt_len = pkt->data_len;
    set_magic(pkt);
}

// Handle signal and stop sending packets
static void stop_tx(int sig)
{
    std::cout << "Stopping TX" << std::endl;
    keep_sending = 0;
}

#endif /* _MAIN_H_ */
