#ifndef BM25_APP_H
#define BM25_APP_H

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
#include <vector>

#include "base_app.h"

namespace dpdk_apps{
    
class BM25App: public BaseApp {

private:
    
    static constexpr int DATA_RANGE = 1024 * 4;

    struct _matchinfo {
        int term_count;
        float total_docs;
        float avg_length;
        float doc_length;
        float term_frequency;
        float docs_with_term;
    } __attribute__((packed));



    std::vector<_matchinfo> states;
    size_t total_match;

public:
    BM25App(size_t footprint_size = 1024 * 16): 
        states(footprint_size),
        total_match(footprint_size)
    {
        assert(states.size() == total_match);
        printf("size of each state: %lu\n", sizeof(_matchinfo));
        int tmp = rand() % DATA_RANGE;
        for (auto& state : states) {
            state.term_count = tmp;
            state.total_docs = 0.0 + tmp;
            state.avg_length = 0.0 + tmp;
            state.doc_length = 0.0 + tmp;
            state.term_frequency = 0.0 + tmp;
            state.docs_with_term = 0.0 + tmp;
        }
    }


    void run(char* pkt_ptr, size_t len) override 
    {
        size_t num_tuples_in_pkt = len/tuple_size;
        for (int i = 0; i < num_tuples_in_pkt; i++) {

            uint8_t pkt_dummy_data = pkt_ptr[i * tuple_size];

            int fake_record = (rand() + pkt_dummy_data) % total_match;
            okapi_bm25(fake_record, 1.2, 0.75);
        }
    }


private:

    float bm25_mFast_Log2(float val) 
    {
        union { float val; int32_t x; } u = { val };
        float log_2 = (float)(((u.x >> 23) & 255) - 128);

        u.x   &= ~(255 << 23);
        u.x   += 127 << 23;
        log_2 += ((-0.3358287811f) * u.val + 2.0f) * u.val  -0.65871759316667f;

        return (log_2);
    }



    void okapi_bm25(int record, float K1, float B)
    {
        static float sum;

        for (int i = 0; i < states[record].term_count; i++) {

            float idf = bm25_mFast_Log2(states[record].total_docs - states[record].docs_with_term + 0.5) / (states[record].docs_with_term + 0.5);
            float right_side = ((states[record].term_frequency * (K1 + 1)) /
                                (states[record].term_frequency + (K1 * (1 - B + 
                                (B * (states[record].doc_length / 
                                    states[record].avg_length))))));
            sum += (idf * right_side);
        }
    }
}; // class BM25App



} // namespace dpdk_apps

#pragma GCC diagnostic pop

#endif /* BM25_APP_H */