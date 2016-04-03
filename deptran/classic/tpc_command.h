#pragma once
#include "../__dep__.h"
#include "../command.h"

namespace rococo {

#define CMD_TPC_PREPARE (1)
#define CMD_TPC_COMMIT  (2)

class TxnCommand;
class TpcPrepareCommand : public ContainerCommand {
 public:
  TpcPrepareCommand() {
    self_cmd_ = this;
    type_ = CMD_TPC_PREPARE;
  }
  txnid_t txn_id_ = 0;
  int32_t res_ = -1;
  TxnCommand* txn_cmd_ = nullptr;

  Marshal& ToMarshal(Marshal&) const override;
  Marshal& FromMarshal(Marshal&) override;
  virtual Command& Execute() {verify(0);};
};

class TpcCommitCommand : public ContainerCommand {
 public:
  TpcCommitCommand() {
    self_cmd_ = this;
    type_ = CMD_TPC_COMMIT;
  }
  TxnCommand* txn_cmd_ = nullptr;
  virtual Marshal& ToMarshal(Marshal&) const;
  virtual Marshal& FromMarshal(Marshal&);
  virtual Command& Execute() {verify(0);};
};

} // namespace rococo