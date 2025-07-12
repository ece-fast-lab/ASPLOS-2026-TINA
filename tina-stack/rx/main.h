#ifndef _MAIN_H_
#define _MAIN_H_

#include "../tx/dpdk_exp_pkt.h"
#include "apps/base_app.h"

#include <getopt.h>
#include <signal.h>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <numeric>
#include <unordered_map>
#include <stdexcept>
#include <atomic>

#include <rte_timer.h>  //Which include all other necessary rte_headers
#include <rte_eal.h>
#include <rte_ethdev.h>
/***********************************************************************/
/************************** Connection Setup ***************************/
/***********************************************************************/
#define LATENCY_REPORT_TX_RING_SIZE 64      //Can't be larger than RX_RING_SIZE
#define MBUF_CACHE_SIZE 0UL
#define BURST_SIZE 32
#define MAX_RX_CORES 16
#define NUM_OF_NUMA 4

// #define ENABLE_CLDEMOTE_AT_FREE
// #define ENABLE_CLDEMOTE_AT_GAP

static const struct rte_eth_conf port_conf_default = {
    .rxmode = {
        .mq_mode = RTE_ETH_MQ_RX_NONE,
        .max_lro_pkt_size = RTE_ETHER_MAX_LEN,
    },
    .txmode = {.mq_mode = RTE_ETH_MQ_TX_NONE},
};

static uint64_t port_id = 0;
static uint64_t rx_ring_size_ddr = 4096;
static uint64_t mbuf_size = 2048 + 128;
static uint64_t bytes_us = 0;
static std::string if_name = "NONE";


// Parse argument 'port'
static int64_t parse_port(char *arg)
{
    long n;
    char *endptr;

    n = strtol(optarg, &endptr, 10);
    if (n < 0) {
        fprintf(stderr, "PORT should be a non-negative integer argument\n");
        return -1;
    }

    port_id = (uint64_t)n;
    uint64_t nb_ports = rte_eth_dev_count_avail();
    if (port_id >= nb_ports) {
        fprintf(stderr, "PORT should be smaller than %lu (# of available ports)\n", nb_ports);
        return -1;
    }

    return 0;
}


/***********************************************************************/
/*********************** Application Specific **************************/
/***********************************************************************/
enum ApplicationChoice {
  Touch = 0,
  NoApp = 1,
  HeaderTouch = 2,
  KVS = 3,
  Crypto = 4,
  BM25 = 5,             //Don't Touch Pkt
  KNN = 6,              
  NAT = 7,


  _ApplicationChoiceCount
} application_choice;

//create string for application choice
static const std::unordered_map<ApplicationChoice, std::string> application_choice_str = {
    {Touch, "Touch"},
    {NoApp, "NoApp"},
    {HeaderTouch, "HeaderTouch"},
    {KVS, "KVS"},
    {Crypto, "Crypto"},
    {BM25, "BM25"},
    {KNN, "KNN"},
    {NAT, "NAT"}
};

enum SecondaryRingMode {
    None = 0,
    CXL = 1,
    NUMA = 2,

    _SecondaryRingModeCount
} secondary_ring_mode;

enum OperationMode {
    PIPELINE = 0,
    RTC = 1,
    _OpertionModeCount
} operation_mode;

static uint64_t tsc_hz;

static uint64_t rx_lcore_count = 0;
static uint64_t second_ring_size = 128;
static std::vector<rte_mempool*> rx_mbuf_pools_array;
static rte_mempool* tx_mbuf_pools;
static std::vector<uint64_t> ring_size_array;
enum MBuf_Pool_Array_Index{
    DDR_IDX = 0,
    CXL_IDX = 1,
    NUMA1_IDX = 1,
    NUMA2_IDX = 2,
    NUMA3_IDX = 3
} mbuf_pool_array_index;

static std::vector<rte_ring *> sw_qs;

static uint64_t app_arg1 = 0;
static std::string app_arg1_str = "";
static uint64_t app_arg2 = 0;
static std::string app_arg2_str = "";

static uint64_t waiting_time = 0;
static std::vector<std::shared_ptr<dpdk_apps::BaseApp>> app_p_vec;

/***********************************************************************/
/*************************** General Setup *****************************/
/***********************************************************************/

static std::vector<uint64_t> ring_rx_record;
static std::vector<uint64_t> lcore_tx_record;
static std::vector<uint64_t> ring_rx_record_snapshot;
static std::vector<uint64_t> lcore_tx_record_snapshot;

static std::vector<uint64_t> lcore_processing_time;
static std::vector<uint64_t> lcore_processing_time_snapshot;

static std::vector<uint64_t> lcore_polling_time;
static std::vector<uint64_t> lcore_polling_time_snapshot;

static std::vector<uint64_t> lcore_main_processing_time_ddr_ring;
static std::vector<uint64_t> lcore_main_processing_time_second_ring;
static std::vector<uint64_t> lcore_main_processing_time_ddr_ring_snapshot;
static std::vector<uint64_t> lcore_main_processing_time_second_ring_snapshot;


static volatile int64_t keep_receiving = 1;
static int64_t monitor_interval_ms = 1000;


static void print_usage(const char *prgname);
static void print_config();

/***********************************************************************/
/*********************** MBuf Record for Demote ************************/
/***********************************************************************/
#define MAX_MBUF_RECORD_COUNT 2000
#define DEMOTE_BATCH_SIZE 32UL

typedef struct mbuf_record {
    char* mbuf_ptr;
    size_t mbuf_size;
} mbuf_record_t;
typedef struct mbuf_record_idx {
    uint64_t idx;
    bool flag;
} mbuf_record_idx_t;

static std::vector<std::vector<mbuf_record_t>> lcore_mbuf_record;
static std::vector<mbuf_record_idx_t> lcore_mbuf_record_ptr_head;
static std::vector<mbuf_record_idx_t> lcore_mbuf_record_ptr_tail;


inline void inc_mbuf_ptr(mbuf_record_idx_t& ptr){
    ptr.flag = (ptr.idx == MAX_MBUF_RECORD_COUNT - 1) ? (!ptr.flag) : (ptr.flag);
    ptr.idx = (ptr.idx + 1) % MAX_MBUF_RECORD_COUNT;
}
inline bool queue_full(mbuf_record_idx_t& tail, mbuf_record_idx_t& head){
    return (tail.flag != head.flag && tail.idx == head.idx);
}
inline bool queue_empty(mbuf_record_idx_t& tail, mbuf_record_idx_t& head){
    return (tail.flag == head.flag && tail.idx == head.idx);
}
inline uint64_t queue_content_size(mbuf_record_idx_t& tail, mbuf_record_idx_t& head){
    return (tail.flag == head.flag) ? (head.idx - tail.idx) : (MAX_MBUF_RECORD_COUNT - (head.idx - tail.idx));
}


/***********************************************************************/
/*************************** Sampling Setup ****************************/
/***********************************************************************/

static int64_t latency_sample_frq = -1;


/***********************************************************************/
/***************************** Functions *******************************/
/***********************************************************************/


// Process RX packets in a logical core
static int pipeline_process(void *arg);

static int pipeline_poll(void *arg);

static int rtc_rx(void *arg);

static void app_process(rte_mbuf** pkts_burst, uint64_t nb_rx, uint64_t rx_index);



// Print usage of the program
static void print_usage(const char *prgname)
{
    printf("%s [EAL options] -- [-p PORT]\n"
           "    -p, --port=<PORT>               port to receive packets (default %lu)\n"
           "    -y  --rx_ring_size_ddr=<No.En>  number of rx ring entry per cpu core on DDR MBuf\n"
           "    -i, --monitor_interval=<ms>     mseconds between periodic reports, only appliable when call_main is disabled (default %lu)\n"
           "    -l, --latency_sample_frq        how many rx pkts do we send 1 tx response latency pkt? default to disable\n"
           "    -a  --application               application to run\n"
           "    -b  --app_args_1                application specific argument 1\n"
           "    -c  --app_args_2                application specific argument 2\n"
           "    -h, --help                      print usage of the program\n"
           "    -s, --second_rings_mode         enable second ring mode, Default to NotUsingSecondaryRing\n" 
           "    -d, --second_rings_size         secondary ring size, default to 128\n"
           "    -o, --operation_mode            operation mode, default to pipeline\n"

           "\n\n"
           "Application Choices:\n"
           "[Touch]     --  [Args1 -----> waiting time (ns)                                              ]\n"
           "[KVS]       --  [Args1 -----> key_pool_count                                            ]\n"
           "[Crypto]    --  [Args1 -----> engineIDString(rdrand or pka),  Args2 -----> Algorithm ID ]\n"
           "[BM25]      --  [Args1 -----> data footprint                                            ]\n"
           "[KNN]       --  [Args1 -----> data footprint                                            ]\n",
           prgname, port_id, monitor_interval_ms);
}

static struct option long_options[] = {
    {"port",                required_argument,  0,      'p' },
    {"rx_ring_size_ddr",    required_argument,  0,      'y' },
    {"monitor_interval_ms", required_argument,  0,      'i' },
    {"latency_sample_frq",  required_argument,  0,      'l' },
    {"application",         required_argument,  0,      'a' },
    {"app_args_1",          required_argument,  0,      'b' },
    {"app_args_2",          required_argument,  0,      'c' },
    {"help",                no_argument,        0,      'h' },
    {"second_ring_mode",    required_argument,  0,      's' },
    {"second_ring_size",    required_argument,  0,      'd' },
    {"operation_mode",      required_argument,  0,      'o' },
    {NULL,                  0,                  NULL,   0   }
};

// Parse the argument given in the command line of the application
static int64_t parse_args(const int64_t argc, char **argv)
{
    const char *prgname = argv[0];
    const char short_options[] = "p:y:i:l:a:b:c:s:d:h:o:";        //!Need to end with ":", o/w it will SEGFAULT
    int64_t c;
    int64_t ret;
    char *endptr;

    while ((c = getopt_long(argc, argv, short_options, long_options, NULL)) != EOF) {

        switch (c) {
            case 'p':
                ret = parse_port(optarg);
                if (ret < 0) {
                    printf("Failed to parse port\n");
                    return -1;
                }
                break;

            case 'y':
                rx_ring_size_ddr = strtoul(optarg, &endptr, 10);    
                break;

            case 'i':
                monitor_interval_ms = strtoul(optarg, &endptr, 10);  
                break;           

            case 'l':
                latency_sample_frq = strtoul(optarg, &endptr, 10);
                break;

            case 'a':
            {
                uint64_t val = (uint64_t)strtoul(optarg, &endptr, 10);
                if (val >= _ApplicationChoiceCount) {
                    printf("Invalid application choice\n");
                    return -1;
                }
                application_choice = static_cast<ApplicationChoice>(val);
                break;
            }
            case 'b':
                app_arg1 = (uint64_t)strtoul(optarg, &endptr, 10);
                app_arg1_str = optarg;
                break;

            case 'c':
                app_arg2 = (uint64_t)strtoul(optarg, &endptr, 10);
                app_arg2_str = optarg;
                break;
            case 's':
            {
                uint64_t val = (uint64_t)strtoul(optarg, &endptr, 10);
                if (val >= _SecondaryRingModeCount) {
                    printf("Invalid secondary ring mode\n");
                    return -1;
                }
                secondary_ring_mode = static_cast<SecondaryRingMode>(val);
                break;
            }
            case 'o':
            {
                uint64_t val = (uint64_t)strtoul(optarg, &endptr, 10);
                if (val >= _OpertionModeCount) {
                    printf("Invalid operation mode\n");
                    return -1;
                }
                operation_mode = static_cast<OperationMode>(val);
                break;
            }
            case 'd':
            {
                //Check if number if power of 2
                second_ring_size = (uint64_t)strtoul(optarg, &endptr, 10);
                if ((second_ring_size & (second_ring_size - 1)) != 0) {
                    printf("Secondary ring size should be power of 2\n");
                    return -1;
                }
                break;
            }
            case 'h':
            default:
                print_usage(prgname);
                return -1;
        }
    }

	return 0;
}


// Print the configuration of the program
static void print_config()
{
    printf("=============== Configuration ===============\n");
    printf("DDR RX Ring Size/Core:  %lu\n", rx_ring_size_ddr);
    printf("Mbuf Size:              %lu\n", mbuf_size);
    printf("Port:                   %lu\n", port_id);
    printf("Monitor Interval:       %lu msec\n", monitor_interval_ms);
    printf("Latency Sampling_Frq:   %s\n", latency_sample_frq == -1 ? "Disabled" : std::to_string(latency_sample_frq).c_str());
    printf("APP:                    %s\n", application_choice_str.at(application_choice).c_str());
    printf("APP Arg1:               %lu\n", app_arg1);
    printf("APP Arg2:               %lu\n", app_arg2);
    printf("Second Ring Mode        %s\n", secondary_ring_mode == None ? "None" : (secondary_ring_mode == CXL ? "CXL" : "NUMA"));
    printf("Second Ring Size        %s\n", secondary_ring_mode == None ? "N/A" : std::to_string(second_ring_size).c_str());
    printf("Operation Mode          %s\n", operation_mode == PIPELINE ? "Pipeline" : "RTC");
    #if defined(ENABLE_CLDEMOTE_AT_FREE)
        printf("CLDEMOTE At Free        Enabled\n");
    #endif
    #if defined(ENABLE_CLDEMOTE_AT_GAP)
        printf("CLDEMOTE At Gap         Enabled\n");
    #endif
    #if defined(ENABLE_REMOTE_PERF)
        printf("Remote Perf             Enabled\n");
    #endif
}

int setup_flows(const std::vector<uint16_t>& ddr_rx_ids, const std::vector<uint16_t>& second_rx_ids)
{
    uint16_t port_id = 0;
    
    //*** Attributes */
    struct rte_flow_attr attr;
    memset(&attr, 0, sizeof(struct rte_flow_attr));
    attr.ingress = 1;

    //*** Patterns */
    struct rte_flow_item pattern[4];
    memset(pattern, 0, sizeof(pattern));
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
    pattern[0].spec = NULL;
    pattern[0].mask = NULL;

    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = NULL;
    pattern[1].mask = NULL;

    pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

    //*** UDP spec and mask */
    struct rte_flow_item_udp udp_spec;
    struct rte_flow_item_udp udp_mask;
    memset(&udp_spec, 0, sizeof(struct rte_flow_item_udp));
    memset(&udp_mask, 0, sizeof(struct rte_flow_item_udp));

    //*** Action */
    struct rte_flow_action action[2];
    memset(action, 0, sizeof(action));

    //***************************************************************** */
    //***** Rule 1: Last two bits of UDP dest port are '00' (binary) ****/
    //***************************************************************** */

    std::cout << "Setting up Rule 1 for flow control" << std::endl;
    assert(ddr_rx_ids.size() != 0 && "DDR RX IDs should not be empty");
    attr.priority = 0;      /* Set the priority for this rule (higher priority) */

    /* Match when last two bits of dst_port are '00' */
    udp_spec.hdr.dst_port = rte_cpu_to_be_16(0x0000); /* Value with last two bits '00' */
    udp_mask.hdr.dst_port = rte_cpu_to_be_16(0x0003); /* Mask to consider last two bits */

    /* Assign spec and mask to UDP pattern */
    pattern[2].spec = &udp_spec;
    pattern[2].mask = &udp_mask;

    struct rte_flow_action_rss ddr_rss_action;
    memset(&ddr_rss_action, 0, sizeof(ddr_rss_action));
    ddr_rss_action.queue_num = ddr_rx_ids.size();   /* Number of queues */
    ddr_rss_action.queue = ddr_rx_ids.data();       /* Queue IDs */
    ddr_rss_action.types = RTE_ETH_RSS_UDP;         /* Hash on IP and UDP fields */

    action[0].type = RTE_FLOW_ACTION_TYPE_RSS;
    action[0].conf = &ddr_rss_action;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    /* Validate and create the flow rule */
    struct rte_flow_error error;
    if (rte_flow_validate(port_id, &attr, pattern, action, &error))
        rte_exit(EXIT_FAILURE, "Flow validation failed (Rule 1): %s\n", error.message ? error.message : "(no stated reason)");

    if (!rte_flow_create(port_id, &attr, pattern, action, &error))
        rte_exit(EXIT_FAILURE, "Flow creation failed (Rule 1): %s\n", error.message ? error.message : "(no stated reason)");

    //************************************************************** */
    //***** Rule 2: Last two bits of UDP dest port are '10' (binary) */
    //************************************************************** */


    /* Set the priority for this rule (lower priority than Rule 1) */
    attr.priority = 1;

    /* Match when last two bits of dst_port are '10' */
    udp_spec.hdr.dst_port = rte_cpu_to_be_16(0x0002); /* Value with last two bits '10' */
    udp_mask.hdr.dst_port = rte_cpu_to_be_16(0x0003); /* Mask to consider last two bits */

    /* Assign spec and mask to UDP pattern */
    pattern[2].spec = &udp_spec;
    pattern[2].mask = &udp_mask;

    /* Configure the RSS action */
    struct rte_flow_action_rss second_rss_action;
    memset(&second_rss_action, 0, sizeof(second_rss_action));
    second_rss_action.types = RTE_ETH_RSS_UDP;                  /* Hash on UDP fields */
    second_rss_action.key_len = 0;                              /* Use default RSS key */   
    second_rss_action.key = NULL;                               /* Use default key */

    if (second_rx_ids.size() != 0){
        second_rss_action.queue_num = second_rx_ids.size();   /* Number of queues */
        second_rss_action.queue = second_rx_ids.data();       /* Queue IDs */
    } else {
        second_rss_action.queue_num = ddr_rx_ids.size();   /* Number of queues */
        second_rss_action.queue = ddr_rx_ids.data();       /* Queue IDs */
    }

    action[0].type = RTE_FLOW_ACTION_TYPE_RSS;
    action[0].conf = &second_rss_action;
    action[1].type = RTE_FLOW_ACTION_TYPE_END;

    /* Validate and create the flow rule */
    if (rte_flow_validate(port_id, &attr, pattern, action, &error))
        rte_exit(EXIT_FAILURE, "Flow validation failed (Rule 2): %s\n", error.message ? error.message : "(no stated reason)");

    if (!rte_flow_create(port_id, &attr, pattern, action, &error))
        rte_exit(EXIT_FAILURE, "Flow creation failed (Rule 2): %s\n", error.message ? error.message : "(no stated reason)");

    return 0;
}


void setup_eth_dev(uint64_t rx_ring_count, uint64_t tx_ring_count, uint16_t* nb_txd, uint16_t* nb_rxd_first, uint16_t* nb_rxd_second){

    struct rte_eth_conf port_conf = port_conf_default;
    uint64_t retval;
    retval = rte_eth_dev_configure(port_id, rx_ring_count, tx_ring_count, &port_conf);
    if (retval != 0) 
        rte_exit(EXIT_FAILURE, "Error during rte_eth_dev_configure\n");

    // Adjust # of descriptors for each TX/RX ring
    uint16_t old_nb_txd = *nb_txd;
    uint16_t old_nb_rxd_first = *nb_rxd_first;

    uint16_t nb_rxd_second_null_bypassed = 0;
    uint16_t old_nb_rxd_second = 0;

    retval = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, nb_txd, nb_rxd_first);
    if (nb_rxd_second){
        nb_rxd_second_null_bypassed = *nb_rxd_second;
        old_nb_rxd_second = *nb_rxd_second;
        retval = (retval != 0) ? retval : rte_eth_dev_adjust_nb_rx_tx_desc(port_id, nb_txd, &nb_rxd_second_null_bypassed);
        *nb_rxd_second = nb_rxd_second_null_bypassed;
    }


    if (retval != 0)
        rte_exit(EXIT_FAILURE, "Error during rte_eth_dev_adjust_nb_rx_tx_desc\n");
    if (*nb_txd != old_nb_txd || *nb_rxd_first != old_nb_rxd_first || old_nb_rxd_second != nb_rxd_second_null_bypassed)
        rte_exit(EXIT_FAILURE, "Warning: RX/TX ring size is Adjusted, "
        "Rx_Ring_first_size %d --> %d, "
        "Rx_Ring_second_size %d --> %d, "
        "Tx_Ring_size %d --> %d\n", 
        old_nb_rxd_first, *nb_rxd_first, 
        (nb_rxd_second == nullptr) ? -1 : old_nb_rxd_second, 
        (nb_rxd_second == nullptr) ? -1 : nb_rxd_second_null_bypassed, 
        old_nb_txd, *nb_txd);


}

void setup_sw_qs(uint64_t num_qs)
{
    if (num_qs == 0)
    {
        fprintf(stderr, "No software queues to setup\n");
        return;
    }
    for (uint64_t i = 0; i < num_qs; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "SWQ%lu", i);
        sw_qs.push_back(rte_ring_create(name, 256 * 1024, 0, RING_F_SC_DEQ));
    }
}

void setup_ring_monitors(const std::vector<uint16_t>& ddr_rx_ids, const std::vector<uint16_t>& second_rx_ids, uint64_t total_lcores){
    uint64_t total_rx_ring_count = ddr_rx_ids.size() + second_rx_ids.size();
    ring_rx_record.resize(total_rx_ring_count, 0);
    ring_rx_record_snapshot.resize(total_rx_ring_count, 0);

    lcore_tx_record.resize(total_lcores, 0);
    lcore_tx_record_snapshot.resize(total_lcores, 0);

    lcore_processing_time.resize(total_lcores, 0);
    lcore_processing_time_snapshot.resize(total_lcores, 0);

    lcore_polling_time.resize(total_lcores, 0);
    lcore_polling_time_snapshot.resize(total_lcores, 0);

    lcore_main_processing_time_ddr_ring.resize(total_lcores, 0);
    lcore_main_processing_time_second_ring.resize(total_lcores, 0);
    lcore_main_processing_time_ddr_ring_snapshot.resize(total_lcores, 0);
    lcore_main_processing_time_second_ring_snapshot.resize(total_lcores, 0);

    lcore_mbuf_record.resize(total_lcores, std::vector<mbuf_record_t>(MAX_MBUF_RECORD_COUNT));
    lcore_mbuf_record_ptr_head.resize(total_lcores, {0, 0});
    lcore_mbuf_record_ptr_tail.resize(total_lcores, {0, 0});
}

// Handle signal and stop receiving packets
static void stop_rx(int sig)
{
    printf("Crtl+C pressed, preparing to exit...\n");
    keep_receiving=0;
}


static void nsleep_high_precision(uint64_t ns){
    if (ns == 0) return;
    // convert ns to tsc cycles
    uint64_t tsc_cycles = (tsc_hz * ns) / 1000000000;
    double start = rte_rdtsc();
    double end = start + tsc_cycles;
    while(rte_rdtsc() < end);
}

struct numa_allocation_thread_args {
    const char* name;
    uint8_t numa_idx;
    uint64_t rx_num_mbuf_ddr123;
};

pthread_t numa_pool_allocation_thread[3];
std::atomic<size_t> numa_pool_allocation_thread_finish(0);
void* numa_pool_allocation(void* args){
    struct numa_allocation_thread_args* numa_args = (struct numa_allocation_thread_args*)args;

    const char* name = numa_args->name;
    uint8_t numa_idx = numa_args->numa_idx;
    uint64_t rx_num_mbuf_ddr123 = numa_args->rx_num_mbuf_ddr123;

    //**** */
    int num_cores_per_numa = sysconf(_SC_NPROCESSORS_ONLN)/NUM_OF_NUMA;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(num_cores_per_numa * numa_idx, &cpuset);
    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset)){
        printf("Failed to set affinity for numa allocation thread\n");
        return NULL;
    }

    auto mbuf_ptr = rte_pktmbuf_pool_create(name, rx_num_mbuf_ddr123, MBUF_CACHE_SIZE, 0, mbuf_size, numa_idx);
    rx_mbuf_pools_array[numa_idx] = mbuf_ptr;

    numa_pool_allocation_thread_finish++;

    //
    while(numa_pool_allocation_thread_finish.load() != 4);

    while(keep_receiving){

    }

    return NULL;
}

/**
 * This function exist, because the following rule:
 * !(Total MBuf Count) Need to be >= (Each Single RX Ring Size + 32)
 * !Don't know exactly why 32, but experiment prove this.
 * !Probably due to it is the bulk size for rx_thres?
 */
inline uint64_t mbuf_count_calculation(uint64_t ring_size, uint64_t ring_count){
    return 65*1024;
    uint64_t anticipated_mbuf_count = ring_size * ring_count * 1.1;
    if (anticipated_mbuf_count < ring_size + 32)
        printf( "\033[1;33m\033[1m"
                "Warning: Mbuf Count Violate:(Total MBuf Count) Need to be >= (Each Single RX Ring Size + 32) \n"
                "Use %ld instead of %ld\n"
                "\033[0m", ring_size + 32, anticipated_mbuf_count
        );
    return std::max(anticipated_mbuf_count, ring_size + 32);        
}

inline void bring_down_interface(std::string& if_name){
    std::string cmd = "sudo ip link set "+if_name+" "+"down";    
    printf("Executing: %s\n", cmd.c_str());                      
    auto rvl = system(cmd.c_str());
    if (rvl != 0) 
        printf( "\033[1;31m\033[1m"
                "Failed to bring down interface %s\n"
                "\033[0m", if_name.c_str());                          
}

inline struct rte_mbuf* build_tx_stats_pkt(struct rte_mbuf* rx_pkt){

    /**
     * !We dont use dpdk_clone because we want to fix the size of the tx back pkt to smallest ~80B
     */

    struct rte_mbuf* pkt = rte_pktmbuf_alloc(tx_mbuf_pools);
    if (!pkt) 
        rte_exit(EXIT_FAILURE, "Cannot allocate mbuf for tx stats pkt\n");

    struct dpdk_exp_pkt* pkt_data = rte_pktmbuf_mtod(pkt, struct dpdk_exp_pkt*);
    struct dpdk_exp_pkt* rx_pkt_data = rte_pktmbuf_mtod(rx_pkt, struct dpdk_exp_pkt*);

    /**
     * !For now we don't copy Eth/IP/UDP header to save time
     * **/

    //Construct the Ethernet Header
    pkt_data->magic = rx_pkt_data->magic;
    pkt_data->soft_timestamp = rx_pkt_data->soft_timestamp;

    memcpy(&pkt_data->rx_ring_sample_index_array, &rx_pkt_data->rx_ring_sample_index_array, sizeof(pkt_data->rx_ring_sample_index_array));
    memcpy(&pkt_data->rx_ring_sample_num_array, &rx_pkt_data->rx_ring_sample_num_array, sizeof(pkt_data->rx_ring_sample_num_array));
    assert(sizeof(pkt_data->rx_ring_sample_index_array) == 4);

    pkt_data->fpga_tx_timestamp = rx_pkt_data->fpga_tx_timestamp;
    pkt_data->fpga_rx_timestamp = rx_pkt_data->fpga_rx_timestamp;
    pkt_data->bytes_us = bytes_us;
    pkt->data_len = sizeof(dpdk_exp_pkt);
    pkt->pkt_len = pkt->data_len;

    return pkt;
}




#endif /* _MAIN_H_ */
