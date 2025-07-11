#ifndef KVS_APP_H
#define KVS_APP_H


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


namespace dpdk_apps{

class KVSApp: public BaseApp {

private:
    static constexpr uint64_t WORD_LEN = 2048;
    static constexpr uint64_t WARM_UP_ROUND = 10000;

    struct entry {
        int64_t id;
        char value[WORD_LEN];
        int64_t len;
        uint64_t num_gets;
        uint64_t num_sets;
        UT_hash_handle hh;
    };

public:

    struct _kvs_state {
        struct entry *mydb;
        pthread_mutex_t lock;
    };

    static uint64_t key_pool_count;
    static _kvs_state kvs_state;        //Shared with all KVS threads
    uint64_t dummy_value;

    KVSApp(){
        assert(key_pool_count != 0);

        for (uint64_t i = 0; i < key_pool_count; i++) {
            struct entry *e = (struct entry *)malloc(sizeof(struct entry));
            e->id = i;
            e->num_gets = 0;
            e->num_sets = 0;
            e->len = WORD_LEN;
            memset(e->value, 0, WORD_LEN);
            HASH_ADD_INT(kvs_state.mydb, id, e);
        }

    }

    ~KVSApp() {
        struct entry *e, *tmp;
        pthread_mutex_lock(&(kvs_state.lock));
        HASH_ITER(hh, kvs_state.mydb, e, tmp) {
            HASH_DEL(kvs_state.mydb, e);
            free(e);
        }        
        pthread_mutex_unlock(&(kvs_state.lock));
    }

    //! This is the first version of KVS App
    /*
    void run(char* pkt_ptr, size_t len) override {

        int64_t key = rand() % key_pool_count;
        enum Op_Type {SET = 0, GET = 1} operation_type;
        operation_type = static_cast<Op_Type> (rand() % 2);

        assert(len <= WORD_LEN);

        struct entry *e;
        pthread_mutex_lock(&(kvs_state.lock));

        if (operation_type == SET) {
            HASH_FIND_INT(kvs_state.mydb, &key, e);
            if (!e) {
                e = (struct entry *)malloc(sizeof(struct entry));
                e->id = key;
                e->num_gets = 0;
                e->num_sets = 0;
                memcpy(e->value, pkt_ptr, len);
                e->len = len;

                HASH_ADD_INT(kvs_state.mydb, id, e);
            } else {
                memcpy(e->value, pkt_ptr, len);
                e->len = len;
            }
            e->num_sets += 1;
        } else {
            HASH_FIND_INT(kvs_state.mydb, &key, e);
            if (e)
                e->num_gets += 1;
        }

        pthread_mutex_unlock(&(kvs_state.lock));
    }
    */

   //! This is the second version of KVS App
    void run(char* pkt_ptr, size_t len) override {

        int64_t key = rand() % key_pool_count;
        enum Op_Type {SET = 0, GET = 1} operation_type;
        operation_type = static_cast<Op_Type> (rand() % 2);

        struct entry *e;
        pthread_mutex_lock(&(kvs_state.lock));


        if (operation_type == SET) {                    //! @ Set, Touch 256B Pkt
            HASH_FIND_INT(kvs_state.mydb, &key, e);
            for (int i = 0; i < 256; i+=64){
                dummy_value += pkt_ptr[i];
            }
            if (e) {
                e->num_sets += 1;
            } 
        } else {                                        //! @ Get, Touch 256B Pkt + Touch 2KB KVS Database           
            HASH_FIND_INT(kvs_state.mydb, &key, e);
            for (int i = 0; i < 256; i+=64){
                dummy_value += pkt_ptr[i];
            }
            if (e){
                e->num_gets += 1;
                for (int i = 0; i < e->len; i+=64){
                    dummy_value += e->value[i];
                }
            }
        }

        pthread_mutex_unlock(&(kvs_state.lock));
    }   


    std::string print_stats() override {
        struct entry *e, *tmp;

        uint64_t  count = 0;
        uint64_t length = 0;
        uint64_t num_gets = 0;
        uint64_t num_sets = 0;
        pthread_mutex_lock(&(kvs_state.lock));
        HASH_ITER(hh, kvs_state.mydb, e, tmp) {
            count++;
            length += e->len;
            num_gets += e->num_gets;
            num_sets += e->num_sets;

        }
        pthread_mutex_unlock(&(kvs_state.lock));
        return std::string("KVS Key Pool Size: ") + std::to_string(count)
         + std::string(" -- Average Value Length(B): ") + ((count == 0) ? "NA" : std::to_string(length / count))
         + std::string(" -- Total GETs: ") + ((count == 0) ? "NA" : std::to_string(num_gets))
         + std::string(" -- Total SETs: ") + ((count == 0) ? "NA" : std::to_string(num_sets));
    }

};


} // namespace dpdk_apps
#endif /* KVS_APP_H */