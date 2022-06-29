#ifndef RMC_CLIENT_H
#define RMC_CLIENT_H

#include <algorithm>
#include <array>
#include <cstdint>

#include "rdma/rdmaclient.h"
#include "rmcs.h"
#include "rpc.h"

class HostClient {
  bool rmccready = false;
  unsigned int pending_unsig_sends = 0;
  uint32_t req_idx = 0;
  uint32_t inflight = 0;
  RMCType workload;

  RDMAClient rclient;

  /* communication with nicserver */
  std::array<DataReq, QP_MAX_2SIDED_WRS> datareqs;
  std::array<DataReply, QP_MAX_2SIDED_WRS> datareplies;
  ibv_mr *req_buf_mr;
  ibv_mr *reply_buf_mr;

  void post_recv_reply(const DataReply *reply);
  void post_send_req_unsig(const DataReq *req);
  void maybe_poll_sends(ibv_cq_ex *send_cq);
  void post_send_req(const DataReq *req);
  DataReq *get_req(size_t req_idx);
  const DataReply *get_reply(size_t rep_idx) const;
  void disconnect();

  void load_send_request();
  void load_handle_reps(std::queue<long long> &start_times,
                        std::vector<uint32_t> &rtts, uint32_t polled,
                        uint32_t &rtt_idx);
  void arm_call_req(DataReq *req, uint32_t param) const;

 public:
  // A HostClient creates one 2-sided QP to communicate to nicserver,
  // and one CQ
  HostClient(RMCType workload) : workload(workload), rclient(1, 1, false) {
    printf("sizeof(DataReq())=%lu\n", sizeof(DataReq));
    printf("sizeof(DataReply())=%lu\n", sizeof(DataReply));
  }

  void connect(const std::string &ip, const unsigned int &port);
  long long do_maxinflight(uint32_t num_reqs, uint32_t param,
                           pthread_barrier_t *barrier, uint16_t tid);
  int do_load(float load, std::vector<uint32_t> &durations, uint32_t num_reqs,
              long long freq, uint32_t param, pthread_barrier_t *barrier);

  /* cmd to initiate disconnect */
  void last_cmd();
  void initialize_rmc(RMCType type);
  constexpr uint32_t get_max_inflight() const;
};

/* post a recv for DataReply */
inline void HostClient::post_recv_reply(const DataReply *reply) {
  assert(rmccready);

  rclient.post_recv(rclient.get_ctrl_ctx(), reply, sizeof(DataReply),
                    reply_buf_mr->lkey);
}

inline void HostClient::post_send_req(const DataReq *req) {
  assert(rmccready);

  rclient.post_send(rclient.get_ctrl_ctx(), req, sizeof(DataReq),
                    req_buf_mr->lkey);
}

inline void HostClient::post_send_req_unsig(const DataReq *req) {
  assert(rmccready);

  rclient.post_2s_send_unsig(rclient.get_ctrl_ctx(), req, sizeof(DataReq),
                             req_buf_mr->lkey);
  pending_unsig_sends += 1;
}

/*
if pending_unsig_sends >= 3*MAX_UNSIGNALED_SENDS
    poll wait until we get 1 completion.
*/
inline void HostClient::maybe_poll_sends(ibv_cq_ex *send_cq) {
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

inline DataReq *HostClient::get_req(size_t req_idx) {
  return &datareqs[req_idx];
}

inline const DataReply *HostClient::get_reply(size_t rep_idx) const {
  return &datareplies[rep_idx];
}

inline void HostClient::arm_call_req(DataReq *req, uint32_t param) const {
  req->type = DataCmdType::CALL_RMC;
  ExecReq *execreq = &req->data.exec;
  execreq->id = workload;
  *(reinterpret_cast<uint32_t *>(execreq->data)) = param;
}

constexpr uint32_t HostClient::get_max_inflight() const {
  return QP_MAX_2SIDED_WRS - 1;
}

#endif
