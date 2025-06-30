#ifndef BASE_APP_H
#define BASE_APP_H
#include <string>


namespace dpdk_apps {
class BaseApp {

protected:

    static constexpr int WORD_LEN = 15;
    struct _tuple {
        int32_t freq;
        uint32_t word[WORD_LEN];    //Don't use 1B, we need 4B alignment
    } __attribute__((packed));

    static constexpr size_t tuple_size = sizeof(_tuple);


public:

    BaseApp() {}
    ~BaseApp() {}
    virtual void run(char* pkt_ptr, size_t len) = 0;
    virtual std::string print_stats() {
        return "Nothing To Be Printed";
    };

};
} // namespace dpdk_apps


#endif /* BASE_APP_H */