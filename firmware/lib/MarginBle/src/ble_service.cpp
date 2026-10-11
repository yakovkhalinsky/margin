// SPDX-License-Identifier: MIT
#include "margin/ble_service.h"

#if defined(ARDUINO) && __has_include(<NimBLEDevice.h>)

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstring>

namespace margin::ble {

namespace {

enum class PacketKind : uint8_t { Rx, Ctrl, File, Connected, Disconnected, Secured, Mtu, Passkey };

struct Packet {
  PacketKind kind;
  uint16_t len;
  uint32_t value;  // MTU, passkey, conn handle...
  uint8_t data[proto::kMaxFrameSize];
};

constexpr UBaseType_t kQueueDepth = 24;  // > default window so a full window never drops

QueueHandle_t gQueue = nullptr;
NimBLEServer* gServer = nullptr;
NimBLECharacteristic* gTx = nullptr;
NimBLECharacteristic* gCtrl = nullptr;
uint16_t gConn = BLE_HS_CONN_HANDLE_NONE;
// Connection awaiting a numeric-comparison answer. NimBLEConnInfo cannot be default-constructed,
// so keep the handle and look the peer up again when the user answers.
// TODO(M2): verify injectConfirmPasskey() is safe to call from the main loop task.
uint16_t gPendingPairing = BLE_HS_CONN_HANDLE_NONE;

uint8_t* psramAlloc(uint32_t n) {
  return static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void post(PacketKind kind, const uint8_t* data, size_t len, uint32_t value = 0) {
  if (gQueue == nullptr) return;
  Packet p;
  p.kind = kind;
  p.value = value;
  p.len = static_cast<uint16_t>(len > sizeof p.data ? sizeof p.data : len);
  if (data && p.len) std::memcpy(p.data, data, p.len);
  // Never block the NimBLE host task. A drop is recovered by the ACK/NACK go-back-N logic.
  xQueueSend(gQueue, &p, 0);
}

}  // namespace

struct Callbacks : NimBLEServerCallbacks, NimBLECharacteristicCallbacks {
  // --- server
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    gConn = info.getConnHandle();
    post(PacketKind::Connected, nullptr, 0, gConn);
    // Ask the central to encrypt right away; with only bonded peers allowed this either
    // restores the bond or starts pairing (accepted only inside the pairing window).
    NimBLEDevice::startSecurity(gConn);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    gConn = BLE_HS_CONN_HANDLE_NONE;
    post(PacketKind::Disconnected, nullptr, 0);
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { post(PacketKind::Mtu, nullptr, 0, mtu); }
  void onConfirmPassKey(NimBLEConnInfo& info, uint32_t pin) override {
    gPendingPairing = info.getConnHandle();
    post(PacketKind::Passkey, nullptr, 0, pin);
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (!info.isEncrypted() || !info.isAuthenticated() || !info.isBonded()) {
      // Bonded + MITM-authenticated LE Secure Connections only.
      gServer->disconnect(info.getConnHandle());
      return;
    }
    post(PacketKind::Secured, nullptr, 0);
  }
  // --- characteristics
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    const NimBLEAttValue& v = c->getValue();
    const NimBLEUUID& u = c->getUUID();
    if (u == NimBLEUUID(proto::kRxCharUuid)) post(PacketKind::Rx, v.data(), v.size());
    else if (u == NimBLEUUID(proto::kCtrlCharUuid)) post(PacketKind::Ctrl, v.data(), v.size());
    else if (u == NimBLEUUID(proto::kFileCharUuid)) post(PacketKind::File, v.data(), v.size());
  }
};

namespace {
Callbacks gCallbacks;
}

Service& service() {
  static Service s;
  return s;
}

bool Service::begin(const Config& cfg, Handler* handler) {
  if (state_ != LinkState::Off) return true;
  cfg_ = cfg;
  handler_ = handler;
  if (rxBuf_ == nullptr) rxBuf_ = psramAlloc(cfg_.rxCapacity);
  if (txBuf_ == nullptr) txBuf_ = psramAlloc(cfg_.txCapacity);
  if (rxBuf_ == nullptr || txBuf_ == nullptr) return false;
  reassembler_.attach(rxBuf_, cfg_.rxCapacity);

  if (gQueue == nullptr) gQueue = xQueueCreate(kQueueDepth, sizeof(Packet));
  if (gQueue == nullptr) return false;

  // TODO: append the last two MAC bytes to the name once NimBLEDevice::getAddress() is checked on the S3.
  NimBLEDevice::init(cfg_.deviceName);
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(/*bonding*/ true, /*mitm*/ true, /*sc*/ true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);  // numeric comparison on the e-ink screen

  gServer = NimBLEDevice::createServer();
  gServer->setCallbacks(&gCallbacks);
  NimBLEService* svc = gServer->createService(proto::kServiceUuid);
  // Every attribute requires an encrypted, authenticated (MITM) link.
  svc->createCharacteristic(proto::kRxCharUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
                                                    NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN)
      ->setCallbacks(&gCallbacks);
  gTx = svc->createCharacteristic(proto::kTxCharUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC |
                                                          NIMBLE_PROPERTY::READ_AUTHEN);
  gCtrl = svc->createCharacteristic(proto::kCtrlCharUuid,
                                    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN |
                                        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC |
                                        NIMBLE_PROPERTY::WRITE_AUTHEN | NIMBLE_PROPERTY::NOTIFY);
  gCtrl->setCallbacks(&gCallbacks);
  uint8_t status[proto::kCtrlStatusSize];
  proto::encodeStatus(status, proto::kMaxFrameSize, cfg_.window, 0, cfg_.rxCapacity);
  gCtrl->setValue(status, sizeof status);
  svc->createCharacteristic(proto::kFileCharUuid,
                            NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN)
      ->setCallbacks(&gCallbacks);
  gServer->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(proto::kServiceUuid);
  adv->setName(cfg_.deviceName);
  // TODO(M2): when not in the pairing window, restrict connections to bonded peers with
  // adv->setScanFilter(false, true) + whitelist of bonded identity addresses, and verify that
  // RPA resolution works with the S3 controller. Until then, unbonded peers can connect but
  // cannot read/write anything (all attributes need an authenticated link) and pairing is
  // refused outside the window.
  adv->start();
  setState(LinkState::Advertising);
  return true;
}

void Service::end() {
  if (state_ == LinkState::Off) return;
  NimBLEDevice::deinit(true);
  gServer = nullptr;
  gTx = gCtrl = nullptr;
  gConn = BLE_HS_CONN_HANDLE_NONE;
  setState(LinkState::Off);
}

void Service::openPairingWindow(uint32_t durationMs) { pairingUntilMs_ = millis() + durationMs; }

void Service::confirmPairing(bool accept) {
  if (gPendingPairing == BLE_HS_CONN_HANDLE_NONE || gServer == nullptr) return;
  const bool windowOpen = static_cast<int32_t>(pairingUntilMs_ - millis()) > 0;
  NimBLEDevice::injectConfirmPasskey(gServer->getPeerInfoByHandle(gPendingPairing), accept && windowOpen);
  gPendingPairing = BLE_HS_CONN_HANDLE_NONE;
}

void Service::forgetAllBonds() { NimBLEDevice::deleteAllBonds(); }

void Service::setState(LinkState s) {
  if (s == state_) return;
  state_ = s;
  if (handler_) handler_->onLinkState(s);
}

void Service::poll(uint32_t nowMs) {
  if (gQueue == nullptr) return;
  Packet p;
  // Bounded drain per loop so a flood cannot starve rendering.
  for (int i = 0; i < 16 && xQueueReceive(gQueue, &p, 0) == pdTRUE; ++i) {
    switch (p.kind) {
      case PacketKind::Connected:
        setState(LinkState::Connected);
        frameSize_ = proto::kMinFrameSize;
        reassembler_.reset();
        break;
      case PacketKind::Disconnected:
        reassembler_.reset();
        fragmenter_ = proto::Fragmenter{};
        NimBLEDevice::getAdvertising()->start();
        setState(LinkState::Advertising);
        break;
      case PacketKind::Secured: setState(LinkState::Secured); break;
      case PacketKind::Mtu: {
        uint32_t f = p.value > 3 ? p.value - 3 : proto::kMinFrameSize;
        if (f > proto::kMaxFrameSize) f = proto::kMaxFrameSize;
        if (f < proto::kMinFrameSize) f = proto::kMinFrameSize;
        frameSize_ = static_cast<uint16_t>(f);
        break;
      }
      case PacketKind::Passkey: {
        const bool windowOpen = static_cast<int32_t>(pairingUntilMs_ - nowMs) > 0;
        if (!windowOpen) {
          confirmPairing(false);
        } else if (handler_) {
          handler_->onPairingRequest(p.value);
        }
        break;
      }
      case PacketKind::Rx:
        if (state_ == LinkState::Secured) handleRxFrame(p.data, p.len);
        break;
      case PacketKind::Ctrl:
        if (state_ == LinkState::Secured) handleCtrl(p.data, p.len);
        break;
      case PacketKind::File:
        if (state_ == LinkState::Secured && handler_) {
          proto::FileChunk c;
          if (proto::decodeFileChunk(p.data, p.len, c)) handler_->onFileChunk(c);
          // TODO(M6): the handler decides when to FileAck (every `window` chunks) via sendFileAck().
        }
        break;
    }
  }
  pumpTx(nowMs);
}

void Service::handleRxFrame(const uint8_t* data, size_t len) {
  using proto::FeedResult;
  const FeedResult r = reassembler_.feed(data, len);
  uint8_t ctrl[8];
  if (r == FeedResult::Error) {
    const size_t n = proto::encodeNack(ctrl, reassembler_.msgId(), reassembler_.error(), reassembler_.nextFrag());
    if (gCtrl) gCtrl->notify(ctrl, n, gConn);
    return;
  }
  if (r == FeedResult::Complete && handler_) handler_->onMessage(reassembler_.message());
  if (reassembler_.ackRequested()) {
    const size_t n = proto::encodeAck(ctrl, reassembler_.msgId(), reassembler_.nextFrag(), cfg_.window);
    if (gCtrl) gCtrl->notify(ctrl, n, gConn);
  }
}

void Service::handleCtrl(const uint8_t* data, size_t len) {
  proto::CtrlPacket p;
  if (!proto::decodeCtrl(data, len, p) || !fragmenter_.active() || p.msgId != fragmenter_.msgId()) return;
  if (p.op == proto::CtrlOp::Ack) fragmenter_.onAck(p.nextFrag);
  else if (p.op == proto::CtrlOp::Nack) fragmenter_.rewindTo(p.nextFrag);
}

bool Service::send(proto::MsgType type, const uint8_t* body, uint32_t len) {
  if (state_ != LinkState::Secured || busy() || len > cfg_.txCapacity) return false;
  if (len) std::memcpy(txBuf_, body, len);
  if (!fragmenter_.begin(nextMsgId_++, type, txBuf_, len, frameSize_, cfg_.window)) return false;
  lastTxProgressMs_ = millis();
  return true;
}

bool Service::sendFileAck(uint16_t transferId, uint32_t nextOffset) {
  if (state_ != LinkState::Secured || gCtrl == nullptr) return false;
  uint8_t ctrl[proto::kCtrlFileAckSize];
  const size_t n = proto::encodeFileAck(ctrl, transferId, nextOffset, cfg_.window);
  return gCtrl->notify(ctrl, n, gConn);
}

void Service::pumpTx(uint32_t nowMs) {
  if (!fragmenter_.active() || gTx == nullptr) return;
  if (fragmenter_.done()) {
    fragmenter_ = proto::Fragmenter{};
    return;
  }
  uint8_t frame[proto::kMaxFrameSize];
  while (fragmenter_.canSend()) {
    const size_t n = fragmenter_.next(frame, sizeof frame);
    if (n == 0 || !gTx->notify(frame, n, gConn)) {
      fragmenter_.rewindTo(fragmenter_.nextToSend() - 1);  // controller buffers full: retry next poll
      break;
    }
    lastTxProgressMs_ = nowMs;
  }
  if (fragmenter_.allSent() || !fragmenter_.canSend()) {
    if (nowMs - lastTxProgressMs_ > cfg_.ackTimeoutMs) {
      fragmenter_.rewindTo(fragmenter_.acked());  // go-back-N on ACK timeout
      lastTxProgressMs_ = nowMs;
    }
  }
}

}  // namespace margin::ble

#endif  // ARDUINO && NimBLE
