#pragma once

#include "config.h"

enum class RMCType : int;

struct ExecReq {
  RMCType id;
  uint8_t size;
  uint8_t data[MAX_EXECREQ_DATA];
};

struct InitReq {
  RMCType id;
};

struct InitReply {
  uintptr_t rbaseaddr;
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
  uint8_t size;
  uint8_t data[MAX_RMC_REPLY_LEN];
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

namespace TraverseLL {
struct RpcReq {
  int num_nodes;
};
}  // namespace TraverseLL

namespace KVStore {
static constexpr uint8_t KEY_LEN = 30;
static constexpr uint8_t VAL_LEN = 100;

struct Record {
  uint8_t key[KEY_LEN];
  uint8_t val[VAL_LEN];
};

enum class RpcReqType { GET, PUT };

struct RpcReq {
  RpcReqType reqtype;
  Record record;
};
}  // namespace KVStore

namespace TAO {
static constexpr uint8_t OBJ_KEY_LEN = 8;
static constexpr uint8_t ASSOC_TYPE_LEN = 2;
/* 4 byte otype, 8 byte version, 4 byte update time, 16 byte payload, all 0 */
static constexpr uint8_t OBJ_VAL_LEN = 32;
/* 8 byte objid 1, 2 byte association type (all 0), 8 byte objid 2 = 18 */
static constexpr uint8_t ASSOC_KEY_LEN = OBJ_KEY_LEN * 2 + ASSOC_TYPE_LEN;
/* 22 byte value, all 0 */
static constexpr uint8_t ASSOC_VAL_LEN = 22;

enum class RpcReqType { GET, GET_ASSOC };

struct RpcReq {
  RpcReqType reqtype;

  union {
    uint8_t get_key[OBJ_KEY_LEN];
    uint8_t get_assoc_key[ASSOC_KEY_LEN];
  } params;
};
}  // namespace TAO
