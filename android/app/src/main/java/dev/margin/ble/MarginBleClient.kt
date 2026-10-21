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
import android.os.Handler
import android.os.Looper
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
import java.util.LinkedList
import java.util.UUID
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * GATT client for the margin service (docs/BLE_PROTOCOL.md).
 *
 * Features:
 * - Serialized GATT operation queue (Android allows only one outstanding op)
 * - MTU negotiation for efficient transfers
 * - Chunked message sending with ACK/NACK flow control
 * - Automatic reconnection with exponential backoff
 * - TX characteristic notification subscription
 */
@SuppressLint("MissingPermission")
class MarginBleClient(private val context: Context) {
    enum class State { DISCONNECTED, CONNECTING, DISCOVERING, READY }

    private val _state = MutableStateFlow(State.DISCONNECTED)
    val state: StateFlow<State> = _state

    private val incoming = Channel<Message>(Channel.BUFFERED)
    val messages = incoming.receiveAsFlow()

    private var gatt: BluetoothGatt? = null
    private var rx: BluetoothGattCharacteristic? = null
    private var tx: BluetoothGattCharacteristic? = null
    private var ctrl: BluetoothGattCharacteristic? = null

    private var frameSize = Protocol.MIN_FRAME_SIZE
    private var window = Protocol.DEFAULT_WINDOW
    private val reassembler = Reassembler()
    private var outgoing: Fragmenter? = null
    private var nextMsgId = 0

    private val handler = Handler(Looper.getMainLooper())
    private val gattQueue = ConcurrentLinkedQueue<GattOp>()
    private var gattOpInProgress = false

    private var connectedDevice: BluetoothDevice? = null
    private var reconnectAttempt = 0
    private val maxReconnectAttempts = 10
    private var shouldReconnect = true

    private val messageQueue = LinkedList<Pair<MsgType, ByteArray>>()
    private var ackTimeoutRunnable: Runnable? = null
    private var lastTxProgressMs = 0L

    private sealed class GattOp {
        data class Write(val char: BluetoothGattCharacteristic, val data: ByteArray, val type: Int) : GattOp()
        data class EnableNotify(val char: BluetoothGattCharacteristic) : GattOp()
        data class ReadDescriptor(val desc: BluetoothGattDescriptor) : GattOp()
        object DiscoverServices : GattOp()
        data class RequestMtu(val mtu: Int) : GattOp()
    }

    fun connect(device: BluetoothDevice) {
        shouldReconnect = true
        connectedDevice = device
        reconnectAttempt = 0
        doConnect(device)
    }

    private fun doConnect(device: BluetoothDevice) {
        _state.value = State.CONNECTING
        gattQueue.clear()
        gattOpInProgress = false
        gatt?.close()
        gatt = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
    }

    fun disconnect() {
        shouldReconnect = false
        connectedDevice = null
        handler.removeCallbacksAndMessages(null)
        ackTimeoutRunnable = null
        gatt?.close()
        gatt = null
        _state.value = State.DISCONNECTED
    }

    @Synchronized
    fun send(type: MsgType, body: ByteArray): Boolean {
        if (_state.value != State.READY) {
            Log.w(TAG, "Cannot send: not ready (state=${_state.value})")
            return false
        }

        if (outgoing?.done == false) {
            messageQueue.add(type to body)
            Log.d(TAG, "Message queued (${messageQueue.size} in queue)")
            return true
        }

        return startSending(type, body)
    }

    @Synchronized
    private fun startSending(type: MsgType, body: ByteArray): Boolean {
        outgoing = Fragmenter(nextMsgId++ and 0xFF, type, body, frameSize, window)
        lastTxProgressMs = System.currentTimeMillis()
        pump()
        startAckTimeout()
        return true
    }

    @Synchronized
    private fun pump() {
        val f = outgoing ?: return
        val g = gatt ?: return
        val ch = rx ?: return

        if (f.done) {
            outgoing = null
            cancelAckTimeout()

            val next = messageQueue.poll()
            if (next != null) {
                startSending(next.first, next.second)
            }
            return
        }

        while (f.canSend) {
            val frame = f.next() ?: break
            enqueueWrite(ch, frame, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
        }
    }

    private fun startAckTimeout() {
        cancelAckTimeout()
        ackTimeoutRunnable = Runnable {
            synchronized(this) {
                val f = outgoing ?: return@Runnable
                if (!f.done && System.currentTimeMillis() - lastTxProgressMs > ACK_TIMEOUT_MS) {
                    Log.w(TAG, "ACK timeout, rewinding to ${f.acked}")
                    f.rewindTo(f.acked)
                    lastTxProgressMs = System.currentTimeMillis()
                    pump()
                    startAckTimeout()
                }
            }
        }.also {
            handler.postDelayed(it, ACK_TIMEOUT_MS)
        }
    }

    private fun cancelAckTimeout() {
        ackTimeoutRunnable?.let { handler.removeCallbacks(it) }
        ackTimeoutRunnable = null
    }

    private fun enqueueWrite(char: BluetoothGattCharacteristic, data: ByteArray, type: Int) {
        gattQueue.add(GattOp.Write(char, data, type))
        processGattQueue()
    }

    private fun enqueueNotify(char: BluetoothGattCharacteristic) {
        gattQueue.add(GattOp.EnableNotify(char))
        processGattQueue()
    }

    @Synchronized
    private fun processGattQueue() {
        if (gattOpInProgress) return
        val op = gattQueue.poll() ?: return
        val g = gatt ?: return

        gattOpInProgress = true

        when (op) {
            is GattOp.Write -> {
                val rc = g.writeCharacteristic(op.char, op.data, op.type)
                if (rc != BluetoothStatusCodes.SUCCESS) {
                    Log.w(TAG, "writeCharacteristic failed: $rc")
                    gattOpInProgress = false
                    handler.post { processGattQueue() }
                }
            }
            is GattOp.EnableNotify -> {
                g.setCharacteristicNotification(op.char, true)
                val desc = op.char.getDescriptor(CCCD)
                if (desc != null) {
                    val rc = g.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                    if (rc != BluetoothStatusCodes.SUCCESS) {
                        Log.w(TAG, "writeDescriptor failed: $rc")
                        gattOpInProgress = false
                        handler.post { processGattQueue() }
                    }
                } else {
                    gattOpInProgress = false
                    handler.post { processGattQueue() }
                }
            }
            is GattOp.DiscoverServices -> {
                if (!g.discoverServices()) {
                    Log.w(TAG, "discoverServices failed")
                    gattOpInProgress = false
                }
            }
            is GattOp.RequestMtu -> {
                if (!g.requestMtu(op.mtu)) {
                    Log.w(TAG, "requestMtu failed")
                    gattOpInProgress = false
                }
            }
            is GattOp.ReadDescriptor -> {
                if (!g.readDescriptor(op.desc)) {
                    Log.w(TAG, "readDescriptor failed")
                    gattOpInProgress = false
                }
            }
        }
    }

    private fun onGattOpComplete() {
        gattOpInProgress = false
        handler.post { processGattQueue() }
    }

    private fun scheduleReconnect() {
        if (!shouldReconnect) return
        val device = connectedDevice ?: return

        reconnectAttempt++
        if (reconnectAttempt > maxReconnectAttempts) {
            Log.w(TAG, "Max reconnect attempts reached")
            return
        }

        val delayMs = minOf(1000L * (1 shl (reconnectAttempt - 1)), 30000L)
        Log.d(TAG, "Scheduling reconnect in ${delayMs}ms (attempt $reconnectAttempt)")

        handler.postDelayed({
            if (shouldReconnect && _state.value == State.DISCONNECTED) {
                doConnect(device)
            }
        }, delayMs)
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            Log.d(TAG, "onConnectionStateChange: status=$status, newState=$newState")

            if (newState == BluetoothProfile.STATE_CONNECTED) {
                reconnectAttempt = 0
                _state.value = State.DISCOVERING
                gattQueue.add(GattOp.RequestMtu(Protocol.MAX_FRAME_SIZE + 3))
                processGattQueue()
            } else {
                reassembler.reset()
                outgoing = null
                messageQueue.clear()
                cancelAckTimeout()
                _state.value = State.DISCONNECTED

                gatt?.close()
                gatt = null

                scheduleReconnect()
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            Log.d(TAG, "onMtuChanged: mtu=$mtu, status=$status")
            onGattOpComplete()

            frameSize = (mtu - 3).coerceIn(Protocol.MIN_FRAME_SIZE, Protocol.MAX_FRAME_SIZE)
            Log.d(TAG, "Frame size set to $frameSize")

            gattQueue.add(GattOp.DiscoverServices)
            processGattQueue()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            Log.d(TAG, "onServicesDiscovered: status=$status")
            onGattOpComplete()

            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Service discovery failed")
                g.disconnect()
                return
            }

            val svc = g.getService(Protocol.SERVICE_UUID)
            if (svc == null) {
                Log.e(TAG, "margin service not found")
                g.disconnect()
                return
            }

            rx = svc.getCharacteristic(Protocol.RX_CHAR_UUID)
            tx = svc.getCharacteristic(Protocol.TX_CHAR_UUID)
            ctrl = svc.getCharacteristic(Protocol.CTRL_CHAR_UUID)

            if (rx == null || tx == null || ctrl == null) {
                Log.e(TAG, "Required characteristics not found")
                g.disconnect()
                return
            }

            enqueueNotify(tx!!)
            enqueueNotify(ctrl!!)

            handler.postDelayed({
                if (_state.value == State.DISCOVERING) {
                    _state.value = State.READY
                    Log.i(TAG, "BLE connection ready")
                }
            }, 500)
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            c: BluetoothGattCharacteristic,
            status: Int
        ) {
            onGattOpComplete()
            if (status == BluetoothGatt.GATT_SUCCESS) {
                lastTxProgressMs = System.currentTimeMillis()
            } else {
                Log.w(TAG, "Characteristic write failed: $status")
            }
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            d: BluetoothGattDescriptor,
            status: Int
        ) {
            Log.d(TAG, "onDescriptorWrite: status=$status")
            onGattOpComplete()
        }

        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            c: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
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
            is FeedResult.Complete -> {
                incoming.trySend(res.message)
            }
            is FeedResult.Error -> {
                val nack = Ctrl.nack(reassembler.msgId, res.reason, reassembler.nextFrag)
                enqueueWrite(ch, nack, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                return
            }
            else -> {}
        }

        if (reassembler.ackRequested) {
            val ack = Ctrl.ack(reassembler.msgId, reassembler.nextFrag, window)
            enqueueWrite(ch, ack, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        }
    }

    @Synchronized
    private fun onCtrl(value: ByteArray) {
        val f = outgoing ?: return

        when (val p = Ctrl.decode(value)) {
            is Ctrl.Ack -> {
                if (p.msgId == f.msgId) {
                    f.onAck(p.nextFrag)
                    window = p.credits.coerceAtLeast(1)
                    lastTxProgressMs = System.currentTimeMillis()
                    pump()
                }
            }
            is Ctrl.Nack -> {
                if (p.msgId == f.msgId) {
                    Log.w(TAG, "NACK received: reason=${p.reason}, nextFrag=${p.nextFrag}")
                    f.rewindTo(p.nextFrag)
                    pump()
                }
            }
            else -> {}
        }
    }

    fun isConnected(): Boolean = _state.value == State.READY

    /**
     * Send a pre-built frame (including header). Parses the type and body from the frame.
     */
    @Synchronized
    fun sendFrame(frame: ByteArray): Boolean {
        if (frame.size < Protocol.MSG_HEADER_SIZE) {
            Log.w(TAG, "Frame too small: ${frame.size}")
            return false
        }
        val typeCode = frame[1].toInt() and 0xFF
        val type = MsgType.of(typeCode) ?: run {
            Log.w(TAG, "Unknown message type: $typeCode")
            return false
        }
        val body = frame.copyOfRange(Protocol.MSG_HEADER_SIZE, frame.size)
        return send(type, body)
    }

    companion object {
        private const val TAG = "MarginBle"
        private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        private const val ACK_TIMEOUT_MS = 2000L

        @Volatile
        private var instance: MarginBleClient? = null

        fun getInstance(context: Context): MarginBleClient {
            return instance ?: synchronized(this) {
                instance ?: MarginBleClient(context.applicationContext).also { instance = it }
            }
        }
    }
}
