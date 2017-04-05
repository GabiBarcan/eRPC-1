/**
 * @file rpc_connect_handlers.cc
 * @brief Handlers for session management discconnect requests and responses.
 */
#include "rpc.h"
#include <algorithm>

namespace ERpc {

// We don't need to check remote arguments since the session was already
// connected successfully.
//
// We don't need to lock the session since it is idle, i.e., the session client
// has received responses for all outstanding requests.

template <class TTr>
void Rpc<TTr>::handle_disconnect_req_st(SessionMgmtPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kDisconnectReq);

  // Check that the server fields known by the client were filled correctly
  assert(sm_pkt->server.rpc_id == rpc_id);
  assert(strcmp(sm_pkt->server.hostname, nexus->hostname.c_str()) == 0);

  // Create the basic issue message
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg, "eRPC Rpc %u: Received disconnect request from %s. Issue",
          rpc_id, sm_pkt->client.name().c_str());

  uint16_t session_num = sm_pkt->server.session_num;
  assert(session_num < session_vec.size());

  // Check if the session was already disconnected
  if (session_vec.at(session_num) == nullptr) {
    erpc_dprintf("%s. Duplicate disconnect request. Sending response.\n",
                 issue_msg);

    sm_pkt->send_resp_mut(SessionMgmtErrType::kNoError, &nexus->udp_config);
    return;
  }

  // If the session was not already disconnected, the session endpoints
  // (hostname, Rpc ID, session num) in the pkt should match our local copy.
  Session *session = session_vec.at(session_num);  // The server end point
  assert(session->is_server());
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  // Check that responses for all sslots have been sent
  for (size_t i = 0; i < Session::kSessionReqWindow; i++) {
    SSlot &sslot = session->sslot_arr[i];
    assert(sslot.rx_msgbuf.buf == nullptr &&
           sslot.rx_msgbuf.buffer.buf == nullptr);

    if (sslot.tx_msgbuf != nullptr) {
      assert(sslot.tx_msgbuf->pkts_queued == sslot.tx_msgbuf->num_pkts);
    }
  }

  erpc_dprintf("%s. None. Sending response.\n", issue_msg);
  sm_pkt->send_resp_mut(SessionMgmtErrType::kNoError, &nexus->udp_config);

  bury_session_st(session);  // Free session resources + NULL in session_vec
}

// We don't need to acquire the session lock because this session has been
// idle since the disconnect request was sent.
template <class TTr>
void Rpc<TTr>::handle_disconnect_resp_st(SessionMgmtPkt *sm_pkt) {
  assert(in_creator());
  assert(sm_pkt != NULL);
  assert(sm_pkt->pkt_type == SessionMgmtPktType::kDisconnectResp);
  assert(session_mgmt_err_type_is_valid(sm_pkt->err_type));

  // Create the basic issue message using only the packet
  char issue_msg[kMaxIssueMsgLen];
  sprintf(issue_msg,
          "eRPC Rpc %u: Received disconnect response from %s for session %u. "
          "Issue",
          rpc_id, sm_pkt->server.name().c_str(), sm_pkt->client.session_num);

  // Try to locate the requester session for this response
  uint16_t session_num = sm_pkt->client.session_num;
  assert(session_num < session_vec.size());

  Session *session = session_vec[session_num];

  // Check if the client session was already disconnected. This happens when
  // we get a duplicate disconnect response. If so, the callback is not invoked.
  if (session == nullptr) {
    assert(!mgmt_retryq_contains_st(session));
    erpc_dprintf("%s: Client session is already disconnected.\n", issue_msg);
    return;
  }

  // If we are here, this is the first disconnect response, so we must be in the
  // kDisconnectInProgress state, and the disconnect req should be in flight.
  //
  // It's not possible to also have a connect request in flight, since
  // the disconnect must wait for the first connect response, at which point
  // the connect request is removed from the in-flight list.
  assert(session->state == SessionState::kDisconnectInProgress);
  assert(mgmt_retryq_contains_st(session));
  mgmt_retryq_remove_st(session);

  // If the session was not already disconnected, the session endpoints
  // (hostname, Rpc ID, session num) in the pkt should match our local copy.
  assert(session->server == sm_pkt->server);
  assert(session->client == sm_pkt->client);

  // Disconnect requests can only succeed
  assert(sm_pkt->err_type == SessionMgmtErrType::kNoError);

  session->state = SessionState::kDisconnected;  // Mark session connected

  if (!session->client_info.sm_callbacks_disabled) {
    erpc_dprintf("%s: None. Session disconnected.\n", issue_msg);
    session_mgmt_handler(session->local_session_num,
                         SessionMgmtEventType::kDisconnected,
                         SessionMgmtErrType::kNoError, context);
  } else {
    erpc_dprintf(
        "%s: None. Session disconnected. Not invoking disconnect "
        "callback because session was never connected successfully.",
        issue_msg);
  }

  bury_session_st(session);  // Free session resources + NULL in session_vec
}

}  // End ERpc
