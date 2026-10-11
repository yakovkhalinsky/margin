// SPDX-License-Identifier: MIT
package dev.margin.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.content.Context
import android.util.Log
import dev.margin.protocol.Ctrl
import dev.margin.protocol.FeedResult
import dev.margin.protocol.Fragmenter
import dev.margin.protocol.Message
import dev.margin.protocol.MsgType
import dev.margin.protocol.Protocol
import dev.margin.protocol.Reassembler
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import java.util.UUID

/**
 * GATT client for the margin service (docs/BLE_PROTOCOL.md).
 *
 * STATUS: scaffold. The protocol layer it uses (Fragmenter/Reassembler/Ctrl) is unit-tested;
 * the Android GATT plumbing below is untested and has known TODOs:
 *  - TODO(M2): scanning with a ScanFilter on SERVICE_UUID + CompanionDeviceManager association
 *    (gives background reconnects without broad location/scan permissions).
 *  - TODO(M2): a single serialized GATT operation queue (Android allows one outstanding op).
 *  - TODO(M2): bonding UX: createBond() and surface the numeric-comparison prompt.
 *  - TODO(M2): ACK timeout -> rewind, and an outbound message queue (one Fragmenter in flight).
 *  - TODO(M6): FILE characteristic sender with FileAck flow control.
 * Uses the API 33+ GATT write/notify signatures (minSdk 33).
 * Permissions (BLUETOOTH_CONNECT / BLUETOOTH_SCAN) must be granted before calling connect().
 */
@SuppressLint("MissingPermission")
class MarginBleClient(private val context: Context) {
    enum class State { DISCONNECTED, CONNECTING, DISCOVERING, READY }

    private val _state = MutableStateFlow(State.DISCONNECTED)
    val state: StateFlow<State> = _state

    private val incoming = Channel<Message>(Channel.BUFFERED)
    /** Complete messages from the device (USER_MESSAGE, PASSAGE_SHARE, HELLO, ...). */
    val messages = incoming.receiveAsFlow()

    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var ctrl: BluetoothGattCharacteristic? = null
    private var frameSize = Protocol.MIN_FRAME_SIZE
    private var window = Protocol.DEFAULT_WINDOW
    private val reassembler = Reassembler()
    private var outgoing: Fragmenter? = null
    private var nextMsgId = 0

    fun connect(device: BluetoothDevice) {
        _state.value = State.CONNECTING
        gatt = device.connectGatt(context, /* autoConnect = */ false, callback, BluetoothDevice.TRANSPORT_LE)
    }

    fun disconnect() {
        gatt?.close()
        gatt = null
        _state.value = State.DISCONNECTED
    }

    /** Queue one message. Returns false if another message is still in flight (TODO: queue). */
    @Synchronized
    fun send(type: MsgType, body: ByteArray): Boolean {
        if (_state.value != State.READY || outgoing?.done == false) return false
        outgoing = Fragmenter(nextMsgId++ and 0xFF, type, body, frameSize, window)
        pump()
        return true
    }

    @Synchronized
    private fun pump() {
        val f = outgoing ?: return
        val g = gatt ?: return
        val ch = rx ?: return
        // TODO(M2): this must go through the serialized op queue and wait for onCharacteristicWrite.
        while (f.canSend) {
            val frame = f.next() ?: break
            val rc = g.writeCharacteristic(ch, frame, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
            if (rc != BluetoothStatusCodes.SUCCESS) { f.rewindTo(f.sent - 1); break }
        }
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                _state.value = State.DISCOVERING
                g.requestMtu(Protocol.MAX_FRAME_SIZE + 3)
            } else {
                reassembler.reset()
                outgoing = null
                _state.value = State.DISCONNECTED
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            frameSize = (mtu - 3).coerceIn(Protocol.MIN_FRAME_SIZE, Protocol.MAX_FRAME_SIZE)
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(Protocol.SERVICE_UUID) ?: run { Log.w(TAG, "margin service missing"); return }
            rx = svc.getCharacteristic(Protocol.RX_CHAR_UUID)
            ctrl = svc.getCharacteristic(Protocol.CTRL_CHAR_UUID)
            // TODO(M2): serialize: enable TX notify, then CTRL notify, then read CTRL status, then HELLO.
            enableNotify(g, svc.getCharacteristic(Protocol.TX_CHAR_UUID))
            enableNotify(g, ctrl)
            _state.value = State.READY
        }

        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            when (c.uuid) {
                Protocol.TX_CHAR_UUID -> onTxFrame(g, value)
                Protocol.CTRL_CHAR_UUID -> onCtrl(value)
            }
        }
    }

    private fun onTxFrame(g: BluetoothGatt, frame: ByteArray) {
        val res = reassembler.feed(frame)
        val ch = ctrl ?: return
        when (res) {
            is FeedResult.Complete -> incoming.trySend(res.message)
            is FeedResult.Error -> {
                g.writeCharacteristic(ch, Ctrl.nack(reassembler.msgId, res.reason, reassembler.nextFrag),
                    BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                return
            }
            else -> {}
        }
        if (reassembler.ackRequested) {
            g.writeCharacteristic(ch, Ctrl.ack(reassembler.msgId, reassembler.nextFrag, window),
                BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        }
    }

    @Synchronized
    private fun onCtrl(value: ByteArray) {
        val f = outgoing ?: return
        when (val p = Ctrl.decode(value)) {
            is Ctrl.Ack -> if (p.msgId == f.msgId) { f.onAck(p.nextFrag); window = p.credits.coerceAtLeast(1) }
            is Ctrl.Nack -> if (p.msgId == f.msgId) f.rewindTo(p.nextFrag)
            else -> {}
        }
        pump()
    }

    private fun enableNotify(g: BluetoothGatt, c: BluetoothGattCharacteristic?) {
        c ?: return
        g.setCharacteristicNotification(c, true)
        c.getDescriptor(CCCD)?.let { g.writeDescriptor(it, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) }
    }

    companion object {
        private const val TAG = "MarginBle"
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
}
