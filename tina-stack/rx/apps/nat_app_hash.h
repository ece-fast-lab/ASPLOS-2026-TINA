#ifndef NAT_APP_H
#define NAT_APP_H

#pragma GCC diagnostic push

// disable all warnings in this file
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>
#include "uthash.h"

#include "base_app.h"
#include "../../dpdk-tx/dpdk_exp_pkt.h"


namespace dpdk_apps{

class NATApp: public BaseApp {

private:

    struct _nat_entry {
        uint32_t internal_ip;
        uint32_t external_ip;
        UT_hash_handle hh;
    };

    struct _nat_entry *mydb = nullptr;
    uint64_t num_records;

    uint64_t num_hits = 0;
    uint64_t num_misses = 0;

public:

    NATApp(uint64_t num_records): num_records(num_records)
    {
        struct _nat_entry *entry;
        printf("Size of each nat_entry: %lu, total datasize: %lu\n", 
            sizeof(struct _nat_entry), num_records * sizeof(struct _nat_entry));

        for(int i = 0; i < num_records; i++) {
            entry = (struct _nat_entry*)malloc(sizeof(struct _nat_entry));
            entry->internal_ip = rand() % 4294967295 + 1; // Random 32-bit integer
            entry->external_ip = rand() % 4294967295 + 1; // Random 32-bit integer
            HASH_ADD_INT(mydb, external_ip, entry);
        }
    }

    ~NATApp() {
        struct _nat_entry *current_entry, *tmp;

        HASH_ITER(hh, mydb, current_entry, tmp) {
            HASH_DEL(mydb, current_entry);
            free(current_entry);
        }
    }

    void run(char* pkt_ptr, size_t len) override {


        struct _nat_entry *entry;
        dpdk_exp_pkt *pkt = (dpdk_exp_pkt*)pkt_ptr;

        uint32_t external_ip = pkt->ipv4_hdr.src_addr;

        HASH_FIND_INT(mydb, &external_ip, entry);

        if (entry) {
            pkt->ipv4_hdr.src_addr = entry->internal_ip;
            num_hits++;
        } else {
            num_misses++;
        }

    }

    std::string print_stats() override {
        std::ostringstream oss;
        oss << "============ NAT APP STATS ============\n" << "Hits: " << num_hits << " Misses: " << num_misses << "\n";
        return  oss.str();
    }
};


} // namespace dpdk_apps

#pragma GCC diagnostic pop
#endif /* NAT_APP_H */

