#ifndef CRYPTO_APP_H
#define CRYPTO_APP_H

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

#include <openssl/engine.h>
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "base_app.h"
#include <string>
#include <unordered_map>

#define USE_ISAL 0


#if USE_ISAL == 1
#include "isa-l_crypto.h"
#include "aes_thread.c"
#endif


namespace dpdk_apps{



class CryptoApp: public BaseApp {

private:

    enum ALGO {
        NONE = 0,
        SHA256 = 1,
        AES = 2,
        RSA_ENC = 3,
        AES_DEC = 4,
        RSA_DEC = 5,

        _ALGOCOUNTS
    };

    static ENGINE *engine;
    static std::vector<RSA*> rsa;
    static ALGO algo;

    uint64_t core_id;
public:

    static void init_engine(std::string engine_id, std::string algorithm, size_t num_lcores){
        if (engine_id != "rdrand" && engine_id != "pka") {
            assert((std::string("Unknown engine id: %s\n") + engine_id).c_str() && false);
        }

        /* Load the engine */
        ENGINE_load_builtin_engines();
        engine = ENGINE_by_id(engine_id.c_str());
        if (engine == NULL) {
            assert(false &&  "Error loading engine for CryptoApp");
        }

        /* Set the engine as the default for all available algorithms */
        if (!ENGINE_set_default(engine, ENGINE_METHOD_ALL)) {
            assert(false && "Error setting default engine");
        }

        const std::unordered_map<std::string, ALGO> algo_map = {
            {"SHA256", CryptoApp::SHA256},
            {"AES", CryptoApp::AES},
            {"RSA_ENC", CryptoApp::RSA_ENC},
            {"AES_DEC", CryptoApp::AES_DEC},
            {"RSA_DEC", CryptoApp::RSA_DEC}
        };


        auto it = algo_map.find(algorithm);
        if (it == algo_map.end()) {
            assert((std::string("Unknown algorithm: ") + algorithm).c_str() && false);
        }
        algo = it->second;

        if (algo == RSA_ENC || algo == RSA_DEC) {
            /* Generate 16 RSA key pairs */
            for (int64_t i = 0; i < num_lcores; i++) {
                rsa.emplace_back(RSA_generate_key(2048, RSA_F4, NULL, NULL));
            }
        }
    }

    static void free_engine(int64_t algorithm){
        ENGINE_free(engine);

        if (algorithm == RSA_ENC || algorithm == RSA_DEC) {
            /* Free the RSA structure */
            for (int64_t i = 0; i < rsa.size(); i++) {
                RSA_free(rsa[i]);
            }
        }
    }

    CryptoApp(uint64_t core_id): core_id(core_id) {
        assert(engine && "Did not init the engine for CryptoApp");
    }

    ~CryptoApp() {
        if (core_id == 0) {
            free_engine(algo);
        }
    }

    void run(char* pkt_ptr, size_t len) override {

        switch(algo){

            case SHA256:
            {
                /* Perform SHA256 operation using the loaded engine */
                unsigned char digest[SHA256_DIGEST_LENGTH];

                SHA256_CTX ctx;
                SHA256_Init(&ctx);
                SHA256_Update(&ctx, pkt_ptr, len);
                SHA256_Final(digest, &ctx);
                break;
            }

            case AES:
            {
                /* Perform AES encryption and decryption using the loaded engine */
                unsigned char key[AES_BLOCK_SIZE];
                unsigned char iv[AES_BLOCK_SIZE];
                unsigned char encrypted[len];
                AES_KEY encrypt_key;

                /* Initialize key and IV (in a real application, use a secure random number generator) */
                memset(key, 0, sizeof(key));
                memset(iv, 0, sizeof(iv));

                /* Encrypt the text */
                AES_set_encrypt_key(key, 128, &encrypt_key);
                AES_cbc_encrypt(reinterpret_cast<const unsigned char*>(pkt_ptr), encrypted, len, &encrypt_key, iv, AES_ENCRYPT);
                break;
            }

            case RSA_ENC:
            {
                /* Perform RSA encryption and decryption using the loaded engine */
                unsigned char encrypted[len];

                /* Encrypt the text */
                int64_t encrypted_length = RSA_public_encrypt(len, reinterpret_cast<const unsigned char*>(pkt_ptr), encrypted, rsa[core_id], RSA_PKCS1_OAEP_PADDING);
                break;
            }
            
            case AES_DEC:
            {
                unsigned char key[AES_BLOCK_SIZE];
                unsigned char iv[AES_BLOCK_SIZE];
                unsigned char decrypted[len];
                AES_KEY decrypt_key;
                AES_set_decrypt_key(key, 128, &decrypt_key);
                AES_cbc_encrypt(reinterpret_cast<const unsigned char*>(pkt_ptr), decrypted, len, &decrypt_key, iv, AES_DECRYPT);
                break;
            }

            case RSA_DEC:
            {
                unsigned char decrypted[len];
                int64_t decrypted_length = RSA_private_decrypt(len, reinterpret_cast<const unsigned char*>(pkt_ptr), decrypted, rsa[core_id], RSA_PKCS1_OAEP_PADDING);
                break;
            }
        }
    } 

}; // End of class CryptoApp
} // End of namespace dpdk_apps

#pragma GCC diagnostic pop

#endif /* CRYPTO_APP_H */

