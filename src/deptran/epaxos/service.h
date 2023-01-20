#pragma once

#include "__dep__.h"
#include "epaxos_rpc.h"
#include "server.h"
#include "macros.h"
// #include "constants.h"
// #include "../rcc/graph.h"
// #include "../rcc/graph_marshaler.h"
// #include "../command.h"
// #include "../procedure.h"
// #include "../command_marshaler.h"


namespace janus {

class TxLogServer;
class EpaxosServer;
class EpaxosServiceImpl : public EpaxosService {
 public:
  EpaxosServer* svr_;
  EpaxosServiceImpl(TxLogServer* sched);

  RpcHandler(PreAccept, 15,
             const epoch_t&, epoch,
             const ballot_t&, ballot_no,
             const uint64_t&, ballot_replica_id,
             const uint64_t&, leader_replica_id,
             const uint64_t&, instance_no,
             const MarshallDeputy&, md_cmd,
             const string&, dkey,
             const uint64_t&, seq,
             const unordered_map_uint64_uint64_t&, deps,
             status_t*, status,
             epoch_t*, highest_seen_epoch,
             ballot_t*, highest_seen_ballot_no,
             uint64_t*, highest_seen_replica_id,
             uint64_t*, updated_seq,
             unordered_map_uint64_uint64_t*, updated_deps) {
    *status = EpaxosPreAcceptStatus::FAILED;
    *highest_seen_epoch = 0;
    *highest_seen_ballot_no = -1;
    *highest_seen_replica_id = 0;
    *updated_seq = 0;
    *updated_deps = unordered_map<uint64_t, uint64_t>();
  }

  RpcHandler(Accept, 13,
             const epoch_t&, epoch,
             const ballot_t&, ballot_no,
             const uint64_t&, ballot_replica_id,
             const uint64_t&, leader_replica_id,
             const uint64_t&, instance_no,
             const MarshallDeputy&, md_cmd,
             const string&, dkey,
             const uint64_t&, seq,
             const unordered_map_uint64_uint64_t&, deps,
             bool_t*, status,
             epoch_t*, highest_seen_epoch,
             ballot_t*, highest_seen_ballot_no,
             uint64_t*, highest_seen_replica_id) {
    *status = false;
    *highest_seen_epoch = 0;
    *highest_seen_ballot_no = -1;
    *highest_seen_replica_id = 0;
  }

  RpcHandler(Commit, 9,
             const epoch_t&, epoch,
             const ballot_t&, ballot_no,
             const uint64_t&, replica_id,
             const uint64_t&, instance_no,
             const MarshallDeputy&, md_cmd,
             const string&, dkey,
             const uint64_t&, seq,
             const unordered_map_uint64_uint64_t&, deps,
             bool_t*, status) {
    *status = false;
  }

  RpcHandler(Prepare, 15,
             const epoch_t&,epoch,
             const ballot_t&, ballot_no,
             const uint64_t&, ballot_replica_id,
             const uint64_t&, leader_replica_id,
             const uint64_t&, instance_no,
             bool_t*, status,
             MarshallDeputy*, md_cmd,
             string*, dkey,
             uint64_t*, seq,
             unordered_map_uint64_uint64_t*, deps,
             status_t*, cmd_state,
             uint64_t*, acceptor_replica_id,
             epoch_t*, highest_seen_epoch,
             ballot_t*, highest_seen_ballot_no,
             uint64_t*, highest_seen_replica_id) {
    *status = false;
    *md_cmd = MarshallDeputy(make_shared<Marshallable>(10));
    *dkey = "";
    *seq = 0;
    *deps = unordered_map<uint64_t, uint64_t>();
    *cmd_state = EpaxosCommandState::NOT_STARTED;
    *acceptor_replica_id = 0;
    *highest_seen_epoch = 0;
    *highest_seen_ballot_no = -1;
    *highest_seen_replica_id = 0;
  }
  
};

} // namespace janus