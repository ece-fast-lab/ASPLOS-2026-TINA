#ifndef KNN_APP_H
#define KNN_APP_H

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


#include "base_app.h"
#include <vector>


namespace dpdk_apps{

class KnnApp: public BaseApp {

private:

    enum _category {
        KNN_TYPE1,
        KNN_TYPE2,
        KNN_TYPE3,
        KNN_TYPE4,
        KNN_TYPE5,
        
        _KNN_TOTAL
    };

    struct _knn_node {
        int x;
        int y;
        enum _category type;
    };

    struct _train_node {
        long long int dist;
        enum _category type;
    };

    static constexpr int X = 1000;
    static constexpr int Y = 1000;

    static constexpr int VAL_RANGE = (1024 * 1024);
    static constexpr int KNN_K = 4;

    int set_size;
    std::vector<_knn_node> states;

    int partition(struct _train_node *tuples,
            int first,
            int last)
    {
        long long int pivot_freq = tuples[first].dist;
        int left_marker = first + 1, right_marker = last;
        struct _train_node tmp;

        while (left_marker < right_marker) {
            while ((left_marker <= right_marker) &&
                (tuples[left_marker].dist >= pivot_freq)) {
                left_marker++;
            }
            while ((right_marker >= left_marker) &&
                (tuples[right_marker].dist <= pivot_freq)) {
                right_marker--;
            }

            if (left_marker < right_marker) {
                tmp = tuples[left_marker];
                tuples[left_marker] = tuples[right_marker];
                tuples[right_marker] = tmp;
            }
        }

        tmp = tuples[first];
        tuples[first] = tuples[right_marker];
        tuples[right_marker] = tmp;
        return right_marker;
    }

    void quick_sort(struct _train_node *tuples,
            int first,
            int last)
    {
        if (first < last) {
            int split_po = partition(tuples, first, last);
            quick_sort(tuples, first, split_po - 1);
            quick_sort(tuples, split_po + 1, last);
        }
    }

public:
    KnnApp(int footprint_size)
    :set_size(footprint_size), states(footprint_size)
    {
        printf("Size of each Knn Node: %lu, size of train_node: %lu\n", sizeof(_knn_node), sizeof(_train_node));
        for (int i = 0; i < footprint_size; i++) {
            states[i].x = rand() % VAL_RANGE;
            states[i].y = rand() % VAL_RANGE;
            states[i].type = static_cast<_category>(rand() % _KNN_TOTAL);
        }
    }

    ~KnnApp() {}

    void knn_process(int x, int y)
    {
        int i, train_nodes_freq[_KNN_TOTAL], max_freq;
        enum _category type;
        struct _train_node *train_nodes;

        train_nodes = (struct _train_node *)malloc(sizeof(struct _train_node) * 
                set_size);
        assert(train_nodes);
        memset(train_nodes_freq, 0x00, sizeof(int) * _KNN_TOTAL);


        for (i = 0; i < set_size; i++) {
            train_nodes[i].dist = (x - states[i].x) * (x - states[i].x) + 
                (y - states[i].y) * (y - states[i].y);
            train_nodes[i].type = states[i].type;
        }


        quick_sort(train_nodes, 0, set_size - 1);

        for (i = 0; i < KNN_K; i++) {
            train_nodes_freq[train_nodes[i].type]++;
        }

        max_freq = 0;
        for (i = 0; i < _KNN_TOTAL; i++) {
            if (train_nodes_freq[i] > max_freq) {
                max_freq = train_nodes_freq[i];
                type = static_cast<_category>(i);
            }
        }

        free(train_nodes);
    }

    void run(char* pkt_ptr, size_t len) override {

        size_t num_tuples_in_pkt = len/tuple_size;
        for (int i = 0; i < num_tuples_in_pkt; i++) {
            uint8_t pkt_dummy_data = pkt_ptr[i * tuple_size];
            
            int fake_x = (rand() + pkt_dummy_data) % VAL_RANGE;
            int fake_y = fake_x;

            knn_process(fake_x, fake_y);
        }
    }

};


} // namespace dpdk_apps


#pragma GCC diagnostic pop
#endif /* KNN_APP_H */
