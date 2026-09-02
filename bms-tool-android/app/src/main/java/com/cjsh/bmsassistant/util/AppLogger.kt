package com.cjsh.bmsassistant.util

import android.content.Context
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class AppLogger(context: Context) {
    private val lock = Any()
    private val timeFmt = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)
    private val fileFmt = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US)
    val file: File
    @Volatile var listener: ((String) -> Unit)? = null

    init {
        val dir = File(context.filesDir, "logs").apply { mkdirs() }
        file = File(dir, "BmsAssistant_${fileFmt.format(Date())}.log")
        file.appendText("BMS Assistant Android diagnostic log\n")
    }

    fun log(category: String, message: String) {
        val line = "${timeFmt.format(Date())} [$category] $message"
        synchronized(lock) { file.appendText(line + "\n") }
        listener?.invoke(line)
    }
}
