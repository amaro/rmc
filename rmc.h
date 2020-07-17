#ifndef RMC_H
#define RMC_H

#define MAX_RMC_LEN 4096

typedef std::string RMC;
typedef unsigned long long int RMCId;

struct RMCRequest {
    enum {
        RMC_GET_ID,
        RMC_CALL
    } type;

    union {
        struct GetIdReq {
            char rmc[MAX_RMC_LEN];
        };

        struct CallReq {
            char rmc[MAX_RMC_LEN];
        };
    } data;
};

struct RMCReply {
    enum {
        RMC_GET_ID,
        RMC_CALL
    } type;

    union {
        struct GetIdReply {
            RMCId id;
        };

        struct CallReply {
            int status;
        };
    } data;
};

#endif
