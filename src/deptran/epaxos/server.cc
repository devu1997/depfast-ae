#include "server.h"
#include "frame.h"


namespace janus {

EpaxosServer::EpaxosServer(Frame * frame) {
  frame_ = frame;
  /* Your code here for server initialization. Note that this function is 
     called in a different OS thread. Be careful about thread safety if 
     you want to initialize variables here. */
  // TODO: REMOVE
  Log::set_level(Log::DEBUG);
  // Future Work: Update epoch on replica_set change
  curr_epoch = 0;
  cmds[replica_id_] = unordered_map<uint64_t, EpaxosCommand>();
}

EpaxosServer::~EpaxosServer() {
  /* Your code here for server teardown */

}

void EpaxosServer::Setup() {
  /* Your code here for server setup. Due to the asynchronous nature of the 
     framework, this function could be called after a RPC handler is triggered. 
     Your code should be aware of that. This function is always called in the 
     same OS thread as the RPC handlers. */

  // Future Work: Give unique replica id on restarts (by storing old replica_id persistently and new_replica_id = old_replica_id + N)
  replica_id_ = site_id_;

  // Process requests
  Coroutine::CreateRun([this](){
    while(true) {
      mtx_.lock();
      int size = reqs.size();
      mtx_.unlock();
      // Future Work: Can make more efficient by having a pub-sub kind of thing
      if (size == 0) {
        Coroutine::Sleep(1000);
      } else {
        mtx_.lock();
        EpaxosRequest req = reqs.front();
        reqs.pop_front();
        mtx_.unlock();
        Coroutine::CreateRun([this,&req](){
          StartPreAccept(req.cmd, req.dkey, req.ballot, req.replica_id, req.instance_no, req.leader_dep_instance, false);
        });
      }
    }
  });
}

void EpaxosServer::Start(shared_ptr<Marshallable>& cmd, string dkey) {
  /* Your code here. This function can be called from another OS thread. */
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_debug("Received request in server: %d for dep_key: %s", site_id_, dkey.c_str());
  EpaxosRequest req = CreateEpaxosRequest(cmd, dkey);
  reqs.push_back(req);
}

EpaxosRequest EpaxosServer::CreateEpaxosRequest(shared_ptr<Marshallable>& cmd, string dkey) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  uint64_t instance_no = next_instance_no;
  next_instance_no = next_instance_no + 1;
  int64_t leader_dep_instance = -1;
  if (dkey_deps[dkey].count(replica_id_)) {
    leader_dep_instance = dkey_deps[dkey][replica_id_];
  }
  dkey_deps[dkey][replica_id_] = instance_no; // Important - otherwise next command may not have dependency on this command
  EpaxosBallot ballot = EpaxosBallot(curr_epoch, 0, replica_id_);
  return EpaxosRequest(cmd, dkey, ballot, replica_id_, instance_no, leader_dep_instance);
}

void EpaxosServer::StartPreAccept(shared_ptr<Marshallable>& cmd_, 
                                  string dkey, 
                                  EpaxosBallot ballot, 
                                  uint64_t replica_id,
                                  uint64_t instance_no,
                                  int64_t leader_dep_instance,
                                  bool recovery) {
  mtx_.lock();
  Log_debug("Started pre-accept for request for replica: %d instance: %d dep_key: %s with leader_dep_instance: %d ballot: %d leader: %d", 
            replica_id_, instance_no, dkey.c_str(), leader_dep_instance, ballot.ballot_no, ballot.replica_id);
  // Initialise attributes
  uint64_t seq = dkey_seq.count(dkey) ? (dkey_seq[dkey]+1) : 0;
  unordered_map<uint64_t, uint64_t> deps = dkey_deps[dkey];
  if(leader_dep_instance >= 0) {
    deps[replica_id] = leader_dep_instance;
  } else {
    deps.erase(replica_id);
  }
  // Pre-accept command
  EpaxosCommand cmd(cmd_, dkey, seq, deps, ballot, EpaxosCommandState::PRE_ACCEPTED);
  cmds[replica_id][instance_no] = cmd;
  // Update internal atributes
  if (cmd_.get()->kind_ != NO_OP_KIND) {
    dkey_seq[dkey] = cmd.seq;
    dkey_deps[dkey][replica_id] = max(dkey_deps[dkey][replica_id], instance_no);
  }
  
  mtx_.unlock();

  auto ev = commo()->SendPreAccept(site_id_, 
                                   partition_id_, 
                                   recovery,
                                   cmd.highest_seen.epoch, 
                                   cmd.highest_seen.ballot_no, 
                                   cmd.highest_seen.replica_id, 
                                   replica_id,
                                   instance_no, 
                                   cmd.cmd, 
                                   cmd.dkey, 
                                   cmd.seq, 
                                   cmd.deps);
  ev->Wait(100000);
  Log_debug("Started pre-accept reply processing for replica: %d instance: %d dep_key: %s with leader_dep_instance: %d ballot: %d leader: %d", 
            replica_id_, instance_no, dkey.c_str(), leader_dep_instance, ballot.ballot_no, ballot.replica_id);

  switch(ev->status) {
    case EpaxosPreAcceptQuorumEventStatus::FAST_PATH_QUORUM:
      StartCommit(replica_id, instance_no);
      break;
    case EpaxosPreAcceptQuorumEventStatus::SLOW_PATH_QUORUM:
      if (cmd_.get()->kind_ != NO_OP_KIND) {
        UpdateAttributes(ev->replies, replica_id, instance_no);
      }
      StartAccept(replica_id, instance_no);
      break;
    default:
      UpdateHighestSeenBallot(ev->replies, replica_id, instance_no);
  }
}

EpaxosPreAcceptReply EpaxosServer::OnPreAcceptRequest(shared_ptr<Marshallable>& cmd_, 
                                                      string dkey, 
                                                      EpaxosBallot ballot, 
                                                      uint64_t seq_, 
                                                      unordered_map<uint64_t, uint64_t> deps_, 
                                                      uint64_t replica_id, 
                                                      uint64_t instance_no) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_debug("Received pre-accept request for replica: %d instance: %d dep_key: %s with ballot: %d leader: %d", 
            replica_id_, instance_no, dkey.c_str(), ballot.ballot_no, ballot.replica_id);
  EpaxosPreAcceptStatus status = EpaxosPreAcceptStatus::IDENTICAL;
  // Reject older ballots
  if (cmds[replica_id].count(instance_no) && !ballot.isGreater(cmds[replica_id][instance_no].highest_seen)) {
    status = EpaxosPreAcceptStatus::FAILED;
    EpaxosBallot &highest_seen = cmds[replica_id][instance_no].highest_seen;
    EpaxosPreAcceptReply reply(status, highest_seen.epoch, highest_seen.ballot_no, highest_seen.replica_id);
    return reply;
  }
  /* - If already accepted then you can still overwrite it. If it was committed in some other replica then 
     it will never come to pre-accept phase again and instead go to accept phase
     - If already committed or executed then it is just an delayed message so fail it (same as ignore) */
  if (cmds[replica_id].count(instance_no) && (cmds[replica_id][instance_no].state == EpaxosCommandState::COMMITTED 
                                              || cmds[replica_id][instance_no].state == EpaxosCommandState::EXECUTED)) {
    status = EpaxosPreAcceptStatus::FAILED;
    cmds[replica_id][instance_no].highest_seen = ballot; // Verify: needed?
    EpaxosPreAcceptReply reply(status, ballot.epoch, ballot.ballot_no, ballot.replica_id);
    return reply;
  }
  // Initialise attributes
  uint64_t seq = dkey_seq.count(dkey) ? (dkey_seq[dkey]+1) : 0;
  seq = max(seq, seq_);
  if (seq != seq_) {
    status = EpaxosPreAcceptStatus::NON_IDENTICAL;
  }
  auto deps = deps_;
  for (auto itr : dkey_deps[dkey]) {
    uint64_t dreplica_id = itr.first;
    uint64_t dinstance_no = itr.second;
    if (dreplica_id == replica_id) continue;
    if (dinstance_no > deps[dreplica_id]) {
      deps[dreplica_id] = dinstance_no;
      status = EpaxosPreAcceptStatus::NON_IDENTICAL;
    }
  }
  // Pre-accept command
  cmds[replica_id][instance_no] = EpaxosCommand(cmd_, dkey, seq, deps, ballot, EpaxosCommandState::PRE_ACCEPTED);
  // Update internal attributes
  if (cmd_.get()->kind_ != NO_OP_KIND) {
    dkey_seq[dkey] = cmds[replica_id][instance_no].seq;
    int64_t leader_dep_instance = max(dkey_deps[dkey][replica_id], instance_no);
    dkey_deps[dkey] = cmds[replica_id][instance_no].deps;
    dkey_deps[dkey][replica_id] = leader_dep_instance;
  }
  // Reply
  EpaxosPreAcceptReply reply(status, ballot.epoch, ballot.ballot_no, ballot.replica_id, seq, deps);
  return reply;
}

void EpaxosServer::StartAccept(uint64_t replica_id, uint64_t instance_no) {
  mtx_.lock();
  // Accept command
  Log_debug("Started accept request for replica: %d instance: %d", replica_id_, instance_no);
  cmds[replica_id][instance_no].state = EpaxosCommandState::ACCEPTED;
  EpaxosCommand cmd = cmds[replica_id][instance_no];
  mtx_.unlock();
  
  auto ev = commo()->SendAccept(site_id_, 
                                partition_id_, 
                                cmd.highest_seen.epoch, 
                                cmd.highest_seen.ballot_no, 
                                cmd.highest_seen.replica_id, 
                                replica_id,
                                instance_no, 
                                cmd.cmd, 
                                cmd.dkey, 
                                cmd.seq, 
                                cmd.deps);
  ev->Wait(100000);
  Log_debug("Started accept reply processing for replica: %d instance: %d", replica_id_, instance_no);

  if (ev->Yes()) {
    StartCommit(replica_id, instance_no);
  } else {
    UpdateHighestSeenBallot(ev->replies, replica_id, instance_no);
  }
}

EpaxosAcceptReply EpaxosServer::OnAcceptRequest(shared_ptr<Marshallable>& cmd_, 
                                                string dkey, 
                                                EpaxosBallot ballot, 
                                                uint64_t seq,
                                                unordered_map<uint64_t, uint64_t> deps, 
                                                uint64_t replica_id, 
                                                uint64_t instance_no) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_debug("Received accept request for replica: %d instance: %d", replica_id_, instance_no);
  // Reject older ballots
  if (cmds[replica_id].count(instance_no) && !ballot.isGreater(cmds[replica_id][instance_no].highest_seen)) {
    EpaxosBallot &highest_seen = cmds[replica_id][instance_no].highest_seen;
    EpaxosAcceptReply reply(false, highest_seen.epoch, highest_seen.ballot_no, highest_seen.replica_id);
    return reply;
  }
  // Accept command
  /* - If already accepted then you can still overwrite it. If it was committed in some other replica then 
     the current_cmd == committed_cmd' and attr == attr'
     - If committed then MA(cmd', attr'). If MA(cmd', attr') and ballot is higher then the current cmd == cmd' and attr == attr' */
  if (cmds[replica_id].count(instance_no) && (cmds[replica_id][instance_no].state == EpaxosCommandState::COMMITTED 
                                              || cmds[replica_id][instance_no].state == EpaxosCommandState::EXECUTED)) {
    cmds[replica_id][instance_no].highest_seen = ballot;
  } else {
    cmds[replica_id][instance_no] = EpaxosCommand(cmd_, dkey, seq, deps, ballot, EpaxosCommandState::ACCEPTED);
    // Update internal attributes
    if (cmd_.get()->kind_ != NO_OP_KIND) {
      UpdateInternalAttributes(dkey, seq, deps);
    }
  }
  // Reply
  EpaxosAcceptReply reply(true, ballot.epoch, ballot.ballot_no, ballot.replica_id);
  return reply;
}

void EpaxosServer::StartCommit(uint64_t replica_id, uint64_t instance_no) {
  mtx_.lock();
  Log_debug("Started commit request for replica: %d instance: %d", replica_id_, instance_no);
  // Commit command
  cmds[replica_id][instance_no].state = EpaxosCommandState::COMMITTED;
  EpaxosCommand cmd = cmds[replica_id][instance_no];
  mtx_.unlock();
  
  // TODO: REMOVE
  app_next_(*cmd.cmd);
  // TODO: reply to client
  
  auto ev = commo()->SendCommit(site_id_, 
                                partition_id_, 
                                cmd.highest_seen.epoch, 
                                cmd.highest_seen.ballot_no, 
                                cmd.highest_seen.replica_id, 
                                instance_no, 
                                cmd.cmd, 
                                cmd.dkey, 
                                cmd.seq, 
                                cmd.deps);
  ev->Wait(100000);
  // Execute
}

void EpaxosServer::OnCommitRequest(shared_ptr<Marshallable>& cmd_, 
                                   string dkey, 
                                   EpaxosBallot ballot, 
                                   uint64_t seq,
                                   unordered_map<uint64_t, uint64_t> deps, 
                                   uint64_t replica_id, 
                                   uint64_t instance_no) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_debug("Received commit request for replica: %d instance: %d", replica_id_, instance_no);
  // Use the latest ballot
  if (cmds[replica_id].count(instance_no) && !ballot.isGreater(cmds[replica_id][instance_no].highest_seen)) {
    ballot = cmds[replica_id][instance_no].highest_seen;
  }
  // Commit command
  if (cmds[replica_id].count(instance_no) && (cmds[replica_id][instance_no].state == EpaxosCommandState::EXECUTED)) {
    return;
  }
  cmds[replica_id][instance_no] = EpaxosCommand(cmd_, dkey, seq, deps, ballot, EpaxosCommandState::COMMITTED);
  // TODO: REMOVE
  app_next_(*cmd_);
  // Update internal attributes
  if (cmd_.get()->kind_ != NO_OP_KIND) {
    UpdateInternalAttributes(dkey, seq, deps);
  }
}

void EpaxosServer::StartPrepare(uint64_t replica_id, uint64_t instance_no) {
  mtx_.lock();
  // Get ballot = highest seen ballot + 1
  EpaxosBallot ballot = EpaxosBallot(curr_epoch, 0, replica_id_);
  EpaxosBallot highest_ballot = EpaxosBallot();
  // Create prepare reply from self
  EpaxosPrepareReply self_reply(true, NO_OP_CMD, NO_OP_DKEY, 0, unordered_map<uint64_t, uint64_t>(), 0, replica_id_, 0, -1, 0);
  if (cmds[replica_id].count(instance_no)) {
    ballot.ballot_no = cmds[replica_id][instance_no].highest_seen.ballot_no;
    highest_ballot = cmds[replica_id][instance_no].highest_seen;
    self_reply = EpaxosPrepareReply(true, 
                                    cmds[replica_id][instance_no].cmd, 
                                    cmds[replica_id][instance_no].dkey, 
                                    cmds[replica_id][instance_no].seq,
                                    cmds[replica_id][instance_no].deps,
                                    cmds[replica_id][instance_no].state,
                                    replica_id_,
                                    cmds[replica_id][instance_no].highest_seen.epoch,
                                    cmds[replica_id][instance_no].highest_seen.ballot_no,
                                    cmds[replica_id][instance_no].highest_seen.replica_id);
  }
  ballot.ballot_no += 1;
  mtx_.unlock();
  
  auto ev = commo()->SendPrepare(site_id_, 
                                 partition_id_, 
                                 ballot.epoch, 
                                 ballot.ballot_no, 
                                 ballot.replica_id, 
                                 replica_id,
                                 instance_no);

  if (ev->No()) {
    UpdateHighestSeenBallot(ev->replies, replica_id, instance_no);
    return;
  }
  // Add self's reply
  ev->replies.push_back(self_reply);
  // Get set of replies with highest ballot
  vector<EpaxosPrepareReply> highest_ballot_replies;
  for (auto reply : ev->replies) {
    EpaxosBallot reply_ballot = EpaxosBallot(reply.epoch, reply.ballot_no, reply.replica_id);
    if (reply_ballot.isGreater(highest_ballot)) { // VERIFY: If 2 can have diff replica ids and cause issue
      highest_ballot = reply_ballot;
      highest_ballot_replies = vector<EpaxosPrepareReply>();
    } else {
      highest_ballot_replies.push_back(reply);
    }
  }
  // Check id the highest ballot seen is same as default ballot
  bool is_highest_ballot_default = highest_ballot.epoch == ballot.epoch 
                                    && highest_ballot.ballot_no == 0 
                                    && highest_ballot.replica_id == replica_id;
  // Get all unique commands and their counts
  vector<EpaxosRecoveryCommand> unique_cmds;
  EpaxosPrepareReply *any_preaccepted_reply = NULL;
  for (auto reply : highest_ballot_replies) {
    // Atleast one commited reply
    if (reply.cmd_state == EpaxosCommandState::COMMITTED) {
      mtx_.lock();
      cmds[replica_id][instance_no] = EpaxosCommand(reply.cmd, reply.dkey, reply.seq, reply.deps, ballot, EpaxosCommandState::PRE_ACCEPTED);
      if (reply.cmd.get()->kind_ != NO_OP_KIND) {
        UpdateInternalAttributes(reply.dkey, reply.seq, reply.deps);
      }
      mtx_.unlock();
      StartCommit(replica_id, instance_no);
      return;
    }
    // Atleast one command reply
    if (reply.cmd_state == EpaxosCommandState::ACCEPTED) {
      mtx_.lock();
      cmds[replica_id][instance_no] = EpaxosCommand(reply.cmd, reply.dkey, reply.seq, reply.deps, ballot, EpaxosCommandState::PRE_ACCEPTED);
      if (reply.cmd.get()->kind_ != NO_OP_KIND) {
        UpdateInternalAttributes(reply.dkey, reply.seq, reply.deps);
      }
      mtx_.unlock();
      StartAccept(replica_id, instance_no);
      return;
    }
    if (reply.cmd_state == EpaxosCommandState::PRE_ACCEPTED) {
      any_preaccepted_reply = &reply;
      // Checking for N/2 identical replies for default ballot
      if (is_highest_ballot_default && reply.acceptor_replica_id != replica_id) {
        bool found = false;
        for (auto cmd : unique_cmds) {
          if (cmd.cmd->kind_ == reply.cmd->kind_ && cmd.seq == reply.seq && cmd.deps == reply.deps) {
            cmd.count++;
            found = true;
            break;
          }
        }
        if (!found) {
          EpaxosRecoveryCommand rec_cmd(reply.cmd, reply.dkey, reply.seq, reply.deps, 0);
          unique_cmds.push_back(rec_cmd);
        }
      }
      if (reply.cmd.get()->kind_ != NO_OP_KIND) {
        UpdateInternalAttributes(reply.dkey, reply.seq, reply.deps);
      }
    }
  }
  for (auto rec_cmd : unique_cmds) {
    int n_total = commo()->rpc_par_proxies_[partition_id_].size();
    // N/2 identical pre-accepted replies for default ballot
    if (rec_cmd.count >= n_total/2) {
      mtx_.lock();
      cmds[replica_id][instance_no] = EpaxosCommand(rec_cmd.cmd, rec_cmd.dkey, rec_cmd.seq, rec_cmd.deps, ballot, EpaxosCommandState::PRE_ACCEPTED);
      mtx_.unlock();
      StartAccept(replica_id, instance_no);
      return;
    }
  }
  // Atleast one pre-accepted reply - start phase 1
  if (any_preaccepted_reply != NULL) {
    uint64_t leader_deps_instance = any_preaccepted_reply->deps[replica_id];
    StartPreAccept(any_preaccepted_reply->cmd, any_preaccepted_reply->dkey, ballot, replica_id, instance_no, leader_deps_instance, true);
    return;
  }
  // No pre-accepted replies - start phase 1 with NO_OP
  StartPreAccept(NO_OP_CMD, NO_OP_DKEY, ballot, replica_id, instance_no, -1, true);
}

EpaxosPrepareReply EpaxosServer::OnPrepareRequest(EpaxosBallot ballot, uint64_t replica_id, uint64_t instance_no) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  EpaxosPrepareReply reply(true, NO_OP_CMD, NO_OP_DKEY, 0, unordered_map<uint64_t, uint64_t>(), EpaxosCommandState::NOT_STARTED, replica_id_, 0, -1, 0);
  if (cmds[replica_id].count(instance_no)) {
    // Reject older ballots
    if (!ballot.isGreater(cmds[replica_id][instance_no].highest_seen)) {
      reply.status = false;
      return reply;
    }
    reply = EpaxosPrepareReply(true, 
                               cmds[replica_id][instance_no].cmd, 
                               cmds[replica_id][instance_no].dkey, 
                               cmds[replica_id][instance_no].seq,
                               cmds[replica_id][instance_no].deps,
                               cmds[replica_id][instance_no].state,
                               replica_id_,
                               cmds[replica_id][instance_no].highest_seen.epoch,
                               cmds[replica_id][instance_no].highest_seen.ballot_no,
                               cmds[replica_id][instance_no].highest_seen.replica_id);
  }
  return reply;
}

bool EpaxosServer::StartExecution(uint64_t replica_id, uint64_t instance_no) {
  if (cmds[replica_id].count(instance_no) == 0) {
    StartPrepare(replica_id, instance_no); // TODO: if failed then retry after a while
    return false;
  }
  if (cmds[replica_id][instance_no].cmd.get()->kind_ == NO_OP_KIND 
      || cmds[replica_id][instance_no].state == EpaxosCommandState::EXECUTED) {
    return true;
  }
  if (cmds[replica_id][instance_no].state != EpaxosCommandState::COMMITTED) {
    StartPrepare(replica_id, instance_no);
    return false;
  }
  if (cmds[replica_id][instance_no].state == EpaxosCommandState::COMMITTED) {
    // Graph<EpaxosInstance> graph(true);
    // shared_ptr<EpaxosInstance> cmd = make_shared<EpaxosInstance>(replica_id, instance_no);
    // graph.AddV(cmd);
    // uint64_t seq = cmds[replica_id][instance_no].seq;
    // for (auto itr : cmds[replica_id][instance_no].deps) {
    //   uint64_t dreplica_id = itr.first;
    //   uint64_t dinstance_no = itr.second;
    //   // shared_ptr<EpaxosInstance> dep = make_shared<EpaxosInstance>(EpaxosInstance(replica_id, instance_no));
    //   // graph.AddV(dep);
    //   // dep.get()->AddParentEdge(cmd, seq);
      
    // }
  }
}

template<class ClassT>
void EpaxosServer::UpdateHighestSeenBallot(vector<ClassT>& replies, uint64_t replica_id, uint64_t instance_no) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  EpaxosBallot highest_seen_ballot = cmds[replica_id][instance_no].highest_seen;
  for (auto reply : replies) {
    EpaxosBallot ballot(reply.epoch, reply.ballot_no, reply.replica_id);
    if (ballot.isGreater(highest_seen_ballot)) {
      highest_seen_ballot = ballot;
    }
  }
  cmds[replica_id][instance_no].highest_seen = highest_seen_ballot;
}

void EpaxosServer::UpdateAttributes(vector<EpaxosPreAcceptReply>& replies, uint64_t replica_id, uint64_t instance_no) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  EpaxosCommand &cmd = cmds[replica_id][instance_no];
  for (auto reply : replies) {
    cmd.seq = max(cmd.seq, reply.seq);
    dkey_seq[cmd.dkey] = max(dkey_seq[cmd.dkey], reply.seq);
    for (auto itr : reply.deps) {
      uint64_t dreplica_id = itr.first;
      uint64_t dinstance_no = itr.second;
      cmd.deps[dreplica_id] = max(cmd.deps[dreplica_id], dinstance_no);
      dkey_deps[cmd.dkey][dreplica_id] = max(dkey_deps[cmd.dkey][dreplica_id], dinstance_no);
    }
  }
}

void EpaxosServer::UpdateInternalAttributes(string dkey, uint64_t seq, unordered_map<uint64_t, uint64_t> deps) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  dkey_seq[dkey] = max(dkey_seq[dkey], seq);
  for (auto itr : deps) {
    uint64_t dreplica_id = itr.first;
    uint64_t dinstance_no = itr.second;
    dkey_deps[dkey][dreplica_id] = max(dkey_deps[dkey][dreplica_id], dinstance_no);
  }
}

/* Do not modify any code below here */

void EpaxosServer::Disconnect(const bool disconnect) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  verify(disconnected_ != disconnect);
  // global map of rpc_par_proxies_ values accessed by partition then by site
  static map<parid_t, map<siteid_t, map<siteid_t, vector<SiteProxyPair>>>> _proxies{};
  if (_proxies.find(partition_id_) == _proxies.end()) {
    _proxies[partition_id_] = {};
  }
  EpaxosCommo *c = (EpaxosCommo*) commo();
  if (disconnect) {
    verify(_proxies[partition_id_][loc_id_].size() == 0);
    verify(c->rpc_par_proxies_.size() > 0);
    auto sz = c->rpc_par_proxies_.size();
    _proxies[partition_id_][loc_id_].insert(c->rpc_par_proxies_.begin(), c->rpc_par_proxies_.end());
    c->rpc_par_proxies_ = {};
    verify(_proxies[partition_id_][loc_id_].size() == sz);
    verify(c->rpc_par_proxies_.size() == 0);
  } else {
    verify(_proxies[partition_id_][loc_id_].size() > 0);
    auto sz = _proxies[partition_id_][loc_id_].size();
    c->rpc_par_proxies_ = {};
    c->rpc_par_proxies_.insert(_proxies[partition_id_][loc_id_].begin(), _proxies[partition_id_][loc_id_].end());
    _proxies[partition_id_][loc_id_] = {};
    verify(_proxies[partition_id_][loc_id_].size() == 0);
    verify(c->rpc_par_proxies_.size() == sz);
  }
  disconnected_ = disconnect;
}

bool EpaxosServer::IsDisconnected() {
  return disconnected_;
}

} // namespace janus