#ifndef RMC_CLIENT_H
#define RMC_CLIENT_H

#include <algorithm>
#include <array>
#include <cstdint>

#include "rdma/rdmaclient.h"
#include "rmcs.h"
#include "rpc.h"

/* the size of DataReq and ExecREq without ExecReq.data */
static constexpr size_t DATAREQ_NODATA_SZ = sizeof(DataReq) - MAX_EXECREQ_DATA;
static constexpr size_t EXECREQ_NODATA_SZ = sizeof(ExecReq) - MAX_EXECREQ_DATA;
/* key distribution file for kvstore workload */
static constexpr char KVSTORE_DISTFILE[] = "zipf_distrib";

class HostClient {
  bool rmccready = false;
  unsigned int pending_unsig_sends = 0;
  uint32_t req_idx = 0;
  uint32_t inflight = 0;
  RMCType workload;

  RDMAClient rclient;

  /* communication with nicserver */
  std::array<DataReq, QP_MAX_2SIDED_WRS> req_slot;
  std::array<DataReply, QP_MAX_2SIDED_WRS> reply_slot;
  ibv_mr *req_buf_mr;
  ibv_mr *reply_buf_mr;

  void post_recv_reply(const DataReply *reply) {
    assert(rmccready);

    rclient.post_recv(rclient.get_ctrl_ctx(), reply, sizeof(DataReply),
                      reply_buf_mr->lkey);
  }

  void post_send_req_unsig(const DataReq *req) {
    assert(rmccready);

    rclient.post_2s_send_unsig(rclient.get_ctrl_ctx(), req,
                               DATAREQ_NODATA_SZ + req->data.exec.size,
                               req_buf_mr->lkey);
    pending_unsig_sends += 1;
  }

  /*
  if pending_unsig_sends >= 3*MAX_UNSIGNALED_SENDS
      poll wait until we get 1 completion.
  */
  void maybe_poll_sends(ibv_cq_ex *send_cq) {
    static_assert(3 * RDMAPeer::MAX_UNSIGNALED_SENDS < QP_MAX_2SIDED_WRS);
    thread_local struct ibv_wc wc;
    int ne;

    if (pending_unsig_sends >= 3 * RDMAPeer::MAX_UNSIGNALED_SENDS) {
      ne = ibv_poll_cq(ibv_cq_ex_to_cq(send_cq), 1, &wc);
      TEST_NZ(ne < 0);

      if (ne > 0) {
        TEST_NZ(wc.status != IBV_WC_SUCCESS);
        pending_unsig_sends -= RDMAPeer::MAX_UNSIGNALED_SENDS;
      }
    }
  }

  void post_send_req(const DataReq *req) {
    assert(rmccready);

    rclient.post_send(rclient.get_ctrl_ctx(), req, sizeof(DataReq),
                      req_buf_mr->lkey);
  }

  void disconnect();

  void load_send_request();
  void load_handle_reps(std::queue<long long> &start_times,
                        std::vector<uint32_t> &rtts, uint32_t polled,
                        uint32_t &rtt_idx);

  void arm_exec_req(DataReq *dst, const ExecReq *src) const {
    dst->type = DataCmdType::CALL_RMC;
    std::memcpy(&dst->data.exec, src, EXECREQ_NODATA_SZ + src->size);
  }

  void get_req_args_kvstore(uint32_t numreqs,
                            std::vector<ExecReq> &args) const {
    std::vector<uint32_t> keys;
    file_to_vec(keys, KVSTORE_DISTFILE);

    /* YCSB-B 95% gets 5% puts */
    std::vector<KVStore::RpcReqType> actions;
    uint32_t num_puts = numreqs * 0.05;
    for (auto req = 0u; req < numreqs; req++) {
      if (req <= num_puts)
        actions.push_back(KVStore::RpcReqType::PUT);
      else
        actions.push_back(KVStore::RpcReqType::GET);
    }
    shuffle_vec(actions, current_tid);

    rt_assert(keys.size() >= numreqs, "keys < numreqs");
    rt_assert(actions.size() == numreqs, "actions != numreqs");

    for (auto req = 0u; req < numreqs; req++) {
      args.emplace_back(
          ExecReq{.id = RMCType::KVSTORE, .size = sizeof(KVStore::RpcReq)});
      ExecReq &newreq = args.back();

      KVStore::RpcReq kvreq = {};
      kvreq.reqtype = actions[req];
      *(reinterpret_cast<uint32_t *>(kvreq.record.key)) = keys[req];
      std::memset(&kvreq.record.val, 2, KVStore::VAL_LEN);

      /* copy RpcReq to newreq.data */
      std::memcpy(newreq.data, &kvreq, sizeof(kvreq));
    }
  }

 public:
  // A HostClient creates one 2-sided QP to communicate to nicserver,
  // and one CQ
  HostClient(RMCType workload) : workload(workload), rclient(1, 1, false) {
    printf("sizeof(DataReq())=%lu\n", sizeof(DataReq));
    printf("sizeof(DataReply())=%lu\n", sizeof(DataReply));
  }

  void connect(const std::string &ip, const unsigned int &port);
  long long do_maxinflight(uint32_t num_reqs, const std::vector<ExecReq> &args,
                           pthread_barrier_t *barrier, uint16_t tid);
  int do_load(float load, std::vector<uint32_t> &durations, uint32_t num_reqs,
              long long freq, const std::vector<ExecReq> &args,
              pthread_barrier_t *barrier);

  /* cmd to initiate disconnect */
  void last_cmd();
  void initialize_rmc(RMCType type);
  constexpr uint32_t get_max_inflight() const { return QP_MAX_2SIDED_WRS - 1; }

  void get_req_args(uint32_t numreqs, std::vector<ExecReq> &args) const {
    switch (workload) {
      case RMCType::TRAVERSE_LL:
      case RMCType::LOCK_TRAVERSE_LL:
      case RMCType::UPDATE_LL:
        die("unsupported\n");
      case RMCType::KVSTORE:
        return get_req_args_kvstore(numreqs, args);
    }
  }
};

#endif
