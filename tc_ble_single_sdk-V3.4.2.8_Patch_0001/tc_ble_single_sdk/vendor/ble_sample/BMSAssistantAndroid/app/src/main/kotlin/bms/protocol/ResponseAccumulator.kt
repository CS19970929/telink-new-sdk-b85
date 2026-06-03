package com.telink.bmsassistant.protocol

data class AccumulatorEvent(
    val state: String,
    val frame: ByteArray = byteArrayOf(),
    val expectedLength: Int? = null,
    val fragments: Int = 0,
) {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is AccumulatorEvent) return false
        return state == other.state &&
            frame.contentEquals(other.frame) &&
            expectedLength == other.expectedLength &&
            fragments == other.fragments
    }

    override fun hashCode(): Int {
        var result = state.hashCode()
        result = 31 * result + frame.contentHashCode()
        result = 31 * result + (expectedLength ?: 0)
        result = 31 * result + fragments
        return result
    }
}

class ResponseAccumulator {
    var buffer: ByteArray = byteArrayOf()
        private set
    private var expectedLengthHint: Int? = null
    private var fragments: Int = 0

    fun reset(expectedLengthHint: Int? = null) {
        buffer = byteArrayOf()
        fragments = 0
        this.expectedLengthHint = expectedLengthHint
    }

    fun append(fragment: ByteArray): AccumulatorEvent {
        fragments += 1
        buffer += fragment
        val expectedLength = BmsModbusCodec.inferExpectedLength(buffer, expectedLengthHint)
        if (expectedLength == null || buffer.size < expectedLength) {
            return AccumulatorEvent(
                state = "waiting",
                expectedLength = expectedLength,
                fragments = fragments,
            )
        }

        val frame = buffer.copyOf(expectedLength)
        val frameFragments = fragments
        val valid = BmsModbusCodec.validateCrc(frame)
        reset(expectedLengthHint)
        return AccumulatorEvent(
            state = if (valid) "complete" else "invalid_crc",
            frame = frame,
            expectedLength = expectedLength,
            fragments = frameFragments,
        )
    }
}
