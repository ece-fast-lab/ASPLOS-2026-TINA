#ifndef HEADER_TOUCH_H
#define HEADER_TOUCH_H


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
#include <vector>
#include <string>
#include <iostream>     
#include <sstream>  
#include <iomanip>    


#include "base_app.h"
#include "../../dpdk-tx/dpdk_exp_pkt.h"

#define SRC_PORT_SAMPLE_SIZE 8

namespace dpdk_apps{

class HeaderTouchApp: public BaseApp {

private:
    uint64_t port_ending[4] = {0, 0, 0, 0};     
    std::vector<uint64_t> src_port_samples;

public:
    HeaderTouchApp(): src_port_samples(SRC_PORT_SAMPLE_SIZE,0) {}
    ~HeaderTouchApp(){}

    void run(char* pkt_ptr, size_t len) override {

        dpdk_exp_pkt* pkt = (dpdk_exp_pkt*) pkt_ptr;
        uint16_t dst_port = ntohs(pkt->udp_hdr.dst_port);
        uint16_t src_port = ntohs(pkt->udp_hdr.src_port);

        port_ending[dst_port % 4] += 1;
        src_port_samples[src_port % SRC_PORT_SAMPLE_SIZE] += 1;

    }

    std::string print_stats() override {
        std::ostringstream out;

        out << "========== HeaderTouchApp Stats ============\n";
        out << "\033[1;33m\033[1m" << "dst_port_sample LastTwoBits: \n\033[0m"
            << " [00]-" << std::setw(13) << port_ending[0]
            << " [01]-" << std::setw(13) << port_ending[1]
            << " [10]-" << std::setw(13) << port_ending[2]
            << " [11]-" << std::setw(13) << port_ending[3] << "\n";

        out << "\033[1;33m\033[1m" << "src_port_sample LastFiveBits: \n\033[0m";
        for (uint64_t i = 0; i < src_port_samples.size(); i++) {
            out << "[" << i << "]-" << src_port_samples[i] << " \n";

        }

        out << "===========================================\n";
        return out.str();
    }

};



} // namespace dpdk_apps
#endif /* HEADER_TOUCH_H */