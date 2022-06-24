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
  bool nsready = false;

  /* communication with client */
  const std::unique_ptr<DataReq[]> datareq_buf;
  const std::unique_ptr<DataReply[]> datareply_buf;
  ibv_mr *req_buf_mr;
  ibv_mr *reply_buf_mr;

  /* post an ibv recv for an incoming DataReq */
  void post_recv_req(DataReq *req);
  void post_batched_recv_req(RDMAContext &ctx, unsigned int startidx,
                             unsigned int num_reqs);
  void post_send_reply(DataReply *reply);
  /* send a batched reply to client */
  void post_batched_send_reply(RDMAContext &ctx, DataReply *reply);

  DataReq *get_req(size_t req_idx);
  DataReply *get_reply(size_t req_idx);

 public:
  NICServer(OneSidedClient &client, RDMAServer &server)
      : onesidedclient(client),
        rserver(server),
        datareq_buf(std::unique_ptr<DataReq[]>(new DataReq[QP_MAX_2SIDED_WRS])),
        datareply_buf(
            std::unique_ptr<DataReply[]>(new DataReply[QP_MAX_2SIDED_WRS])) {
    for (size_t i = 0; i < QP_MAX_2SIDED_WRS; ++i) {
      /* TODO: assumes replies are successful ???*/
      datareply_buf[i].type = DataCmdType::CALL_RMC;
    }
  }

  void connect(const unsigned int &port);
  void start(RMCScheduler &sched, const unsigned int &clientport, uint16_t tid);
  void init(RMCScheduler &sched, uint16_t tid);
  void disconnect();
};

inline void NICServer::post_recv_req(DataReq *req) {
  assert(nsready);
  rserver.post_recv(rserver.get_ctrl_ctx(), req, sizeof(DataReq),
                    req_buf_mr->lkey);
}

inline void NICServer::post_send_reply(DataReply *reply) {
  assert(nsready);
  rserver.post_send(rserver.get_ctrl_ctx(), reply, sizeof(DataReply),
                    reply_buf_mr->lkey);
}

inline void NICServer::post_batched_send_reply(RDMAContext &ctx,
                                               DataReply *reply) {
  assert(nsready);
  ctx.post_batched_send(reply, sizeof(DataReply), reply_buf_mr->lkey);
}

inline DataReq *NICServer::get_req(size_t req_idx) {
  return &datareq_buf[req_idx];
}

inline DataReply *NICServer::get_reply(size_t rep_idx) {
  return &datareply_buf[rep_idx];
}

#endif
