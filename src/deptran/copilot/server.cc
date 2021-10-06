#include "server.h"
#include "frame.h"
#include "coordinator.h"

// #define DEBUG
#define WAIT_AT_UNCOMMIT
#define REVERSE(p) (1 - (p))
#define N_CMD_KEEP (10000)

namespace janus {

const char* CopilotServer::toString(uint8_t is_pilot) {
  if (is_pilot)
    return "PILOT  ";
  else
    return "COPILOT";
}

CopilotServer::CopilotServer(Frame* frame) : log_infos_(2) {
  frame_ = frame;
  id_ = frame->site_info_->id;
  setIsPilot(frame_->site_info_->locale_id == 0);
  setIsCopilot(frame_->site_info_->locale_id == 1);

  Setup();
}

shared_ptr<CopilotData> CopilotServer::GetInstance(slotid_t slot, uint8_t is_pilot) {
  if (slot < log_infos_[is_pilot].min_active_slot && slot != 0) {
    Log_warn("server %d get freed ins %s %lu", id_, toString(is_pilot), slot);
    return nullptr;  // never re-create freed slot
  }
  auto& sp_instance = log_infos_[is_pilot].logs[slot];
  if (!sp_instance)
    sp_instance = std::make_shared<CopilotData>(
      CopilotData{nullptr,
                  0,
                  is_pilot, slot,
                  0,
                  Status::NOT_ACCEPTED,
                  0, 0});
  return sp_instance;
}

std::pair<slotid_t, uint64_t> CopilotServer::PickInitSlotAndDep() {
  
  uint64_t init_dep;
  slotid_t assigned_slot;
  /**
   * It also assigns the initial dependency for this entry,
   * which is the most recent entry (latest accepted entry)
   * from the other pilot it has seen.
   * 
   * initial slot id is 1, slot 0 is always empty
   */
  if (isPilot_) {
    init_dep = log_infos_[NO].max_accepted_slot;
    assigned_slot = ++log_infos_[YES].current_slot;
  } else if (isCopilot_) {
    init_dep = log_infos_[YES].max_accepted_slot;
    assigned_slot = ++log_infos_[NO].current_slot;
  } else {
    init_dep = 0;
    assigned_slot = 0;
  }

  Log_debug("server %d assigned %s : %lu -> %lu", id_, toString(isPilot_),
            assigned_slot, init_dep);

  return { assigned_slot, init_dep };
}

void CopilotServer::Setup() {

  log_infos_[NO] = {};
  log_infos_[YES] = {};

  GetInstance(0, YES)->status = EXECUTED;
  GetInstance(0, NO)->status = EXECUTED;
}

void CopilotServer::OnForward(shared_ptr<Marshallable>& cmd,
                              const function<void()>& cb) {
  verify(isPilot_ || isCopilot_);
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_info("This Copilot server is: %d", id_);
  rep_frame_ = frame_;

  if (!isPilot_ && !isCopilot_) {
    verify(0);
    // TODO: forward to pilot server
  }
  auto coord = (CoordinatorCopilot *)CreateRepCoord();
  coord->Submit(cmd);

  cb();
}

void CopilotServer::OnPrepare(const uint8_t& is_pilot,
                              const uint64_t& slot,
                              const ballot_t& ballot,
                              MarshallDeputy* ret_cmd,
                              ballot_t* max_ballot,
                              uint64_t* dep,
                              status_t* status,
                              const function<void()>& cb) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  auto ins = GetInstance(slot, is_pilot);

  /**
   * The fast pilot does this by sending Prepare messages with a
   * higher ballot number b' for the entry to all replicas. If b'
   * is higher than the set ballot number for that entry, the
   * replicas reply with PrepareOk messages and update their
   * prepared ballot number for that entry.
   */
  if (ins->ballot < ballot) {
    ins->ballot = ballot;
  }
  /**
   * The PrepareOk messages include the highest ballot number
   * for which a replica has fast or regular accepted an entry,
   * the command and dependency associated with that entry, and
   * an id of the dependency's proposing pilot.
   */
  *max_ballot = ins->ballot;
  verify(ins->cmd);  // TODO: occationally it fails at this point
  ret_cmd->SetMarshallable(ins->cmd);
  *dep = ins->dep_id;
  *status = ins->status;

  Log_debug(
      "server %d [PREPARE    ] %s : %lu -> %lu status %d ballot %ld",
      id_, toString(is_pilot), slot, *dep, *status, *max_ballot);

  cb();
}

void CopilotServer::OnFastAccept(const uint8_t& is_pilot,
                                 const uint64_t& slot,
                                 const ballot_t& ballot,
                                 const uint64_t& dep,
                                 shared_ptr<Marshallable>& cmd,
                                 ballot_t* max_ballot,
                                 uint64_t* ret_dep,
                                 const function<void()> &cb) {
  // TODO: deal with ballot
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_debug("server %d [FAST_ACCEPT] %s : %lu -> %lu", id_,
            toString(is_pilot), slot, dep);

  auto ins = GetInstance(slot, is_pilot);
  auto& log_info = log_infos_[REVERSE(is_pilot)];
  auto& logs = log_info.logs;
  uint64_t suggest_dep = dep;

  /**
   * Thus, the check only needs to look at later entries in the other
   * pilot’s log. The compatibility check passes unless the replica
   * has already accepted a later entry P'.k (k > j) from the other
   * pilot P0 with a dependency earlier than P.i, i.e., P'.k’s dependency
   * is < P.i.
   * 
   * 
   */
  if (dep != 0) {
    for (slotid_t j = dep + 1; j <= log_info.max_accepted_slot; j++) {
      // if (!logs[j])
      //   Log_fatal("slot %lu max acpt %lu", j, log_info.max_accepted_slot);
      // auto dep_id = logs[j]->dep_id;
      auto it = logs.find(j);
      if (it == logs.end())
        continue;
      auto dep_id = it->second->dep_id;
      if (dep_id != 0 && dep_id < slot) {
        // if (GetInstance(dep_id, is_pilot)->status == Status::EXECUTED &&
        //     logs[dep]->status == Status::EXECUTED)
        //   continue;
        /**
         * Otherwise, it sends a FastAcceptReply message to the pilot
         * with its latest entry for the other pilot, P'.k,
         * as its suggested dependency.
         * //TODO: definition on "latest"
         */
        suggest_dep = log_info.max_accepted_slot;
        Log_debug(
            "copilot server %d find imcompatiable dependence for %s : "
            "%lu -> %lu. suggest dep: %lu",
            id_, toString(is_pilot), slot, dep, suggest_dep);
        break;
      }
    }
  }

  if (ins->ballot <= ballot) {
    ins->ballot = ballot;
    ins->dep_id = dep;
    ins->cmd = cmd;
    // still set the cmd here, to prevent PREPARE from getting an empty cmd
    if (suggest_dep == dep) {
      ins->status = Status::FAST_ACCEPTED;
      updateMaxAcptSlot(log_infos_[is_pilot], slot);
    }
  } else {
    // TODO
  }
  *max_ballot = ballot;
  *ret_dep = suggest_dep;

  cb();
}

void CopilotServer::OnAccept(const uint8_t& is_pilot,
                             const uint64_t& slot,
                             const ballot_t& ballot,
                             const uint64_t& dep,
                             shared_ptr<Marshallable>& cmd,
                             ballot_t* max_ballot,
                             const function<void()> &cb) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_debug("server %d [ACCEPT     ] %s : %lu -> %lu", id_, toString(is_pilot), slot, dep);

  auto ins = GetInstance(slot, is_pilot);
  auto& log_info = log_infos_[is_pilot];

  if (ins->ballot <= ballot) {
    ins->ballot = ballot;
    ins->dep_id = dep;
    ins->cmd = cmd;
    ins->status = Status::ACCEPTED;
    updateMaxAcptSlot(log_info, slot);
  } else {  // ins->ballot > ballot
    /**
     * This can happen when a fast-takeover ACCEPT reaches the replica before a regular
     * ACCEPT and set a higher ballot number for the instance, thus block the regular
     * ACCEPT.
     */
  }

  *max_ballot = ballot;
  cb();
}

void CopilotServer::OnCommit(const uint8_t& is_pilot,
                             const uint64_t& slot,
                             const uint64_t& dep,
                             shared_ptr<Marshallable>& cmd) {
  std::lock_guard<std::recursive_mutex> lock(mtx_);
  Log_debug("server %d [COMMIT     ] %s : %ld -> %ld", id_, toString(is_pilot), slot, dep);
  auto ins = GetInstance(slot, is_pilot);
  if (ins->status >= Status::COMMITED) {
    /**
     * This case only happens when: this instance is fast-takovered on
     * another server and that server sent a COMMIT for that instance
     * to all replicas
     */
    return;
  }
  
  ins->cmd = cmd;
  ins->status = Status::COMMITED;
#ifdef WAIT_AT_UNCOMMIT
  ins->cmit_evt.Set(1);
#endif

  auto& log_info = log_infos_[is_pilot];
  auto& another_log_info = log_infos_[REVERSE(is_pilot)];
  updateMaxCmtdSlot(log_info, slot);
  verify(slot > log_info.max_executed_slot);

#ifndef WAIT_AT_UNCOMMIT
  /**
   * Q: should we execute commands here?
   * A: We may use another threads to execute the commands,
   * but for better programbility and understandability,
   * we should execute cmds here
   */
  /**
   * @Xuhao's note:
   * We should not only execute the committed cmd here, but also try to execute
   * cmds in both logs up-to max committed index. Cases are that later cmds have
   * committed before this cmd, and fail to execute due to this cmd had not been
   * committed at that time. The commit of this cmd makes executing later cmd possible.
   * 
   * If a cmd is committed but fail to execute due to uncommitted dependency, one way
   * for it to execute eventually is by executing a later cmd, which will execute all
   * cmds before it according to the order requirement. But a later cmd may not be
   * proposed if client is blocked and stop issuing new cmds. Thus the only way to
   * execute it in this case is to execute it when its dependent cmd commits, which is
   * bound to happen.
   */
  for (slotid_t i = slot; i <= log_info.max_committed_slot; i++) {
    auto is = GetInstance(i, is_pilot);
    executeCmds(is);
  }
  for (slotid_t i = std::max(dep, another_log_info.max_executed_slot + 1);
       i <= another_log_info.max_committed_slot; i++) {
    auto is = GetInstance(i, REVERSE(is_pilot));
    executeCmds(is);
  }
#else
  executeCmds(ins);
#endif

  // TODO: should support snapshot for freeing memory.
  // for now just free anything 1000 slots before.
  int i = log_info.min_active_slot;
  while (i + N_CMD_KEEP < log_info.max_executed_slot) {
    // remove unused cmds. a removed cmd must have been executed.
    removeCmd(log_info, i++);
  }
  log_info.min_active_slot = i;
}

void CopilotServer::setIsPilot(bool isPilot) {
  verify(!isPilot || !isCopilot_);
  isPilot_ = isPilot;
}

void CopilotServer::setIsCopilot(bool isCopilot) {
  verify(!isCopilot || !isPilot_);
  isCopilot_ = isCopilot;
}

inline void CopilotServer::updateMaxExecSlot(shared_ptr<CopilotData>& ins) {
  Log_debug("server %d [EXECUTE    ] %s : %lu -> %lu", id_, toString(ins->is_pilot), ins->slot_id, ins->dep_id);
  auto& log_info = log_infos_[ins->is_pilot];
  if (ins->slot_id == log_info.max_executed_slot + 1) {
    log_info.max_executed_slot = ins->slot_id;
    Log_debug("server %d [max EXECUTE] %s : + %lu", id_, toString(ins->is_pilot), ins->slot_id);
  }
}

void CopilotServer::updateMaxAcptSlot(CopilotLogInfo& log_info, slotid_t slot) {
  slotid_t i;
  for (i = log_info.max_accepted_slot + 1; i <= slot; i++) {
    auto& log_entry = log_info.logs[i];
    if (log_entry && log_entry->status < Status::FAST_ACCEPTED)
      break;
  }
  log_info.max_accepted_slot = i - 1;
}

void CopilotServer::updateMaxCmtdSlot(CopilotLogInfo& log_info, slotid_t slot) {
  slotid_t i;
  for (i = log_info.max_committed_slot + 1; i <= slot; i++) {
    auto& log_entry = log_info.logs[i];
    if (log_entry && log_entry->status < Status::COMMITED)
      break;
  }
  log_info.max_committed_slot = i - 1;
}

void CopilotServer::removeCmd(CopilotLogInfo& log_info, slotid_t slot) {
  auto cmd = dynamic_pointer_cast<TpcCommitCommand>(log_info.logs[slot]->cmd);
  tx_sched_->DestroyTx(cmd->tx_id_);
  log_info.logs.erase(slot);
}

bool CopilotServer::executeCmd(shared_ptr<CopilotData>& ins) {
  if (ins->cmd) {
    app_next_(*ins->cmd);
    updateMaxExecSlot(ins);
    ins->status = Status::EXECUTED;
    return true;
  } else {
    verify(0);
    return false;
  }
}

bool CopilotServer::executeCmds(shared_ptr<CopilotData>& ins) {
  if (ins->status == Status::EXECUTED)
    return true;
  
  if (ins->dep_id == 0) {
    return executeCmd(ins);
  } else {
#ifdef DEBUG
    return executeCmd(ins);
#else
    return findSCC(ins);
#endif
  }
}

bool CopilotServer::isExecuted(shared_ptr<CopilotData>& ins, slotid_t slot, uint8_t is_pilot) {
  if (ins)
    return ins->status == Status::EXECUTED;
  else
    return slot <= log_infos_[is_pilot].max_executed_slot;
}

/**************************************************************************

Traditional way to do command execution. Abort execution at unCOMMITed cmd

adopted from:
https://github.com/efficient/epaxos/blob/master/src/epaxos/epaxos-exec.go
https://github.com/PlatformLab/epaxos/blob/master/src/epaxos/epaxos-exec.go

**************************************************************************/
#ifndef WAIT_AT_UNCOMMIT

bool CopilotServer::findSCC(shared_ptr<CopilotData>& root) {
  int index = 1;
  while (!stack_.empty()) {
    // auto v = stack_.top();
    // v->dfn = 0;
    stack_.pop();
  }
  
  return strongConnect(root, &index);
}

bool CopilotServer::strongConnect(shared_ptr<CopilotData>& ins, int* index) {
  ins->dfn = *index;
  ins->low = *index;
  *index = *index + 1;
  stack_.push(ins);
  Log_debug("SCC %s : %lu -> %lu (%d, %d)", toString(ins->is_pilot), ins->slot_id, ins->dep_id, ins->dfn, ins->low);

  std::vector<uint8_t> order = ins->is_pilot ? std::vector<uint8_t>{YES, NO}
                                             : std::vector<uint8_t>{NO, YES};

  for (auto& p : order) {
    // first handle own side, then handle another side
    auto end = (p == ins->is_pilot) ? ins->slot_id - 1 : ins->dep_id;
    for (auto i = log_infos_[p].max_executed_slot + 1; i <= end; i++) {
      auto w = GetInstance(i, p);

      if (w->status == Status::EXECUTED) continue;

      if (w->status < Status::COMMITED) {
        // TODO: this cmd has not been committed, wait or return?
        Log_debug("%d, unCOMMITTED cmd %s : %lu -> %lu", id_, toString(w->is_pilot), w->slot_id, w->dep_id);
        ins->dfn = 0;
        return false;
      }

      if (w->dfn == 0) {
        if (!strongConnect(w, index)) {
          // find uncommitted cmd, abort execution and pop out all stacked cmds
          shared_ptr<CopilotData> v;
          do {
            v = stack_.top();
            v->dfn = 0;
            stack_.pop();
          } while (v != ins);
          return false;
        }
        ins->low = std::min(ins->low, w->low);
      } else {
        ins->low = std::min(w->dfn, ins->low);
      }
    }
  }

  if (ins->low == ins->dfn) {
    std::vector<shared_ptr<CopilotData> > list;
    shared_ptr<CopilotData> w;
    
    do {
      w = stack_.top();
      stack_.pop();
      list.push_back(w);
    } while (w != ins);

    std::sort(list.begin(), list.end(),
              [](const shared_ptr<CopilotData>& i1,
                 const shared_ptr<CopilotData>& i2) -> bool {
                if (i1->is_pilot == i2->is_pilot)
                  return i1->slot_id < i2->slot_id;
                else
                  return i1->is_pilot;
              });
    
    for (auto& c : list) {
      executeCmd(c);
    }
  }

  return true;
}

#else

/**************************************************************************

     New way to do command execution. Wait and yield at unCOMMITed cmd

**************************************************************************/

/**
 * DFS traverse the dependence graph (iteratively), if encountered an uncommitted cmd, wait and yield,
 * stop at EXECUTED cmd. There can be multiple DFS instance going in parallel, so each
 * DFS has an independent visited map.
 */
void CopilotServer::waitAllPredCommit(shared_ptr<CopilotData>& ins) {
  visited_map_t visited;
  copilot_stack_t stack;

  stack.push(ins);

  while (!stack.empty()) {
    auto w = stack.top();
    stack.pop();

    if (visited[w])
      continue;
    
    visited[w] = true;
    auto pre_ins = GetInstance(w->slot_id - 1, w->is_pilot);
    auto dep_ins = GetInstance(w->dep_id, REVERSE(w->is_pilot));

    if (pre_ins && pre_ins->status < EXECUTED && !visited[pre_ins]) {
      if (pre_ins->status < COMMITED)
        pre_ins->cmit_evt.WaitUntilGreaterOrEqualThan(1);
      stack.push(pre_ins);
    }

    if (dep_ins && dep_ins->status < EXECUTED && !visited[dep_ins]) {
      if (dep_ins->status < COMMITED)
        dep_ins->cmit_evt.WaitUntilGreaterOrEqualThan(1);
      stack.push(dep_ins);
    }
  }

  // waitPredCmds(ins, visited);
}

/**
 * DFS traverse the dependence graph (recursively), if encountered an uncommitted cmd, wait and yield,
 * stop at EXECUTED cmd. There can be multiple DFS instance going in parallel, so each
 * DFS has an independent visited map.
 */
void CopilotServer::waitPredCmds(shared_ptr<CopilotData>& w, shared_ptr<visited_map_t> m) {
  (*m)[w] = true;
  auto pre_ins = GetInstance(w->slot_id - 1, w->is_pilot);
  auto dep_ins = GetInstance(w->dep_id, REVERSE(w->is_pilot));

  if (pre_ins && pre_ins->status < EXECUTED && (m->find(pre_ins) == m->end())) {
    if (pre_ins->status < COMMITED)
      pre_ins->cmit_evt.WaitUntilGreaterOrEqualThan(1);
    waitPredCmds(pre_ins, m);
  }

  if (dep_ins && dep_ins->status < EXECUTED && (m->find(dep_ins) == m->end())) {
    if (dep_ins->status < COMMITED)
      dep_ins->cmit_evt.WaitUntilGreaterOrEqualThan(1);
    waitPredCmds(dep_ins, m);
  }
}

bool CopilotServer::findSCC(shared_ptr<CopilotData>& root) {
  int index = 1;
  copilot_stack_t stack;
  waitAllPredCommit(root);
  return strongConnect(root, &index);
}

bool CopilotServer::strongConnect(shared_ptr<CopilotData>& ins, int* index) {
  // at this time all predecessor cmds must have committed so this function must complete successfully
  ins->dfn = *index;
  ins->low = *index;
  *index = *index + 1;
  stack_.push(ins);
  verify(ins);
  // Log_debug("SCC %s : %lu -> %lu (%d, %d)", toString(ins->is_pilot), ins->slot_id, ins->dep_id, ins->dfn, ins->low);


  std::vector<uint8_t> order = ins->is_pilot ? std::vector<uint8_t>{YES, NO}
                                             : std::vector<uint8_t>{NO, YES};
  
  for (auto& p : order) {
    auto end = (p == ins->is_pilot) ? ins->slot_id - 1 : ins->dep_id;
    for (auto i = log_infos_[p].max_executed_slot + 1; i <= end; i++) {
      auto w = GetInstance(i, p);

      if (w->status == Status::EXECUTED) continue;

      if (w->status < Status::COMMITED) {
        verify(0); // should not happen
      }

      if (w->dfn == 0) {
        strongConnect(w, index);
        ins->low = std::min(ins->low, w->low);
      } else {
        ins->low = std::min(w->dfn, ins->low);
      }
    }
  }

  if (ins->low == ins->dfn) {
    std::vector<shared_ptr<CopilotData> > list;
    shared_ptr<CopilotData> w;
    
    do {
      w = stack_.top();
      stack_.pop();
      list.push_back(w);
    } while (w != ins);

    std::sort(list.begin(), list.end(),
              [](const shared_ptr<CopilotData>& i1,
                 const shared_ptr<CopilotData>& i2) -> bool {
                if (i1->is_pilot == i2->is_pilot)
                  return i1->slot_id < i2->slot_id;
                else
                  return i1->is_pilot;
              });
    
    for (auto& c : list) {
      executeCmd(c);
    }
  }

  return true;
}

#endif

} // namespace janus