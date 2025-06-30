

#include "main.h"

#include "apps/headerTouch_app.h"
#include "apps/kvs_app.h"
#include "apps/crypto_app.h"
#include "apps/bm25_app.h"
#include "apps/knn_app.h"
#include "apps/nat_app_hash.h"
#include "./dpdk_perf.h"

#include <fstream>
#include <net/if.h>

#define RTE_EXIT_PRINT(...)                                                     \
    do {                                                                        \
        if (if_name == "NONE") {                                                \
            printf("\033[1;31m\033[1m"                                          \
                   "During Exiting, Interface Status Might Be messed up\n"      \
                   "Please manually \"ifconfig <interface> down \""             \
                   "to avoid any potential experiment inaccuracy\n"             \
                   "\033[0m");                                                  \
        } else {                                                                \
            bring_down_interface(if_name);                                      \
        }                                                                       \
        rte_exit(__VA_ARGS__);                                                  \
                                                                                \
    } while(0)


/*************************************************************************/
/*************************** Regular RX Thread ***************************/
/*************************************************************************/

static void app_process(rte_mbuf **pkts_burst, uint64_t nb_rx, uint64_t rx_index)
{
    switch (application_choice)
    {

    /*********************************/
    case Touch:
    {
        for (uint64_t i = 0; i < nb_rx; i++)
        {
            char *pkt_ptr = rte_pktmbuf_mtod(pkts_burst[i], char *);
            volatile uint64_t flag;
            for (uint64_t k = 0; k < pkts_burst[i]->pkt_len; k += 64)
            { // Make sure we bring every cacheline into L2
                flag = flag + pkt_ptr[k];
            }
        }
        nsleep_high_precision(waiting_time);
        break;
    }

    /*********************************/
    case HeaderTouch:
    case KVS:
    case Crypto:
    case BM25:
    case NAT:
    case KNN:
    {
        for (uint64_t i = 0; i < nb_rx; i++)
        {
            char *pkt_ptr = rte_pktmbuf_mtod(pkts_burst[i], char *);
            app_p_vec[rx_index]->run(pkt_ptr, pkts_burst[i]->pkt_len);
        }
        break;
    }

    /*********************************/
    case NoApp:
    default:
        break;
    }
}

static int pipeline_process(void *arg)
{
    int64_t processed_pkt = 0;

    //**** RX Thread Setups */
    int64_t rx_lcore_id = rte_lcore_id();
    int64_t rx_index = (rte_lcore_index(rx_lcore_id) - 1) / 2;
    printf("lcore %2lu (main_core_id %2u, SW Q index %2lu) starts to POLL FOR packets FROM SW Q\n", rx_lcore_id, rte_get_main_lcore(), rx_index);

    rte_ring *my_sw_ring = sw_qs[rx_index];

    if (rte_eth_dev_socket_id(port_id) != (int)rte_socket_id())
    {
        fprintf(stderr, "WARNING, port %lu is on remote NUMA node to lcore %lu\n", port_id, rx_lcore_id);
    }

    while (keep_receiving)
    {
        struct rte_mbuf *pkts_burst[BURST_SIZE * 4]; // To support maximum the (secondary_ring_mode == NUMA) case
        uint64_t nb_rx_final = 0;

        // POLL SW Q
        nb_rx_final = rte_ring_dequeue_burst(my_sw_ring, (void **)pkts_burst, BURST_SIZE, nullptr);

        if (nb_rx_final == 0)
            continue;

        //**** Ring Touching
        uint64_t processing_time_start = rte_get_timer_cycles();
        app_process(pkts_burst, nb_rx_final, rx_index);
        
        processed_pkt += nb_rx_final;
        // SAMPLE AND TX

        auto lat_sample_count = processed_pkt;
        if (lat_sample_count > latency_sample_frq && latency_sample_frq != -1)
        {
            auto pkt = build_tx_stats_pkt(pkts_burst[0]);
            if (pkt == nullptr) {
                printf("Failed to clone and send packet\n");
            } else {
                dpdk_exp_pkt* dpdk_pkt = rte_pktmbuf_mtod(pkt, dpdk_exp_pkt*);
                dpdk_pkt->rx_ring_sample_num_array[0] = rte_ring_count(my_sw_ring) + rte_eth_rx_queue_count(port_id, rx_index);
                dpdk_pkt->rx_ring_sample_index_array[0] = rx_index;
                dpdk_pkt->ddr_processed_pkt_count = processed_pkt;
                auto nb_tx = rte_eth_tx_burst(port_id, rx_index, &pkt, 1);
                lcore_tx_record[rx_index] += nb_tx;
            }
            processed_pkt = 0;
        }

        for (uint64_t i = 0; i < nb_rx_final; i++)
        {
            rte_pktmbuf_free(pkts_burst[i]);
        }

        lcore_processing_time[rx_index] += (rte_get_timer_cycles() - processing_time_start);
    }
    
    // free all packets from the sw ring that are not processed
    printf("lcore %2lu (main_core_id %2u, SW Q index %2lu) exits\n", rx_lcore_id, rte_get_main_lcore(), rx_index);
    return 0;
}

// Process RX packets in a logical core
static int pipeline_poll(void *arg)
{
    //**** RX Thread Setups */
    int64_t rx_lcore_id = rte_lcore_id();
    int64_t rx_index = rte_lcore_index(rx_lcore_id) - 1;
    rte_ring *my_sw_ring = sw_qs[rx_index];
    printf("lcore %2lu (main_core_id %2u, RX index %2lu) starts to PROCESS packets\n", rx_lcore_id, rte_get_main_lcore(), rx_index);

    if (rte_eth_dev_socket_id(port_id) != (int64_t)rte_socket_id())
    {
        fprintf(stderr, "WARNING, port %lu is on remote NUMA node to lcore %lu\n", port_id, rx_lcore_id);
    }

    enum RX_CURR_RING {
        TIER0_RING = 0, 
        TIER1_RING = 1,
        NUM_TIERS = 2
    } curr_ring = TIER0_RING;

    /****************************************************************************************/
    /************************************** Major Loop **************************************/
    /****************************************************************************************/
    while (keep_receiving)
    {

        struct rte_mbuf *pkts_burst[BURST_SIZE * 4]; // To support maximum the (secondary_ring_mode == NUMA) case
        uint64_t nb_rx_final = 0;

        uint64_t processing_time_start;

        if (curr_ring == TIER0_RING)
        {
            nb_rx_final += rte_eth_rx_burst(port_id, rx_index, pkts_burst, BURST_SIZE);
            processing_time_start = rte_get_timer_cycles();
            ring_rx_record[rx_index] += nb_rx_final;
        }
        if (curr_ring == TIER1_RING)
        {
            if (secondary_ring_mode == CXL)
            {
                rte_panic("CXL MODE NOT IMPLEMENTED IN THIS VERSION!\n");
            }
            else if (secondary_ring_mode == NUMA)
            {
                uint64_t nb_rx_numa1, nb_rx_numa2, nb_rx_numa3;
                nb_rx_numa1 = rte_eth_rx_burst(port_id, rx_index + rx_lcore_count, pkts_burst, BURST_SIZE);
                nb_rx_numa2 = rte_eth_rx_burst(port_id, rx_index + rx_lcore_count * 2, pkts_burst + nb_rx_numa1, BURST_SIZE);
                nb_rx_numa3 = rte_eth_rx_burst(port_id, rx_index + rx_lcore_count * 3, pkts_burst + nb_rx_numa1 + nb_rx_numa2, BURST_SIZE);
                ring_rx_record[rx_index + rx_lcore_count] += nb_rx_numa1;
                ring_rx_record[rx_index + rx_lcore_count * 2] += nb_rx_numa2;
                ring_rx_record[rx_index + rx_lcore_count * 3] += nb_rx_numa3;
                nb_rx_final = nb_rx_numa1 + nb_rx_numa2 + nb_rx_numa3;
            }
        }


        /*****************************************************************/
        /************************* RX Processing *************************/
        /*****************************************************************/

        // queue the packets to the software queue in a busy-waiting manner
        uint64_t num_enqueued = 0;
        while ((num_enqueued < nb_rx_final) && keep_receiving)
        {
            num_enqueued += rte_ring_enqueue_burst(my_sw_ring, (void **)pkts_burst + num_enqueued, nb_rx_final - num_enqueued, nullptr);
        }

        if (nb_rx_final != 0)
        {
            lcore_polling_time[rx_index] += (rte_get_timer_cycles() - processing_time_start);
        }

        if (nb_rx_final == 0 && (secondary_ring_mode == NUMA))
        {
            curr_ring = (curr_ring == TIER0_RING) ? TIER1_RING : TIER0_RING;
        }

    }
    return 0;
}

/*************************************************************************/
/********************************* Main **********************************/
/*************************************************************************/

int main(int argc, char **argv)
{

    std::cout << "==================== EAL ====================" << std::endl;
    // Initialize Environment Abstraction Layer (EAL)
    int64_t ret = rte_eal_init(argc, argv);
    if (ret < 0)
    {
        RTE_EXIT_PRINT(EXIT_FAILURE, "Invalid EAL arguments\n");
    }
    rte_timer_subsystem_init();

#if defined(ENABLE_REMOTE_PERF)
    if (open_perf_events(4))
        RTE_EXIT_PRINT(EXIT_FAILURE, "Failed to open perf events\n");
#endif

    //****************************************************/
    //************************************* DPDK-RX General
    //****************************************************/
    argc -= ret;
    argv += ret;

    ret = parse_args(argc, argv);
    if (ret < 0)
    {
        RTE_EXIT_PRINT(EXIT_FAILURE, "Invalid application arguments\n");
    }

    tsc_hz = rte_get_tsc_hz();

    print_config();

    switch (operation_mode)
    {
        case OperationMode::PIPELINE:
            if (rte_lcore_count() != 3)
            {
                RTE_EXIT_PRINT(EXIT_FAILURE, "Pipeline mode only supports 3 lcores\n");
            }
            rx_lcore_count = 1;
            break;
        case OperationMode::RTC:
            rx_lcore_count = rte_lcore_count() - 1;
            break;
        default:
            RTE_EXIT_PRINT(EXIT_FAILURE, "Invalid operation mode\n");
    }
    
    printf("RX lcore count: %lu\n", rx_lcore_count);
    
    if (rx_lcore_count > MAX_RX_CORES) {
        RTE_EXIT_PRINT(EXIT_FAILURE, "We only support up to %u lcores\n", MAX_RX_CORES);
    }
    keep_receiving = 1;

    /***********************************************************************************/
    /***************************** MBufs & Port Setup. *********************************/
    /***********************************************************************************/
    int64_t retval;
    std::vector<uint16_t> ddr_rx_ids;
    std::vector<uint16_t> second_rx_ids;

    //*** Check Dev Ports */
    if (!rte_eth_dev_is_valid_port(port_id)) 
        RTE_EXIT_PRINT(EXIT_FAILURE, "Invalid port @%ld\n", port_id);

    //*** Get Device Info */
    struct rte_eth_dev_info dev_info;
    retval = rte_eth_dev_info_get(port_id, &dev_info);
    if (retval != 0) 
        RTE_EXIT_PRINT(EXIT_FAILURE, "Error during getting device (port %lu) info: %s\n", port_id, strerror(-retval));
    
    char if_name_tmp[20];
    if (if_indextoname(dev_info.if_index, if_name_tmp) == nullptr) {
        RTE_EXIT_PRINT(EXIT_FAILURE, "Cannot get interface name for port %lu\n", port_id);
    }
    if_name = std::string(if_name_tmp);
    

    printf("\n================== Interface ==================\n");
    printf("Interface Name:             %s\n", if_name.c_str());

    //*** Configure RSS */
    if (!(dev_info.flow_type_rss_offloads & RTE_ETH_RSS_UDP))
        RTE_EXIT_PRINT(EXIT_FAILURE, "The device does not support RSS on UDP traffic.\n");

    if (!(dev_info.flow_type_rss_offloads & RTE_ETH_RSS_L4_DST_ONLY))
        RTE_EXIT_PRINT(EXIT_FAILURE, "The device may not support matching on UDP port numbers.\n");


    //! ~~~~~~~~ Don't ever change this,, it will ruin entire experiment
    constexpr uint64_t snc_numa_count = 4;      
    constexpr uint64_t nosnc_numa_count = 1;
    constexpr uint64_t cxl_numa_count = 1;
    //! ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

    //*** lcore Tx MBuf Setup */
    printf("\n=============== Mbuf/Ring Setup ================\n");
    char tx_pool_name[] = "MBUF_POOL_TX_DDR";
    uint64_t tx_num_mbuf = LATENCY_REPORT_TX_RING_SIZE * rx_lcore_count;
    tx_mbuf_pools = rte_pktmbuf_pool_create(tx_pool_name, tx_num_mbuf, MBUF_CACHE_SIZE, 0, mbuf_size, 0);
    assert(tx_num_mbuf > MBUF_CACHE_SIZE && "Please make sure the total size of a single MBuf is at least bigger than Mbuf_cache_size");     
    if (!tx_mbuf_pools) 
        RTE_EXIT_PRINT(EXIT_FAILURE, "Cannot create mbuf pool @0, Errno: %s\n", rte_strerror(rte_errno));
    else 
        printf("Created TX mbuf pool %s, size %u\n", tx_mbuf_pools->name, tx_mbuf_pools->size);
    

    switch (secondary_ring_mode)
    {
        //! 1-Mbuf, N-Ring/Within 1-Mbuf
        case None:  /******************************** None *********************************/
        {
            std::cout << "------------- NO SECONDARY RING -------------" << std::endl;

            char rx_pool_name[] = "MBUF_POOL_RX_DDR";
            uint64_t rx_num_mbuf = 512 * 1024;
            rx_mbuf_pools_array.emplace_back(rte_pktmbuf_pool_create_by_ops(rx_pool_name, rx_num_mbuf, 0, 0, mbuf_size ,0, "stack")); // Numa0 or SubNUMA0 all the time

            //*** Mbuf Setup Check */
            assert(rx_num_mbuf > MBUF_CACHE_SIZE &&
                "Please make sure the total size of a single MBuf is at least bigger than Mbuf_cache_size");
            if (!rx_mbuf_pools_array[DDR_IDX]) 
                RTE_EXIT_PRINT(EXIT_FAILURE, "Cannot create mbuf pool @0, Errno: %s\n", rte_strerror(rte_errno));
            else 
                printf("Created mbuf pool %s, size %u\n", rx_mbuf_pools_array[DDR_IDX]->name, rx_mbuf_pools_array[DDR_IDX]->size);
                
            //*** Rings Setup */
            uint16_t nb_txd = LATENCY_REPORT_TX_RING_SIZE;
            uint16_t nb_rxd = rx_ring_size_ddr;  
            setup_eth_dev(rx_lcore_count, rx_lcore_count, &nb_txd, &nb_rxd, nullptr);

            for (uint64_t q = 0; q < rx_lcore_count; q++) {
                retval = rte_eth_tx_queue_setup(port_id, q, nb_txd, SOCKET_ID_ANY, NULL);
                retval = (retval != 0) ? retval : 
                         rte_eth_rx_queue_setup(port_id, q, nb_rxd, 0, NULL, rx_mbuf_pools_array[DDR_IDX]);

                ddr_rx_ids.push_back(q);
                if (retval < 0) {
                    RTE_EXIT_PRINT(EXIT_FAILURE, "Error during rx/tx for queue %lu\n", q);
                }
            }
            printf("Set up %hu @ %lu tx rings, %hu @ %lu rx rings\n", nb_txd, rx_lcore_count, nb_rxd, rx_lcore_count);
            std::cout << "--------------------------------------------" << std::endl;
            break;
        }

        //! 2-Mbuf, N-Ring/Within Mbuf[0], N-Ring/Within Mbuf[1] 
        case CXL:   /******************************** CXL **********************************/
        {
            //*** Sanity Check
            if ((rte_socket_count() != (cxl_numa_count+snc_numa_count)) && (rte_socket_count() != (cxl_numa_count+nosnc_numa_count)))
                RTE_EXIT_PRINT(EXIT_FAILURE, "CXL Numa Node Not Found!!\n");

            //*** Setup Pool */
            char rx_pool_name_ddr[] = "MBUF_POOL_RX_DDR";
            uint64_t rx_num_mbuf_ddr = mbuf_count_calculation(rx_ring_size_ddr, rx_lcore_count);
            rx_mbuf_pools_array.emplace_back(rte_pktmbuf_pool_create(rx_pool_name_ddr,
                                                                        rx_num_mbuf_ddr,
                                                                        MBUF_CACHE_SIZE,
                                                                        0,
                                                                        mbuf_size,
                                                                        0));      //Numa0 or SubNUMA0 all the time

            char rx_pool_name_cxl[] = "MBUF_POOL_RX_CXL";
            uint64_t cxl_numa_id = rte_socket_count() - 1;                                       //!CXL is always the last NUMA, regardless of SNC or NOSNC
            uint64_t rx_num_mbuf_cxl = mbuf_count_calculation(second_ring_size, rx_lcore_count); //!We don't allocate TX on CXL rings
            rx_mbuf_pools_array.emplace_back(rte_pktmbuf_pool_create(rx_pool_name_cxl,
                                                                  rx_num_mbuf_cxl,
                                                                  MBUF_CACHE_SIZE,
                                                                  0,
                                                                  mbuf_size,
                                                                  cxl_numa_id));    

            //*** Mbuf Setup Check */
            assert(rx_num_mbuf_cxl > MBUF_CACHE_SIZE && rx_num_mbuf_ddr > MBUF_CACHE_SIZE && 
                "Please make sure the total size of a single MBuf is at least bigger than Mbuf_cache_size");
            if (!rx_mbuf_pools_array[DDR_IDX])       RTE_EXIT_PRINT(EXIT_FAILURE, "Cannot create mbuf pool @0, Errno: %s\n", rte_strerror(rte_errno));
            else if (!rx_mbuf_pools_array[CXL_IDX])  RTE_EXIT_PRINT(EXIT_FAILURE, "Cannot create mbuf pool @1, Errno: %s\n", rte_strerror(rte_errno));
            else {
                printf("Created mbuf pool %s, size %u\n", rx_mbuf_pools_array[DDR_IDX]->name, rx_mbuf_pools_array[DDR_IDX]->size);
                printf("Created mbuf pool %s, size %u\n", rx_mbuf_pools_array[CXL_IDX]->name, rx_mbuf_pools_array[CXL_IDX]->size);
            }

            //*** Rings Setup */
            uint16_t nb_txd = LATENCY_REPORT_TX_RING_SIZE;
            uint16_t nb_rxd_first = rx_ring_size_ddr;
            uint16_t nb_rxd_second = second_ring_size;
            setup_eth_dev(rx_lcore_count * 2, rx_lcore_count, &nb_txd, &nb_rxd_first, &nb_rxd_second);

            for (uint64_t q = 0; q < rx_lcore_count; q++) {
                retval = rte_eth_tx_queue_setup(port_id, q, nb_txd, SOCKET_ID_ANY, NULL);

                retval = (retval != 0) ? retval : rte_eth_rx_queue_setup(port_id, q, rx_ring_size_ddr, 0, NULL, rx_mbuf_pools_array[DDR_IDX]);
                retval = (retval != 0) ? retval : rte_eth_rx_queue_setup(port_id, q + rx_lcore_count, second_ring_size, cxl_numa_id, NULL, rx_mbuf_pools_array[CXL_IDX]);
                
                ddr_rx_ids.push_back(q);
                second_rx_ids.push_back(q + rx_lcore_count);
                
                if (retval < 0) 
                    RTE_EXIT_PRINT(EXIT_FAILURE, "Error during rx/tx for queue %lu, Errno: %lu\n", q, retval);
            }
            printf("Set up %hu @ %lu tx rings, %lu @ %lu DDR rx rings, %lu @ %lu CXL rx rings\n", 
                nb_txd, rx_lcore_count, rx_ring_size_ddr, rx_lcore_count, second_ring_size, rx_lcore_count);

            break;
        }

        //! 4-Mbuf, N-Ring/Within Mbuf[0], N-Ring/Within Mbuf[1], N-Ring/Within Mbuf[2], N-Ring/Within Mbuf[3]
        case NUMA:  /******************************* NUMA **********************************/
        {
            //*** Sanity Check
            std::cout << "------------- NUMA 1-2-3 As Secondary RINGs -------------" << std::endl;
            if (rte_socket_count() != (snc_numa_count) && (rte_socket_count() != (snc_numa_count + cxl_numa_count)))
                RTE_EXIT_PRINT(EXIT_FAILURE, "You are not under SNC Mode, cant execute in NUMA node\n");

            rx_mbuf_pools_array.resize(4);

            //*** Setup DDR0 Pool */
            char rx_pool_name_ddr[] = "MBUF_POOL_RX_DDR0";
            uint64_t rx_num_mbuf_ddr = mbuf_count_calculation(rx_ring_size_ddr, rx_lcore_count);
            rx_mbuf_pools_array[0] = rte_pktmbuf_pool_create_by_ops(rx_pool_name_ddr,
                                                                  rx_num_mbuf_ddr,
                                                                  0,
                                                                  0,
                                                                  mbuf_size,
                                                                  0, "stack");      //Numa0 or SubNUMA0 all the time  

            //*** Setup DDR1-3 Pool */

            uint64_t rx_num_mbuf_ddr123 = mbuf_count_calculation(second_ring_size, rx_lcore_count);   //!We don't allocate TX on CXL rings

            numa_pool_allocation_thread_finish.store(0);
            struct numa_allocation_thread_args numa_1 = {"MBUF_POOL_RX_DDR1", 1, rx_num_mbuf_ddr123};
            pthread_create(&(numa_pool_allocation_thread[0]), NULL, numa_pool_allocation, (void*)&numa_1);
            struct numa_allocation_thread_args numa_2 = {"MBUF_POOL_RX_DDR2", 2, rx_num_mbuf_ddr123};
            pthread_create(&(numa_pool_allocation_thread[1]), NULL, numa_pool_allocation, (void*)&numa_2);
            struct numa_allocation_thread_args numa_3 = {"MBUF_POOL_RX_DDR3", 3, rx_num_mbuf_ddr123};
            pthread_create(&(numa_pool_allocation_thread[2]), NULL, numa_pool_allocation, (void*)&numa_3);
            while(numa_pool_allocation_thread_finish.load() != 3);

            //*** MBuf Setup Check */
            assert(rx_num_mbuf_ddr123 > MBUF_CACHE_SIZE && rx_num_mbuf_ddr > MBUF_CACHE_SIZE && 
                "Please make sure the total size of a single MBuf is at least bigger than Mbuf_cache_size");
            for (uint64_t i = 0; i < rx_mbuf_pools_array.size(); i++){
                if (!rx_mbuf_pools_array[i])
                    RTE_EXIT_PRINT(EXIT_FAILURE, "Cannot create mbuf pool @%lu, Errno: %s\n", i, rte_strerror(rte_errno));
                else
                    printf("Created mbuf pool %s, size %u\n", rx_mbuf_pools_array[i]->name, rx_mbuf_pools_array[i]->size);
            }

            //*** Rings Setup */
            uint16_t nb_txd = LATENCY_REPORT_TX_RING_SIZE;
            uint16_t nb_rxd_first = rx_ring_size_ddr;
            uint16_t nb_rxd_second = second_ring_size;
            setup_eth_dev(rx_lcore_count * 4, rx_lcore_count, &nb_txd, &nb_rxd_first, &nb_rxd_second);

            for (uint64_t q = 0; q < rx_lcore_count; q++) {
                retval  = rte_eth_tx_queue_setup(port_id, q, nb_txd, SOCKET_ID_ANY, NULL);

                retval = (retval != 0) ? retval : rte_eth_rx_queue_setup(port_id, q, rx_ring_size_ddr, 0, NULL, rx_mbuf_pools_array[DDR_IDX]);                          
                retval = (retval != 0) ? retval : rte_eth_rx_queue_setup(port_id, q + rx_lcore_count, second_ring_size, 1, NULL, rx_mbuf_pools_array[NUMA1_IDX]);         
                retval = (retval != 0) ? retval : rte_eth_rx_queue_setup(port_id, q + rx_lcore_count * 2, second_ring_size, 2, NULL, rx_mbuf_pools_array[NUMA2_IDX]);          
                retval = (retval != 0) ? retval : rte_eth_rx_queue_setup(port_id, q + rx_lcore_count * 3, second_ring_size, 3, NULL, rx_mbuf_pools_array[NUMA3_IDX]);

                ddr_rx_ids.push_back(q);
                second_rx_ids.push_back(q + rx_lcore_count);
                second_rx_ids.push_back(q + rx_lcore_count * 2);
                second_rx_ids.push_back(q + rx_lcore_count * 3);

                if (retval < 0) 
                    RTE_EXIT_PRINT(EXIT_FAILURE, "Error during rx/tx for queue %lu, Errno: %lu\n", q, retval);
            }
            printf("Set up %hu @ %lu tx rings, %lu @ %lu DDR0 rx rings, %lu @ %lu DDR123 rx rings\n", 
                nb_txd, rx_lcore_count, rx_ring_size_ddr, rx_lcore_count, second_ring_size, rx_lcore_count);   
            std::cout << "------------------------------------------------------" << std::endl;
            break;
        }
    
        default:
            RTE_EXIT_PRINT(EXIT_FAILURE, "Invalid secondary ring mode\n");
            break;
    }

    // Start the Ethernet port.
    retval = rte_eth_dev_start(port_id);
    if (retval < 0) 
        RTE_EXIT_PRINT(EXIT_FAILURE, "Error during rte_eth_dev_start\n");


    setup_flows(ddr_rx_ids, second_rx_ids);
    setup_ring_monitors(ddr_rx_ids, second_rx_ids, rx_lcore_count);
    setup_sw_qs((rte_lcore_count() - 1) / 2);

    /***********************************************************************************/
    /******************************* Application Init **********************************/
    /***********************************************************************************/
    std::cout << "\n================= Application =================" << std::endl;
    switch(application_choice){
        case Touch: 
            waiting_time = app_arg1;
            printf("Wait for  %lu ns every 32 packets\n", waiting_time);
            break;

        case HeaderTouch:
            app_p_vec = std::vector<std::shared_ptr<dpdk_apps::BaseApp>>
                (rx_lcore_count, std::shared_ptr<dpdk_apps::BaseApp>(new dpdk_apps::HeaderTouchApp()));   
            printf("Header Touch\n");
            break;

        case NoApp:
            printf("NoApp, No FW\n");
            break;

        case KVS:   
            dpdk_apps::KVSApp::key_pool_count = app_arg1;
            dpdk_apps::KVSApp::kvs_state = {0};
            assert(app_arg1 != 0 && "KVS need to has one argument for key_pool_count");
            app_p_vec = std::vector<std::shared_ptr<dpdk_apps::BaseApp>>
                (rx_lcore_count, std::shared_ptr<dpdk_apps::BaseApp>(new dpdk_apps::KVSApp()));     
            printf("KVS, -- key_pool_count %lu\n", app_arg1);
            break;

        case Crypto:

            dpdk_apps::CryptoApp::init_engine(app_arg1_str, app_arg2_str, rx_lcore_count);
            app_p_vec = std::vector<std::shared_ptr<dpdk_apps::BaseApp>>
                (rx_lcore_count, std::shared_ptr<dpdk_apps::BaseApp>(new dpdk_apps::CryptoApp(app_arg1)));     
            printf("Crypto, -- Engine %s -- Algorithm %s\n", app_arg1_str.c_str(), app_arg2_str.c_str());
            break;

        case BM25:
            app_p_vec = std::vector<std::shared_ptr<dpdk_apps::BaseApp>>
                (rx_lcore_count, std::shared_ptr<dpdk_apps::BaseApp>(new dpdk_apps::BM25App(app_arg1)));
            printf("BM25, -- data footprint %lu\n", app_arg1);
            break;

        case KNN:
            app_p_vec = std::vector<std::shared_ptr<dpdk_apps::BaseApp>>
                (rx_lcore_count, std::shared_ptr<dpdk_apps::BaseApp>(new dpdk_apps::KnnApp(app_arg1)));
            printf("KNN, -- data footprint %lu\n", app_arg1);
            break;

        case NAT:
            app_p_vec = std::vector<std::shared_ptr<dpdk_apps::BaseApp>>
                (rx_lcore_count, std::shared_ptr<dpdk_apps::BaseApp>(new dpdk_apps::NATApp(app_arg1)));
            printf("NAT, -- data pool size %lu\n", app_arg1);
            break;

        default:
            
            printf("Internal App Error, your -a input(%u) might not be correct\n", application_choice);
            return -1;
    }
    
    /***********************************************************************************/
    /******************************** Main Workload ************************************/
    /***********************************************************************************/

    signal(SIGINT, stop_rx);
    signal(SIGTERM, stop_rx);
    signal(SIGKILL, [](int signal){printf("Receiving SIGKILL, skil cleaniing part & exiting...\n"); exit(1);});

    printf( "\033[1;33m\033[1m"
            "------------------------- Starting RX Threads -------------------------\n"
            "---------------- Main thread NOT treats as RX thread ------------------\n"
            "\033[0m"
    );

    /*For each LCORE except main*/
    unsigned lcore_id;
    RTE_LCORE_FOREACH_WORKER(lcore_id)
    {
        if (lcore_id % 2 == 0)
        {
            rte_eal_remote_launch(pipeline_poll, NULL, lcore_id);
        }
        else
        {
            rte_eal_remote_launch(pipeline_process, NULL, lcore_id);
        }
    }

    numa_pool_allocation_thread_finish.store(4);
    /***********************************************************************************/
    /********************************** Monitoring *************************************/
    /***********************************************************************************/

    uint64_t hz = rte_get_timer_hz();
    uint64_t interval_cycles = (uint64_t) (monitor_interval_ms * hz / (1000.0));

    std::cout << "=================== Misc. ====================" << std::endl;
    printf("CPU running @                       %lu MHz\n", hz/1000000);
    printf("Interval cycles:                    %lu\n", interval_cycles);
    printf("Interval:                           %ld\n", monitor_interval_ms);
    std::cout << "==============================================" << std::endl;


    uint64_t previous_monitor_timestamp = rte_get_timer_cycles();
    uint64_t monitor_loop_id = 0;
    while (likely(keep_receiving)) {

        uint64_t current_timestamp = rte_get_timer_cycles();
        if (current_timestamp - previous_monitor_timestamp < interval_cycles)
            continue;
        previous_monitor_timestamp = current_timestamp;
        
        uint64_t total_rx_pkts = 0;
        uint64_t total_ddr_rx_pkts = 0;
        uint64_t total_sec_rx_pkts = 0;
        uint64_t total_pkt_processing_cycles = 0;

        //*** Calculate RX Pkts */
        for (uint64_t i = 0; i < ddr_rx_ids.size(); i++)
        {
            auto rx_idx = ddr_rx_ids[i];
            uint64_t tmp = ring_rx_record[rx_idx];
            total_ddr_rx_pkts += tmp - ring_rx_record_snapshot[rx_idx];
            ring_rx_record_snapshot[rx_idx] = tmp;
        }
        for (uint64_t i = 0; i < second_rx_ids.size(); i++)
        {
            auto rx_idx = second_rx_ids[i];
            uint64_t tmp = ring_rx_record[rx_idx];
            total_sec_rx_pkts += tmp - ring_rx_record_snapshot[rx_idx];
            ring_rx_record_snapshot[rx_idx] = tmp;
        }
        total_rx_pkts = total_ddr_rx_pkts + total_sec_rx_pkts;

        //*** Calculate Processing Time */
        for (uint64_t i = 0; i < lcore_processing_time.size(); i++)
        {
            uint64_t tmp = lcore_processing_time[i];
            total_pkt_processing_cycles += tmp - lcore_processing_time_snapshot[i];
            lcore_processing_time_snapshot[i] = tmp;
        }

        printf("%04lu: RX packet per %0.3f second: %0.4f M\n", monitor_loop_id++, monitor_interval_ms/1000.0, total_rx_pkts/1000000.0);

        if (total_rx_pkts != 0){
            uint64_t processing_timestamp_per_pkt = total_pkt_processing_cycles/total_rx_pkts;
            double processing_time_ns = (1000000000.0 * processing_timestamp_per_pkt)/hz;
            // HARDCOED 1024 packet size here
            uint64_t bytes_us_cur = 1024 * (1000.0 / processing_time_ns);
            bytes_us = 0.125 * bytes_us_cur +  0.875 * bytes_us;
            printf("Current Consumption rate in BYTES/US: %lu \n", bytes_us);
        } else {
            printf("RX average packet processing time N/A");
        }
    }

    /***********************************************************************************/
    /****************************** Statistics and Exit ********************************/
    /***********************************************************************************/


    switch (secondary_ring_mode)
    {
        case NUMA:
            for (int i = 0; i < 3; i++) {
                pthread_join(numa_pool_allocation_thread[i], NULL);
            }
            break;
        default:
            break;
    }


    printf( "\033[1;33m\033[1m"
            "-----------------------------------------------------------------------\n"
            "-------------------------- Ending RX Threads --------------------------\n"
            "-----------------------------------------------------------------------\n"
            "\033[0m"
    );

    //*** Global Statistics */
    uint64_t total_rx_global = std::accumulate(ring_rx_record.begin(), ring_rx_record.end(), 0);
    uint64_t total_tx_global = std::accumulate(lcore_tx_record.begin(), lcore_tx_record.end(), 0);

    std::cout << "============= Global Statistics ==============" << std::endl;
    printf("Total RX Pkts:                      %lu \n", total_rx_global);
    printf("Total TX Pkts:                      %lu \n", total_tx_global);
    std::cout << "==============================================" << std::endl;

    //*** Ring Statistics */
    for (uint64_t i = 0; i < ddr_rx_ids.size(); i++)
    {
        auto rx_idx = ddr_rx_ids[i];
        float percentage = total_rx_global ? (ring_rx_record[rx_idx] * 100.0 / total_rx_global) : (0.0);
        printf("DDR-RX ring %2d receives %10lu packets(%0.3f%%)\n", rx_idx, ring_rx_record[rx_idx], percentage);
    }

    for (uint64_t i = 0; i < second_rx_ids.size(); i++)
    {
        auto rx_idx = second_rx_ids[i];
        float percentage = total_rx_global ? (ring_rx_record[rx_idx] * 100.0 / total_rx_global) : (0.0);
        printf("Second-RX ring %2d receives %10lu packets(%0.3f%%)\n", rx_idx, ring_rx_record[rx_idx], percentage);
    }
    std::cout << "==============================================" << std::endl;

    /******************* App Stats *******************/
    if (application_choice != Touch && 
        application_choice != NoApp) {
        assert(!app_p_vec.empty() && app_p_vec.size() == rx_lcore_count && app_p_vec.size() >= 1);
        std::cout << app_p_vec[0]->print_stats() << std::endl;
    } else {
        printf("No App Stats\n");
    }


    /******************* Exiting *******************/
    printf("Trying to stop all working threads\n");
    ret = rte_eth_dev_stop(port_id);
    if (ret != 0)
        printf("\033[1;33m\033[1m" "rte_eth_dev_stop: err=%ld, port=%ld\n" "\033[0m", ret, port_id);

    ret = rte_eth_dev_close(port_id);
    if (ret != 0)
        printf("\033[1;33m\033[1m" "rte_eth_dev_close: err=%ld, port=%ld\n" "\033[0m", ret, port_id);

    // Clean up the EAL
    ret = rte_eal_cleanup();
    if (ret != 0)
        printf("\033[1;33m\033[1m" "rte_eal_cleanup: err=%ld\n" "\033[0m", ret);

    bring_down_interface(if_name);

    printf("Exit Successfully\n");
    return EXIT_SUCCESS;
}

/****** App Static Specific *******/
dpdk_apps::KVSApp::_kvs_state dpdk_apps::KVSApp::kvs_state = {0};
uint64_t dpdk_apps::KVSApp::key_pool_count = 0;

ENGINE* dpdk_apps::CryptoApp::engine = nullptr;
std::vector<RSA*> dpdk_apps::CryptoApp::rsa = {};
dpdk_apps::CryptoApp::ALGO dpdk_apps::CryptoApp::algo = dpdk_apps::CryptoApp::NONE;
