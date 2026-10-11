// SPDX-License-Identifier: MIT
// margin GATT server. Thread model: NimBLE callbacks run on the NimBLE host task and only copy
// raw packets into a FreeRTOS queue; everything else (reassembly, ACKs, dispatch, sending) runs
// in poll() on the PapyriX main loop, so handlers may touch UI state without locking.
//
// STATUS: scaffold. Written against NimBLE-Arduino 2.5.x headers but not yet built for or run on
// an X4 Pro. See firmware/README.md "Integration plan" and docs/ROADMAP.md milestone M2.
#pragma once

#include <cstddef>
#include <cstdint>

#include "margin/framing.h"

namespace margin::ble {

enum class LinkState : uint8_t { Off, Advertising, Connected, Secured };

class Handler {
 public:
  virtual ~Handler() = default;
  // A complete, reassembled message from the app (body valid only during the call).
  virtual void onMessage(const proto::Message& msg) = 0;
  // A chunk written to the FILE characteristic. Return the next offset you expect (for FileAck).
  virtual uint32_t onFileChunk(const proto::FileChunk& chunk) = 0;
  // LE Secure Connections numeric comparison: show `passkey` and call confirmPairing(yes/no).
  virtual void onPairingRequest(uint32_t passkey) = 0;
  virtual void onLinkState(LinkState state) = 0;
};

struct Config {
  const char* deviceName = "margin";  // suffixed with the last 2 MAC bytes, e.g. margin-1A2B
  uint32_t rxCapacity = proto::kMaxBodySize;  // reassembly buffer, allocated in PSRAM by begin()
  uint32_t txCapacity = proto::kMaxBodySize;  // outgoing message copy, kept until fully ACKed
  uint8_t window = proto::kDefaultWindow;
  uint32_t ackTimeoutMs = 2000;
};

class Service {
 public:
  bool begin(const Config& cfg, Handler* handler);
  void end();  // stops advertising, disconnects, deinitialises the controller (saves power)

  // Accept new bonds for `durationMs` (from Settings > Pair phone). Outside this window only
  // already-bonded centrals may connect. Bonding never happens implicitly.
  void openPairingWindow(uint32_t durationMs);
  void confirmPairing(bool accept);
  void forgetAllBonds();

  // Call every main-loop iteration.
  void poll(uint32_t nowMs);

  // Queue one message for the app. Returns false if a previous message is still in flight or the
  // body does not fit txCapacity. (TODO: small outbound queue so UI code never has to retry.)
  bool send(proto::MsgType type, const uint8_t* body, uint32_t len);
  bool sendFileAck(uint16_t transferId, uint32_t nextOffset);
  bool busy() const { return fragmenter_.active() && !fragmenter_.done(); }

  LinkState state() const { return state_; }

 private:
  friend struct Callbacks;
  void handleRxFrame(const uint8_t* data, size_t len);
  void handleCtrl(const uint8_t* data, size_t len);
  void pumpTx(uint32_t nowMs);
  void setState(LinkState s);

  Config cfg_{};
  Handler* handler_ = nullptr;
  proto::Reassembler reassembler_{nullptr, 0};
  uint8_t* rxBuf_ = nullptr;
  uint8_t* txBuf_ = nullptr;
  proto::Fragmenter fragmenter_;
  uint8_t nextMsgId_ = 0;
  uint16_t frameSize_ = proto::kMinFrameSize;
  uint32_t lastTxProgressMs_ = 0;
  uint32_t pairingUntilMs_ = 0;
  LinkState state_ = LinkState::Off;
};

Service& service();

}  // namespace margin::ble
