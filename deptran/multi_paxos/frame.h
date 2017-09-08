#pragma once

#include <deptran/communicator.h>
#include "../frame.h"
#include "../constants.h"
#include "commo.h"

namespace janus {

class MultiPaxosFrame : public Frame {
 private:
  slotid_t slot_hint_ = 1;
 public:
  MultiPaxosFrame(int mode);
  MultiPaxosCommo *commo_ = nullptr;
  Executor *CreateExecutor(cmdid_t cmd_id, Scheduler *sched) override;
  Coordinator *CreateCoordinator(cooid_t coo_id,
                                 Config *config,
                                 int benchmark,
                                 ClientControlServiceImpl *ccsi,
                                 uint32_t id,
                                 TxnRegistry *txn_reg) override;
  Scheduler *CreateScheduler() override;
  Communicator *CreateCommo(PollMgr *poll = nullptr) override;
  vector<rrr::Service *> CreateRpcServices(uint32_t site_id,
                                           Scheduler *dtxn_sched,
                                           rrr::PollMgr *poll_mgr,
                                           ServerControlServiceImpl *scsi) override;
};

} // namespace janus
