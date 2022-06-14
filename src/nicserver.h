#ifndef NIC_SERVER_H
#define NIC_SERVER_H

#include <cstdint>
#include <functional>
#include <unordered_map>

#include "hostserver.h"
#include "onesidedclient.h"
#include "rdma/rdmaserver.h"
#include "rpc.h"

class RMCScheduler;

class NICServer {
  friend class RMCScheduler;

  OneSidedClient &onesidedclient;
  RDMAServer &rserver;
  bool nsready;

  /* communication with client */
  std::vector<CmdRequest> req_buf;
  std::vector<CmdReply> reply_buf;
  ibv_mr *req_buf_mr;
  ibv_mr *reply_buf_mr;

  /* post an ibv recv for an incoming CmdRequest */
  void post_recv_req(CmdRequest *req);
  void post_batched_recv_req(RDMAContext &ctx, unsigned int startidx,
                             unsigned int num_reqs);
  void post_send_reply(CmdReply *reply);
  /* send a batched reply to client */
  void post_batched_send_reply(RDMAContext &ctx, CmdReply *reply);

  CmdRequest *get_req(size_t req_idx);
  CmdReply *get_reply(size_t req_idx);

 public:
  NICServer(OneSidedClient &client, RDMAServer &server)
      : onesidedclient(client), rserver(server), nsready(false) {
    req_buf.reserve(QP_MAX_2SIDED_WRS);
    reply_buf.reserve(QP_MAX_2SIDED_WRS);

    for (size_t i = 0; i < QP_MAX_2SIDED_WRS; ++i) {
      req_buf.push_back(CmdRequest());
      reply_buf.push_back(CmdReply());
      /* assume replies are successful */
      reply_buf[i].type = CmdType::CALL_RMC;
    }
  }

  void connect(const unsigned int &port);
  void start(RMCScheduler &sched, const unsigned int &clientport, uint16_t tid);
  void init(RMCScheduler &sched, uint16_t tid);
  void disconnect();
};

inline void NICServer::post_recv_req(CmdRequest *req) {
  assert(nsready);
  rserver.post_recv(rserver.get_ctrl_ctx(), req, sizeof(CmdRequest),
                    req_buf_mr->lkey);
}

inline void NICServer::post_send_reply(CmdReply *reply) {
  assert(nsready);
  rserver.post_send(rserver.get_ctrl_ctx(), reply, sizeof(CmdReply),
                    reply_buf_mr->lkey);
}

inline void NICServer::post_batched_send_reply(RDMAContext &ctx,
                                               CmdReply *reply) {
  assert(nsready);
  ctx.post_batched_send(reply, sizeof(CmdReply), reply_buf_mr->lkey);
}

inline CmdRequest *NICServer::get_req(size_t req_idx) {
  return &req_buf[req_idx];
}

inline CmdReply *NICServer::get_reply(size_t rep_idx) {
  return &reply_buf[rep_idx];
}

#endif
