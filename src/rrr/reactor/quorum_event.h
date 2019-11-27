#pragma once

#include <vector>
#include <unordered_map>
//#include <unordered_set>
//#include <fstream>
#include <iostream>
#include "event.h"

using rrr::Event;
using std::vector;
using std::unordered_map;
using std::unordered_set;

namespace janus {

class QuorumEvent : public Event {
 private:
  int32_t n_voted_yes_{0};
  int32_t n_voted_no_{0};
 public:
  int32_t n_total_ = -1;
  int32_t quorum_ = -1;
  bool timeouted_ = false;
  uint64_t coro_id_ = -1;
  int64_t par_id_ = -1;
  uint64_t id_ = -1;
  // fast vote result.
  vector<uint64_t> vec_timestamp_{};
  vector<int> sites_{};
  std::unordered_map<int, unordered_map<int, unordered_set<int>>> deps{};
  std::string log_file = "logs.txt";

  QuorumEvent() = delete;

  QuorumEvent(int n_total,
              int quorum) : Event(),
                            n_total_(n_total),
                            quorum_(quorum) {
  }

  bool Yes() {
    return n_voted_yes_ >= quorum_;
  }

  bool No() {
    verify(n_total_ >= quorum_);
    return n_voted_no_ > (n_total_ - quorum_);
  }

  void VoteYes() {
    n_voted_yes_++;
    Test();
  }

  void VoteNo() {
    n_voted_no_++;
    Test();
  }

  void add_dep(int srcId, int srcCoro, int tgtId){
    auto srcIndex = deps.find(srcId);
    if(srcIndex == deps.end()){
      unordered_map<int, unordered_set<int>> temp = {};
      deps[srcId] = temp;
    }

    auto srcCoroIndex = deps[srcId].find(srcCoro);
    if(srcCoroIndex == deps[srcId].end()){
      unordered_set<int> temp = {};
      deps[srcId][srcCoro] = temp; 
    }
    // commented part is for testing
    /*
    std::ofstream of(log_file, std::fstream::app);
    of << srcId << ", " << tgtId << "\n";
    of.close();
    */
    auto tgtIndex = deps[srcId][srcCoro].find(tgtId);
    if(tgtIndex == deps[srcId][srcCoro].end()){
      deps[srcId][srcCoro].insert(tgtId);
    }
  }

  /*void remove_dep(int srcId, int tgtId){
    auto srcIndex = deps.find(srcId);
    if(srcIndex != deps.end()){
      auto tgtIndex = deps[srcId].find(tgtId);
      if(tgtIndex != deps[srcId].end()){
        deps[srcId].erase(tgtId);
      }
    }
  }*/

  void log(){
    std::ofstream of(log_file, std::fstream::app);
    //of << "hello\n";
    if(coro_id_ == -1) return;
    
    // Maybe this part can be more efficient
    for(auto it = deps.begin(); it != deps.end(); it++){
      for(auto it2 = it->second.begin(); it2 != it->second.end(); it2++){
        for(auto it3 = it2->second.begin(); it3 != it2->second.end(); it3++){
          of << "{ " << it->first << ", " << it2->first << ", " << *it3 << ", " << coro_id_ << ", " << id_ << ": " << quorum_ << "/" << n_total_ << " }\n";
        }
      }
    }
    of.close();
  }

  void Wait() override{
    log();
    Event::Wait();
  }

  bool IsReady() override {
    if (timeouted_) {
      // TODO add time out support
      return true;
    }
    if (Yes()) {
//      Log_debug("voted: %d is equal or greater than quorum: %d",
//                (int)n_voted_yes_, (int) quorum_);
      return true;
    } else if (No()) {
      return true;
    }
//    Log_debug("voted: %d is smaller than quorum: %d",
//              (int)n_voted_, (int) quorum_);
    return false;
  }

};

} // namespace janus
