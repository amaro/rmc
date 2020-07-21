#ifndef RMC_H
#define RMC_H

#define MAX_RMC_LEN 4096

typedef std::string RMC;
typedef size_t RMCId;

/* Reqs and Replies */
struct GetIdReq {
    char rmc[MAX_RMC_LEN];
};
struct GetIdReply {
    RMCId id;
};
struct CallReq {
    RMCId id; // TODO: figure out args
};
struct CallReply {
    int status;
};
struct LastReq {};
/* No LastReply */

enum RMCType {
    RMC_GET_ID = 1,
    RMC_CALL,
    RMC_LAST
};

/* Request struct */
struct RMCRequest {
    RMCType type;

    union {
        GetIdReq getid;
        CallReq call;
        LastReq last;
    } request;
};

/* Reply struct */
struct RMCReply {
    RMCType type;

    union {
        GetIdReply getid;
        CallReply call;
    } reply;
};

#endif
