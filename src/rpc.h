#pragma once

#include "config.h"

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

static_assert(sizeof(CmdRequest) == 64);
static_assert(sizeof(CmdReply) == 32);
