#ifndef DPDK_PERF_H
#define DPDK_PERF_H

#include <cstdint>
#include <string>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

// #define ENABLE_REMOTE_PERF

#define IA32_PERFEVTSEL0 0x186
#define IA32_PERFCTR0 0xc1

//0x430000: Enable USR and Kernal mode, and enable counter
#define BUILD_PCM(event, umask) (uint64_t)(0x430000 | (umask << 8) | event)
#define BUILD_PCM_CMASK(event, umask, cmask) (uint64_t)((cmask << 24) | 0x430000 | (umask << 8) | event)
#define BUILD_PCM_EXT(event, umask, umask_ext) (uint64_t)(((uint64_t)umask_ext << 32) | 0x430000 | (umask << 8) | event)

// ********************************************************************* //
// ************************** Sapphire Rapids ************************** //
// ********************************************************************* //
#define CPU_CLK_UNHALTED_REF_TSC_P      BUILD_PCM(0x3c, 0x01)
#define INST_RETIRED_ANY_P              BUILD_PCM(0xc0, 0x00)
#define TOPDOWN_SLOTS_P                 BUILD_PCM(0xa4, 0x01)

#define CYCLE_ACTIVITY_STALLS_L1D_MISS  BUILD_PCM_CMASK(0xa3, 0x0c, 0x0c)
#define CYCLE_ACTIVITY_CYCLES_L1D_MISS  BUILD_PCM_CMASK(0xa3, 0x08, 0x08)
#define CYCLE_ACTIVITY_STALLS_L2_MISS   BUILD_PCM_CMASK(0xa3, 0x05, 0x05)
#define CYCLE_ACTIVITY_CYCLES_L2_MISS   BUILD_PCM_CMASK(0xa3, 0x01, 0x01)
#define CYCLE_ACTIVITY_STALLS_L3_MISS   BUILD_PCM_CMASK(0xa3, 0x06, 0x06)
#define CYCLE_ACTIVITY_CYCLES_L3_MISS   BUILD_PCM_CMASK(0xa3, 0x10, 0x10)

#define TOPDOWN_BACKEND_BOUND_SLOTS     BUILD_PCM(0xa4, 0x02)
#define TOPDOWN_BR_MISPREDICT_SLOTS     BUILD_PCM(0xa4, 0x08)
#define TOPDOWN_BAD_SPEC_SLOTS          BUILD_PCM(0xa4, 0x04)
#define TOPDOWN_MEMORY_BOUND_SLOTS      BUILD_PCM(0xa4, 0x10)

#define L2_LINES_IN_ALL                 BUILD_PCM(0x25, 0x1F)
#define L2_LINES_OUT_NON_SILENT         BUILD_PCM(0x26, 0x02)
#define L2_LINES_OUT_SILENT             BUILD_PCM(0x26, 0x01)
#define L2_LINES_OUT_USELESS_HWPF       BUILD_PCM(0x26, 0x04)
#define L2_RQSTS_ALL_CODE_RD            BUILD_PCM(0x24, 0xE4)
#define L2_RQSTS_ALL_DEMAND_DATA_RD     BUILD_PCM(0x24, 0xE1)
#define L2_RQSTS_ALL_DEMAND_MISS        BUILD_PCM(0x24, 0x27)
#define L2_RQSTS_ALL_DEMAND_REFERENCES  BUILD_PCM(0x24, 0xE7)

#define L2_RQSTS_ALL_HWPF               BUILD_PCM(0x24, 0xF0)
#define L2_RQSTS_ALL_RFO                BUILD_PCM(0x24, 0xE2)
#define L2_RQSTS_CODE_RD_HIT            BUILD_PCM(0x24, 0xC4)
#define L2_RQSTS_CODE_RD_MISS           BUILD_PCM(0x24, 0x24)
#define L2_RQSTS_DEMAND_DATA_RD_HIT     BUILD_PCM(0x24, 0xC1)
#define L2_RQSTS_DEMAND_DATA_RD_MISS    BUILD_PCM(0x24, 0x21)
#define L2_RQSTS_HWPF_MISS              BUILD_PCM(0x24, 0x30)
#define L2_RQSTS_MISS                   BUILD_PCM(0x24, 0x3F)
#define L2_RQSTS_REFERENCES             BUILD_PCM(0x24, 0xFF)
#define L2_RQSTS_RFO_HIT                BUILD_PCM(0x24, 0xC2)
#define L2_RQSTS_RFO_MISS               BUILD_PCM(0x24, 0x22)
#define L2_RQSTS_SWPF_HIT               BUILD_PCM(0x24, 0xC8)
#define L2_RQSTS_SWPF_MISS              BUILD_PCM(0x24, 0x28)
#define L2_TRANS_L2_WB                  BUILD_PCM(0x23, 0x40)

#define MEM_INST_RETIRED_ALL_LOADS          BUILD_PCM(0xD0, 0x81)
#define MEM_LOAD_COMPLETED_L1_MISS_ANY      BUILD_PCM(0x43, 0xFD)
#define MEM_LOAD_L3_HIT_RETIRED_XSNP_FWD    BUILD_PCM(0xD2, 0x04)
#define MEM_LOAD_L3_HIT_RETIRED_XSNP_MISS   BUILD_PCM(0xD2, 0x01)
#define MEM_LOAD_L3_HIT_RETIRED_XSNP_NO_FWD BUILD_PCM(0xD2, 0x02)
#define MEM_LOAD_L3_HIT_RETIRED_XSNP_NONE   BUILD_PCM(0xD2, 0x08)
#define MEM_LOAD_L3_MISS_RETIRED_LOCAL_DRAM     BUILD_PCM(0xD3, 0x01)
#define MEM_LOAD_L3_MISS_RETIRED_REMOTE_DRAM    BUILD_PCM(0xD3, 0x02)
#define MEM_LOAD_L3_MISS_RETIRED_REMOTE_FWD     BUILD_PCM(0xD3, 0x08)
#define MEM_LOAD_L3_MISS_RETIRED_REMOTE_HITM    BUILD_PCM(0xD3, 0x04)
#define MEM_LOAD_L3_MISS_RETIRED_REMOTE_PMM     BUILD_PCM(0xD3, 0x10)
#define MEM_LOAD_MISC_RETIRED_UC                BUILD_PCM(0xD4, 0x04)
#define MEM_LOAD_RETIRED_FB_HIT                 BUILD_PCM(0xD1, 0x40)
#define MEM_LOAD_RETIRED_L1_HIT                 BUILD_PCM(0xD1, 0x01)
#define MEM_LOAD_RETIRED_L1_MISS                BUILD_PCM(0xD1, 0x08)
#define MEM_LOAD_RETIRED_L2_HIT                 BUILD_PCM(0xD1, 0x02)
#define MEM_LOAD_RETIRED_L2_MISS                BUILD_PCM(0xD1, 0x10)
#define MEM_LOAD_RETIRED_L3_HIT                 BUILD_PCM(0xD1, 0x04)
#define MEM_LOAD_RETIRED_L3_MISS                BUILD_PCM(0xD1, 0x20)

#define MEM_INST_RETIRED_ALL_STORES         BUILD_PCM(0xD0, 0x82)
#define MEM_STORE_RETIRED_L2_HIT            BUILD_PCM(0x44, 0x01)

#define SW_PREFETCH_ACCESS_ANY              BUILD_PCM(0x40, 0x0F)



constexpr std::pair<uint64_t, const char*> perfMapping[] = {
    // {TOPDOWN_SLOTS_P, "TOPDOWN_SLOTS_P"},

    // {TOPDOWN_BACKEND_BOUND_SLOTS, "TOPDOWN_BACKEND_BOUND_SLOTS"},
    // {TOPDOWN_MEMORY_BOUND_SLOTS, "TOPDOWN_MEMORY_BOUND_SLOTS"},

    // {CYCLE_ACTIVITY_STALLS_L1D_MISS, "CYCLE_ACTIVITY_STALLS_L1D_MISS"},
    // {CYCLE_ACTIVITY_CYCLES_L1D_MISS, "CYCLE_ACTIVITY_CYCLES_L1D_MISS"},
    // {CYCLE_ACTIVITY_STALLS_L2_MISS, "CYCLE_ACTIVITY_STALLS_L2_MISS"},
    // {CYCLE_ACTIVITY_CYCLES_L2_MISS, "CYCLE_ACTIVITY_CYCLES_L2_MISS"},
    // {CYCLE_ACTIVITY_STALLS_L3_MISS, "CYCLE_ACTIVITY_STALLS_L3_MISS"},
    // {CYCLE_ACTIVITY_CYCLES_L3_MISS, "CYCLE_ACTIVITY_CYCLES_L3_MISS"},

    {L2_LINES_IN_ALL, "L2_LINES_IN_ALL"},
    // {L2_RQSTS_ALL_DEMAND_DATA_RD, "L2_RQSTS_ALL_DEMAND_DATA_RD"},            //No big change
    // {L2_RQSTS_ALL_DEMAND_MISS, "L2_RQSTS_ALL_DEMAND_MISS"},                  //Increase slightly from 2200-2400 to 2400-2600
    // {L2_RQSTS_ALL_DEMAND_REFERENCES, "L2_RQSTS_ALL_DEMAND_REFERENCES"},      //No big change
    // {L2_RQSTS_ALL_HWPF, "L2_RQSTS_ALL_HWPF"},                                //???? There is prefetch?? 10-30
    // {L2_RQSTS_ALL_RFO, "L2_RQSTS_ALL_RFO"},                                  //No big changes
    // {L2_RQSTS_CODE_RD_HIT, "L2_RQSTS_CODE_RD_HIT"},                          //Zeros
    // {L2_RQSTS_CODE_RD_MISS, "L2_RQSTS_CODE_RD_MISS"},                        //Slight increase

    // {L2_RQSTS_DEMAND_DATA_RD_HIT, "L2_RQSTS_DEMAND_DATA_RD_HIT"},            //No Big Change
    // {L2_RQSTS_DEMAND_DATA_RD_MISS, "L2_RQSTS_DEMAND_DATA_RD_MISS"},          //!Slight increase 100~
    // {L2_RQSTS_HWPF_MISS, "L2_RQSTS_HWPF_MISS"},                              //!Not Zero, 
    // {L2_RQSTS_MISS, "L2_RQSTS_MISS"},                                        //!Slight increase 100~
    // {L2_RQSTS_REFERENCES, "L2_RQSTS_REFERENCES"},                            //!Slight increase 100~
    // {L2_RQSTS_RFO_HIT, "L2_RQSTS_RFO_HIT"},                                  //!Did not maintained, drop from 130~ to 60
    // {L2_RQSTS_RFO_MISS, "L2_RQSTS_RFO_MISS"},                                //!Increase 2x     
    // {L2_RQSTS_SWPF_HIT, "L2_RQSTS_SWPF_HIT"},                                //No Big changes
    // {L2_RQSTS_SWPF_MISS, "L2_RQSTS_SWPF_MISS"},                              //!~200 during NUMA123 Fetch, 0 other times
    // {L2_TRANS_L2_WB, "L2_TRANS_L2_WB"},                                      //!Increase 20x after NUMA123 Fetch                


    // {L2_LINES_OUT_SILENT, "L2_LINES_OUT_SILENT"},                            //!Decrease 20x after NUMA123 Fetch
    // {L2_LINES_OUT_USELESS_HWPF, "L2_LINES_OUT_USELESS_HWPF"},                //Zero
    // {L2_RQSTS_ALL_CODE_RD, "L2_RQSTS_ALL_CODE_RD"},                          //Insurtcion fetch is fine
    // {L2_LINES_OUT_NON_SILENT, "L2_LINES_OUT_NON_SILENT"},                    //!Increase 20x after NUMA123 Fetch

    {MEM_INST_RETIRED_ALL_LOADS, "MEM_INST_RETIRED_ALL_LOADS"},
    // {MEM_LOAD_RETIRED_L1_HIT, "MEM_LOAD_RETIRED_L1_HIT"},
    // {MEM_LOAD_RETIRED_L2_HIT, "MEM_LOAD_RETIRED_L2_HIT"},
    // {MEM_LOAD_RETIRED_L3_HIT, "MEM_LOAD_RETIRED_L3_HIT"},
    // {MEM_LOAD_RETIRED_L1_MISS, "MEM_LOAD_RETIRED_L1_MISS"},
    {MEM_LOAD_RETIRED_L2_MISS, "MEM_LOAD_RETIRED_L2_MISS"},
    {MEM_LOAD_RETIRED_L3_MISS, "MEM_LOAD_RETIRED_L3_MISS"},
    // {MEM_LOAD_L3_MISS_RETIRED_REMOTE_DRAM, "MEM_LOAD_L3_MISS_RETIRED_REMOTE_DRAM"},
    {MEM_LOAD_L3_MISS_RETIRED_LOCAL_DRAM, "MEM_LOAD_L3_MISS_RETIRED_LOCAL_DRAM"},

    // {MEM_LOAD_L3_MISS_RETIRED_REMOTE_FWD, "MEM_LOAD_L3_MISS_RETIRED_REMOTE_FWD"},
    // {MEM_LOAD_L3_MISS_RETIRED_REMOTE_HITM, "MEM_LOAD_L3_MISS_RETIRED_REMOTE_HITM"},

    {MEM_INST_RETIRED_ALL_LOADS, "MEM_INST_RETIRED_ALL_LOADS"},

};
constexpr size_t numEvents = sizeof(perfMapping) / sizeof(perfMapping[0]);

extern inline __attribute__((always_inline)) unsigned long read_perf_event(unsigned int idx)
{
   unsigned long a, d, c;

   c = idx;
   __asm__ volatile("rdpmc" : "=a" (a), "=d" (d) : "c" (c));

   return (a | (d << 32));
}

int open_perf_events(uint32_t cpu){
    std::string path = "/dev/cpu/" + std::to_string(cpu) + "/msr";
   
    std::cout << "\n==================== Perf =====================" << std::endl;
   int fd = open(path.c_str(), O_RDWR);
   if (fd < 0){
       std::cerr << "Failed to open perf events for CPU " << cpu << std::endl;
       return -1;
   }


    for (size_t i = 0; i < numEvents; i++) {
        uint64_t msr_value = perfMapping[i].first;
        uint64_t msr_addr = IA32_PERFEVTSEL0 + i;
        if (pwrite(fd, &msr_value, sizeof(msr_value), msr_addr) != sizeof(msr_value)) {
            std::cerr << "Failed to write to perf event " << perfMapping[i].second << std::endl;
            return -1;
        }
    }

    for (size_t i = 0; i < numEvents; i++) {
        uint64_t msr_addr = IA32_PERFEVTSEL0 + i;
        uint64_t msr_value;
        if (pread(fd, &msr_value, sizeof(msr_value), msr_addr) != sizeof(msr_value)) {
            std::cerr << "Failed to read from perf event " << perfMapping[i].second << std::endl;
            return -1;
        } else if (msr_value != perfMapping[i].first) {
            std::cerr << "Failed to set perf event " << perfMapping[i].second << " - "
                << std::hex << msr_value << " VS " << perfMapping[i].first << std::endl;
            return -1;
        } else {
            std::cout << "Successfully Start Perf Event: " << perfMapping[i].second << std::endl;
        }
    }

    //**** Do a Test Run

    read_perf_event(0);
    /***
     * If SegSEGV here: 
     *  It means you dont have access right to rdpmc, you can enable it by:
     *  echo 2 > /sys/devices/cpu/rdpmc
     */




    return 0;
}




#endif // DPDK_PERF_H