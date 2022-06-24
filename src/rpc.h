#pragma once

#include "config.h"

enum RMCType : int;
typedef std::string RMC;  // TODO: needed?

struct ExecReq {
  RMCType id;
  char data[MAX_RMC_ARG_LEN + 1];
};

struct ExecReply {
  char data[MAX_RMC_REPLY_LEN + 1];
};

struct InitReq {
  RMCType id;
};

struct InitReply {
  uintptr_t start_addr;
  uint32_t length;
  uint32_t rkey;
};

struct MrReq {
  uint8_t num_mr;
  ibv_mr mrs[NUM_REG_RMC];
};

/* Datapath commands */
enum DataCmdType { INIT_RMC = 1, CALL_RMC, LAST_CMD };

/* Datapath request (e.g., execute rmc req) */
struct DataReq {
  DataCmdType type;

  union {
    InitReq init;
    ExecReq exec;
    // no req struct for LAST_CMD
  } data;
};

/* Datapath reply */
struct DataReply {
  DataCmdType type;

  union {
    InitReply init;
    ExecReply exec;
    // no reply struct for LAST_CMD
  } data;
};

enum CtrlCmdType { RDMA_MR = 1 };

/* Control path request */
struct CtrlReq {
  CtrlCmdType type;

  union {
    MrReq mr;
  } data;
};

/* Control path reply */
struct CtrlReply {
  CtrlCmdType type;

  union {
    // no reply struct for RDMA_MR
  } data;
};

// static_assert(sizeof(CmdRequest) == 64);
// static_assert(sizeof(CmdReply) == 32);
