#ifndef RMC_H
#define RMC_H

/* to store the actual rmc being queried */
static const unsigned MAX_RMC_LEN = 1024;
/* to store RMC reply results */
static const unsigned MAX_RMC_REPLY_LEN = 1024;

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
    RMCId id; // TODO: add buff for args
};
struct CallReply {
    int status;
    char data[MAX_RMC_REPLY_LEN];
    size_t size;
};
struct SetRDMAMrReq {
    ibv_mr mr;
};

enum CmdType {
    GET_RMCID = 1,
    CALL_RMC,
    SET_RDMA_MR,
    LAST_CMD
};

/* Request struct */
struct CmdRequest {
    CmdType type;

    union {
        GetIdReq getid;
        CallReq call;
        SetRDMAMrReq rdma_mr;
        // no req struct for LAST_CMD
    } request;
};

/* Reply struct */
struct CmdReply {
    CmdType type;

    union {
        GetIdReply getid;
        CallReply call;
        // no reply struct for SET_RDMA_MR
        // no reply struct for LAST_CMD
    } reply;
};

#endif
