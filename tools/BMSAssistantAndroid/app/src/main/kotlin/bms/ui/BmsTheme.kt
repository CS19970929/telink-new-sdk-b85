package com.telink.bmsassistant.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val BmsColorScheme = lightColorScheme(
    primary = Color(0xFF0F766E),
    onPrimary = Color.White,
    secondary = Color(0xFF2563EB),
    tertiary = Color(0xFFB45309),
    background = Color(0xFFF6F8FB),
    surface = Color.White,
    surfaceVariant = Color(0xFFE8EEF3),
    onSurface = Color(0xFF1F2937),
)

@Composable
fun BmsTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = BmsColorScheme,
        content = content,
    )
}
