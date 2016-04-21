#pragma once

#include "deptran/__dep__.h"
#include "deptran/txn_req_factory.h"

namespace rococo {

class TpcaTxnGenerator: public TxnGenerator {
 public:
  boost::random::mt19937 rand_gen_;
  map<int32_t, int32_t> key_ids_ = {};
  TpcaTxnGenerator(Config* config);
  virtual void GetTxnReq(TxnRequest *req, uint32_t cid) override;
};

} // namespace rococo