#ifndef NIC_SERVER_H
#define NIC_SERVER_H

#include "hostserver.h"
#include "onesidedclient.h"
#include "rdma/rdmaserver.h"
#include "rmc.h"
#include <cstdint>
#include <functional>
#include <unordered_map>

class RMCScheduler;

class NICServer {
  friend class RMCScheduler;

  OneSidedClient &onesidedclient;
  RDMAServer &rserver;

  bool nsready;
  uint32_t bsize;

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
  NICServer(OneSidedClient &client, RDMAServer &server, size_t b)
      : onesidedclient(client), rserver(server), nsready(false), bsize(b) {
    assert(bsize > 0);
    req_buf.reserve(bsize);
    reply_buf.reserve(bsize);

    for (size_t i = 0; i < bsize; ++i) {
      req_buf.push_back(CmdRequest());
      reply_buf.push_back(CmdReply());
      /* assume replies are successful */
      reply_buf[i].type = CmdType::CALL_RMC;
    }
  }

  void connect(const unsigned int &port);
  void start(RMCScheduler &sched, const std::string &hostaddr,
             const unsigned int &hostport, const unsigned int &clientport);
  void init(RMCScheduler &sched);
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
