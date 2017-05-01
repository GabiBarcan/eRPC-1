#ifndef ERPC_TLS_REGISTRY_H
#define ERPC_TLS_REGISTRY_H

#include <atomic>
#include "common.h"

namespace ERpc {

class TlsRegistry {
 public:
  TlsRegistry() : cur_etid(0) {}
  std::atomic<size_t> cur_etid;

  /// Initialize all the thread-local registry members
  void init();

  /// Return the ERpc thread ID of the caller
  size_t get_etid() const;
};  // End ERpc
}

#endif