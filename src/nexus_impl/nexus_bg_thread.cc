#include "nexus.h"
#include "common.h"
#include "ops.h"
#include "rpc.h"
#include "session.h"
#include "util/mt_list.h"

namespace ERpc {

template <class TTr>
void Nexus<TTr>::bg_thread_func(BgThreadCtx *bg_thread_ctx) {
  volatile bool *bg_kill_switch = bg_thread_ctx->bg_kill_switch;
  size_t bg_thread_index = bg_thread_ctx->bg_thread_index;
  TlsRegistry *tls_registry = bg_thread_ctx->tls_registry;

  tls_registry->init();  // Initialize thread-local variables for this thread

  // The BgWorkItem request list can be indexed using the background thread's
  // index in the Nexus, or its tiny TID.
  if (bg_thread_index != tls_registry->get_tiny_tid()) {
    // This error showed up once, but I couldn't reproduce it again. This
    // should be just an assert, but the exception might help in debugging.
    std::ostringstream err;
    err << "eRPC Nexus : Background thread ID mismatch. Nexus-assigned thread "
        << "index is " << std::to_string(bg_thread_index) << ", but tiny "
        << "thread ID is " << std::to_string(tls_registry->get_tiny_tid());
    throw std::runtime_error(err.str());
  }

  erpc_dprintf("eRPC Nexus: Background thread %zu running. Tiny TID = %zu.\n",
               bg_thread_index, tls_registry->get_tiny_tid());

  while (*bg_kill_switch == false) {
    MtList<BgWorkItem> &req_list = bg_thread_ctx->bg_req_list;

    if (req_list.size == 0) {
      // Try again later
      usleep(1);
      continue;
    }

    req_list.lock();
    assert(req_list.size > 0);

    for (BgWorkItem bg_work_item : req_list.list) {
      const BgWorkItemType wi_type = bg_work_item.wi_type;
      const uint8_t rpc_id = bg_work_item.rpc_id;  // Debug-only
      void *context = bg_work_item.context;        // The app's context
      SSlot *sslot = bg_work_item.sslot;
      Session *session = sslot->session;  // Debug-only
      _unused(rpc_id);
      _unused(session);
      assert(context != nullptr && sslot != nullptr && session != nullptr);

      // Sanity-check RX and TX MsgBuffers
      Rpc<TTr>::debug_check_bg_rx_msgbuf(sslot, bg_work_item.wi_type);
      assert(sslot->tx_msgbuf == nullptr);  // Sanity-check tx_msgbuf

      bool is_req = (wi_type == BgWorkItemType::kReq);

      dpath_dprintf(
          "eRPC Background: Background thread %zu running %s for Rpc %u, "
          "session %u. Request number = %zu.\n",
          bg_thread_index, is_req ? "request handler" : "continuation", rpc_id,
          session->local_session_num, sslot->rx_msgbuf.get_req_num());

      if (is_req) {
        uint8_t req_type = sslot->rx_msgbuf.get_req_type();
        const ReqFunc &req_func = bg_thread_ctx->req_func_arr->at(req_type);
        assert(req_func.is_registered());  // Checked during submit_bg

        req_func.req_func((ReqHandle *)sslot, context);
        bg_work_item.rpc->bury_sslot_rx_msgbuf(sslot);
      } else {
        sslot->clt_save_info.cont_func((RespHandle *)sslot, context,
                                       sslot->clt_save_info.tag);

        // The continuation must release the response (rx_msgbuf), but the
        // event loop thread may re-use it. So it may not be null.
      }
    }

    req_list.locked_clear();
    req_list.unlock();
  }

  erpc_dprintf("eRPC Nexus: Background thread %zu exiting.\n", bg_thread_index);
  return;
}

}  // End ERpc
