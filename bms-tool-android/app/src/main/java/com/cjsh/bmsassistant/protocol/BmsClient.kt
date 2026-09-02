package com.cjsh.bmsassistant.protocol

import com.cjsh.bmsassistant.ble.BleBmsSession
import com.cjsh.bmsassistant.model.BatterySnapshot
import com.cjsh.bmsassistant.model.DeviceIdentity
import java.io.IOException
import java.nio.charset.StandardCharsets
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class BmsClient(
    private val session: BleBmsSession,
    private val log: (String, String) -> Unit
) : AutoCloseable {
    private val transactionLock = Any()
    private val rxLock = Any()
    private val rx = ArrayList<Byte>()
    @Volatile private var pendingLatch: CountDownLatch? = null
    @Volatile private var pendingResponse: ByteArray? = null
    @Volatile private var pendingError: Exception? = null

    init { session.bmsNotificationListener = { onData(it) } }

    fun probe(timeoutMs: Long = 1800) {
        val rsp = transact(ModbusRtu.readHolding(BmsRegisters.REALTIME, 2), timeoutMs)
        val words = ModbusRtu.parseRead(rsp, 2)
        if (words.size < 2 || words[0] != BmsRegisters.REALTIME_MAGIC) {
            throw IOException("BMS Modbus probe failed: realtime magic not found")
        }
        log("MODBUS", "PROBE_OK magic=0x%04X protocol=0x%04X".format(words[0], words[1]))
    }

    fun readRegisters(start: Int, quantity: Int, timeoutMs: Long = 4000): IntArray {
        val rsp = transact(ModbusRtu.readHolding(start, quantity), timeoutMs)
        return ModbusRtu.parseRead(rsp, quantity)
    }

    fun writeSingle(register: Int, value: Int) {
        val rsp = transact(ModbusRtu.writeSingle(register, value), 4000)
        ModbusRtu.validateWriteSingleAck(rsp, register, value)
    }

    fun writeSingleVerify(register: Int, value: Int): Int {
        writeSingle(register, value)
        val readback = readRegisters(register, 1)[0]
        if (readback != value) throw IOException("Register verify failed at 0x%04X: wrote %d read %d".format(register, value, readback))
        return readback
    }

    fun writeMultiple(start: Int, values: IntArray) {
        val request = ModbusRtu.writeMultiple(start, values)
        if (request.size > 20 && session.negotiatedMtu <= 23) throw IOException("Modbus write request ${request.size} bytes exceeds MTU23 payload")
        val rsp = transact(request, 4000)
        ModbusRtu.validateWriteMultipleAck(rsp, start, values.size)
    }

    fun readIdentity(): DeviceIdentity {
        val mac = readRegisters(BmsRegisters.MAC, 3)
        val sn = readRegisters(BmsRegisters.SERIAL, 16)
        val hw = readRegisters(BmsRegisters.HARDWARE, 16)
        val sw = readRegisters(BmsRegisters.SOFTWARE, 16)
        val name = readRegisters(BmsRegisters.BT_NAME, BmsRegisters.BT_NAME_READ_WORDS)
        val macBytes = mac.flatMap { listOf((it ushr 8) and 0xFF, it and 0xFF) }.take(6)
        return DeviceIdentity(
            macBytes.joinToString(":") { "%02X".format(it) },
            ModbusRtu.decodeAscii(sn), ModbusRtu.decodeAscii(hw), ModbusRtu.decodeAscii(sw), ModbusRtu.decodeAscii(name)
        )
    }

    fun readBattery(): BatterySnapshot {
        val legacy = readRegisters(BmsRegisters.LEGACY, 63)
        val status = readRegisters(BmsRegisters.SYSTEM_STATUS, 2)
        val realtime = readRegisters(BmsRegisters.REALTIME, 11)
        val rt = realtime.size >= 11 && realtime[0] == BmsRegisters.REALTIME_MAGIC

        var charge = signed16(legacy[50])
        val discharge = signed16(legacy[51])
        var current = if (discharge > 0) -discharge else charge
        var voltage = legacy[37]
        var soc = legacy[52]
        var maxTemp = legacy[48]
        var minTemp = legacy[49]
        var mosTemp = legacy[47]
        var maxCell = legacy[32]
        var minCell = legacy[33]
        var delta = legacy[36]
        if (rt) {
            voltage = realtime[2]
            current = signed16(realtime[3])
            soc = realtime[4]
            maxTemp = realtime[5]
            minTemp = realtime[6]
            mosTemp = realtime[7]
            maxCell = realtime[8]
            minCell = realtime[9]
            delta = realtime[10]
        }

        val cells = ArrayList<Int>()
        for (i in 0 until minOf(32, legacy.size)) {
            val mv = legacy[i]
            if (mv in 500..6000) cells.add(mv) else if (cells.isNotEmpty()) break
        }

        return BatterySnapshot(
            packVoltageV = voltage / 100.0,
            currentA = current / 10.0,
            socPercent = soc,
            sohPercent = legacy[53],
            maxTempC = maxTemp / 10.0 - 40.0,
            minTempC = minTemp / 10.0 - 40.0,
            mosTempC = mosTemp / 10.0 - 40.0,
            maxCellMv = maxCell,
            minCellMv = minCell,
            cellDeltaMv = delta,
            maxCellPosition = legacy[34],
            minCellPosition = legacy[35],
            cycleCount = legacy[57],
            capacityNowAh = legacy[54] / 100.0,
            capacityFullAh = legacy[55] / 100.0,
            capacityFactoryAh = legacy[56] / 100.0,
            systemStatus = (status[0].toLong() and 0xFFFF) or ((status[1].toLong() and 0xFFFF) shl 16),
            protocolVersion = if (rt) realtime[1] else 0,
            usesRealtimeWindow = rt,
            protectionLevel1Raw = legacy[58],
            protectionLevel2Raw = legacy[59],
            protectionLevel3Raw = legacy[60],
            cellMillivolts = cells
        )
    }

    fun readProtectionAll(): IntArray = readRegisters(BmsRegisters.PROTECT, BmsRegisters.PROTECT_COUNT)
    fun readAfeAll(): IntArray = readRegisters(BmsRegisters.AFE, BmsRegisters.AFE_COUNT)
    fun readEventLogs(): IntArray = readRegisters(BmsRegisters.EVENT_LOG, 100, 6000)

    fun setSocAndVerify(soc: Int): BatterySnapshot {
        require(soc in 0..100)
        writeSingle(0x1005, soc)
        Thread.sleep(180)
        val snap = readBattery()
        if (snap.socPercent != soc) throw IOException("SOC verify failed: wrote $soc device=${snap.socPercent}")
        return snap
    }

    fun setCycleAndVerify(cycle: Int): BatterySnapshot {
        require(cycle in 0..0xFFFF)
        writeSingle(0x2319, cycle)
        Thread.sleep(180)
        val snap = readBattery()
        if (snap.cycleCount != cycle) throw IOException("Cycle count verify failed: wrote $cycle device=${snap.cycleCount}")
        return snap
    }

    fun readBluetoothName(): String = ModbusRtu.decodeAscii(readRegisters(BmsRegisters.BT_NAME, BmsRegisters.BT_NAME_READ_WORDS))

    fun writeBluetoothNameSuffix(input: String): String {
        var suffix = input.trim()
        if (suffix.startsWith("BT_", true)) suffix = suffix.substring(3)
        require(suffix.isNotEmpty()) { "蓝牙名后缀不能为空" }
        val ascii = suffix.toByteArray(StandardCharsets.US_ASCII)
        require(ascii.size <= BmsRegisters.BT_NAME_MAX_SUFFIX_BYTES) { "蓝牙名后缀最多10个ASCII字节" }
        require(suffix.all { it.isLetterOrDigit() || it == '_' || it == '-' }) { "仅支持字母、数字、_、-" }
        val raw = if (ascii.size % 2 == 0) ascii else ascii.copyOf(ascii.size + 1)
        val values = IntArray(raw.size / 2) { i -> ((raw[i * 2].toInt() and 0xFF) shl 8) or (raw[i * 2 + 1].toInt() and 0xFF) }
        writeMultiple(BmsRegisters.BT_NAME, values)
        val readback = readBluetoothName()
        val expected = "BT_$suffix"
        if (readback != expected) throw IOException("蓝牙名回读不一致：期望 '$expected'，实际 '$readback'")
        return readback
    }

    private fun transact(request: ByteArray, timeoutMs: Long): ByteArray = synchronized(transactionLock) {
        val latch = CountDownLatch(1)
        synchronized(rxLock) {
            rx.clear(); pendingLatch = latch; pendingResponse = null; pendingError = null
        }
        log("MODBUS", "TX ${ModbusRtu.hex(request)}")
        try {
            session.writeBms(request)
            if (!latch.await(timeoutMs, TimeUnit.MILLISECONDS)) {
                val buffered = synchronized(rxLock) { ModbusRtu.hex(rx.toByteArray()) }
                log("MODBUS", "TIMEOUT ${timeoutMs}ms request=${ModbusRtu.hex(request)} buffered=$buffered")
                throw IOException("Modbus response timed out after ${timeoutMs}ms")
            }
            pendingError?.let { throw it }
            val rsp = pendingResponse ?: throw IOException("Modbus response completed without frame")
            log("MODBUS", "RX ${ModbusRtu.hex(rsp)}")
            rsp
        } finally {
            synchronized(rxLock) { if (pendingLatch === latch) pendingLatch = null; rx.clear() }
        }
    }

    private fun onData(fragment: ByteArray) {
        synchronized(rxLock) {
            val latch = pendingLatch ?: run { log("MODBUS", "UNSOLICITED ${ModbusRtu.hex(fragment)}"); return }
            fragment.forEach { rx.add(it) }
            val expected = ModbusRtu.inferExpectedLength(rx)
            log("MODBUS", "RX_FRAGMENT len=${fragment.size} accumulated=${rx.size} expected=${expected ?: -1}")
            if (expected != null && rx.size >= expected) {
                val frame = rx.take(expected).toByteArray()
                try { ModbusRtu.validateFrame(frame); pendingResponse = frame }
                catch (e: Exception) { pendingError = e }
                latch.countDown()
            }
        }
    }

    private fun signed16(v: Int): Int = if ((v and 0x8000) != 0) v - 0x10000 else v

    override fun close() {
        session.bmsNotificationListener = null
        synchronized(rxLock) { pendingLatch?.countDown(); pendingLatch = null; rx.clear() }
    }
}
