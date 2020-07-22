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

enum CmdType {
    GET_RMCID = 1,
    CALL_RMC,
    LAST_CMD
};

/* Request struct */
struct CmdRequest {
    CmdType type;

    union {
        GetIdReq getid;
        CallReq call;
        LastReq last;
    } request;
};

/* Reply struct */
struct CmdReply {
    CmdType type;

    union {
        GetIdReply getid;
        CallReply call;
    } reply;
};

#endif
