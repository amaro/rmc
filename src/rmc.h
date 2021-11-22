#ifndef RMC_H
#define RMC_H

static constexpr const unsigned int MB = 1 * 1024 * 1024;
static constexpr const uint16_t PAGE_SIZE = 4096;
static constexpr const long RMCK_TOTAL_BUFF_SZ = 1024 * MB;
/* reserved space for e.g. locks */
static constexpr const long RMCK_RESERVED_BUFF_SZ = 16 * MB;
static constexpr const long RMCK_APPS_BUFF_SZ =
    RMCK_TOTAL_BUFF_SZ - RMCK_RESERVED_BUFF_SZ;
/* to store the actual rmc being queried */
static constexpr const unsigned MAX_RMC_PROG_LEN = 56;
/* rmc arguments */
static constexpr const unsigned MAX_RMC_ARG_LEN = 16;
/* to store RMC reply results */
static constexpr const unsigned MAX_RMC_REPLY_LEN = 16;

typedef std::string RMC;
typedef size_t RMCId;

/* Reqs and Replies */
struct GetIdReq {
  char rmc[MAX_RMC_PROG_LEN];
};
struct GetIdReply {
  RMCId id;
};
struct CallReq {
  RMCId id;
  char data[MAX_RMC_ARG_LEN + 1];
};
struct CallReply {
  char data[MAX_RMC_REPLY_LEN + 1];
};
struct SetRDMAMrReq {
  ibv_mr mr;
};

enum CmdType { GET_RMCID = 1, CALL_RMC, SET_RDMA_MR, LAST_CMD };

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

enum Workload { READ, READ_LOCK, WRITE, HASHTABLE, SHAREDLOG };

static_assert(sizeof(CmdRequest) == 64);
static_assert(sizeof(CmdReply) == 32);

#endif
