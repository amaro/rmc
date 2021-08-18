#ifndef RMC_CLIENT_H
#define RMC_CLIENT_H

#include "rdma/rdmaclient.h"
#include "rmc.h"
#include <algorithm>
#include <cstdint>

class HostClient {
  bool rmccready;
  size_t bsize;
  unsigned int pending_unsig_sends;
  uint32_t req_idx;
  uint32_t inflight;

  RDMAClient rclient;

  /* communication with nicserver */
  std::vector<CmdRequest> req_buf;
  std::vector<CmdReply> reply_buf;
  ibv_mr *req_buf_mr;
  ibv_mr *reply_buf_mr;

  void post_recv_reply(CmdReply *reply);
  void post_send_req_unsig(CmdRequest *req);
  void maybe_poll_sends(ibv_cq_ex *send_cq);
  void post_send_req(CmdRequest *req);
  CmdRequest *get_req(size_t req_idx);
  CmdReply *get_reply(size_t rep_idx);
  void disconnect();

  long long load_send_request(std::queue<long long> &start_ts);
  void load_handle_reps(std::queue<long long> &start_times,
                        std::vector<uint32_t> &rtts, uint32_t polled,
                        uint32_t &rtt_idx);
  void parse_rmc_reply(CmdReply *reply) const;
  void arm_call_req(CmdRequest *req);
  void arm_call_req(CmdRequest *req, uint32_t param);

public:
  HostClient(size_t b, unsigned int num_qps)
      : rmccready(false), bsize(b), pending_unsig_sends(0), req_idx(0),
        inflight(0), rclient(num_qps, false) {
    assert(bsize > 0);
    req_buf.reserve(bsize);
    reply_buf.reserve(bsize);
    LOG("sizeof(CmdRequest())=" << sizeof(CmdRequest));
    LOG("sizeof(CmdReply())=" << sizeof(CmdReply));

    for (size_t i = 0; i < bsize; ++i) {
      req_buf.push_back(CmdRequest());
      reply_buf.push_back(CmdReply());
    }
  }

  void connect(const std::string &ip, const unsigned int &port);

  /* 1. post recv for id
     2. send rmc to server; wait for 1.
     3. return id */
  RMCId get_rmc_id(const RMC &rmc);

  long long do_maxinflight(uint32_t num_reqs, uint32_t param);
  int do_load(float load, std::vector<uint32_t> &durations, uint32_t num_reqs,
              long long freq);
  int call_one_rmc(const RMCId &id, const size_t arg, long long &duration);

  /* cmd to initiate disconnect */
  void last_cmd();

  constexpr uint32_t get_max_inflight();
};

/* post a recv for CmdReply */
inline void HostClient::post_recv_reply(CmdReply *reply) {
  assert(rmccready);

  rclient.post_recv(rclient.get_ctrl_ctx(), reply, sizeof(CmdReply),
                    reply_buf_mr->lkey);
}

inline void HostClient::post_send_req(CmdRequest *req) {
  assert(rmccready);

  rclient.post_send(rclient.get_ctrl_ctx(), req, sizeof(CmdRequest),
                    req_buf_mr->lkey);
}

inline void HostClient::post_send_req_unsig(CmdRequest *req) {
  assert(rmccready);

  rclient.post_2s_send_unsig(rclient.get_ctrl_ctx(), req, sizeof(CmdRequest),
                             req_buf_mr->lkey);
  pending_unsig_sends += 1;
}

/*
if pending_unsig_sends >= 3*MAX_UNSIGNALED_SENDS
    poll wait until we get 1 completion.
*/
inline void HostClient::maybe_poll_sends(ibv_cq_ex *send_cq) {
  static_assert(3 * RDMAPeer::MAX_UNSIGNALED_SENDS < QP_MAX_2SIDED_WRS);
  static struct ibv_wc wc;
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

inline CmdRequest *HostClient::get_req(size_t req_idx) {
  return &req_buf[req_idx];
}

inline CmdReply *HostClient::get_reply(size_t rep_idx) {
  return &reply_buf[rep_idx];
}

inline void HostClient::parse_rmc_reply(CmdReply *reply) const {
  CallReply *callreply = &reply->reply.call;
  size_t hash = std::stoull(callreply->data);
  LOG("hash at client=" << hash);
}

inline void HostClient::arm_call_req(CmdRequest *req) {
  req->type = CmdType::CALL_RMC;
  // CallReq *callreq = &req->request.call;
  // callreq->id = id;
  // num_to_str<size_t>(arg, callreq->data, MAX_RMC_ARG_LEN);
}

inline void HostClient::arm_call_req(CmdRequest *req, uint32_t param) {
  req->type = CmdType::CALL_RMC;
  CallReq *callreq = &req->request.call;
  *(reinterpret_cast<uint32_t *>(callreq->data)) = param;
}

constexpr uint32_t HostClient::get_max_inflight() {
  return QP_MAX_2SIDED_WRS - 1;
}

#endif
