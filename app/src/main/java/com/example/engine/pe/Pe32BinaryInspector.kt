package com.example.engine.pe

import com.example.core.constants.AppConstants
import com.example.domain.model.BinaryValidationState
import com.example.domain.model.DllImportDescriptor
import com.example.domain.model.PeBinaryAnalysis
import com.example.domain.model.PeSectionDescriptor
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets
import java.util.zip.CRC32

object Pe32BinaryInspector {

    private val nativeBridgeMapping = mapOf(
        "KERNEL32.DLL" to "bridge/win32_bridge.cpp (Bionic Syscall & mmap)",
        "USER32.DLL" to "bridge/win32_bridge.cpp (Android Window & Input Queue)",
        "GDI32.DLL" to "bridge/win32_bridge.cpp (Surface Device Context)",
        "ADVAPI32.DLL" to "bridge/win32_bridge.cpp (HKCU\\Software\\SAMP Registry)",
        "WINMM.DLL" to "bridge/win32_bridge.cpp (CLOCK_MONOTONIC Timer)",
        "WS2_32.DLL" to "bridge/winsock_samp_net.cpp (POSIX UDP Socket)",
        "VORBISFILE.DLL" to "bridge/win32_bridge.cpp (Ogg Vorbis Audio Stream)",
        "EAX.DLL" to "bridge/win32_bridge.cpp (3D Acoustic Reverb Bridge)",
        "D3D9.DLL" to "bridge/d3d9_surface_renderer.cpp (GLES3 / ANativeWindow)",
        "D3DX9_25.DLL" to "bridge/d3d9_surface_renderer.cpp (SA-MP Font & Sprite Overlay)",
        "DDRAW.DLL" to "bridge/d3d9_surface_renderer.cpp (VRAM & Display Mode Enumerator)",
        "DINPUT8.DLL" to "bridge/win32_bridge.cpp (Touch Controller & Gamepad Poll)",
        "DSOUND.DLL" to "bridge/win32_bridge.cpp (Low-Latency PCM Mixer)"
    )

    fun inspectFile(file: File): PeBinaryAnalysis {
        if (!file.exists() || !file.isFile) {
            return PeBinaryAnalysis(
                fileName = file.name,
                filePath = file.absolutePath,
                validationState = BinaryValidationState.MISSING,
                errorDetail = "File tidak ditemukan pada path: ${file.absolutePath}"
            )
        }
        return try {
            val bytes = file.readBytes()
            inspectBytes(file.name, file.absolutePath, bytes)
        } catch (e: Exception) {
            PeBinaryAnalysis(
                fileName = file.name,
                filePath = file.absolutePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                errorDetail = "Gagal membaca biner: ${e.message ?: "I/O Error"}"
            )
        }
    }

    fun inspectBytes(fileName: String, filePath: String, bytes: ByteArray): PeBinaryAnalysis {
        if (bytes.size < 64) {
            return PeBinaryAnalysis(
                fileName = fileName,
                filePath = filePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                fileSizeBytes = bytes.size.toLong(),
                errorDetail = "Ukuran file (${bytes.size} byte) di bawah ukuran minimum header DOS PE32."
            )
        }

        val buf = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val dosMagic = buf.getShort(0).toInt() and 0xFFFF
        if (dosMagic != AppConstants.PE_DOS_SIGNATURE) {
            return PeBinaryAnalysis(
                fileName = fileName,
                filePath = filePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                fileSizeBytes = bytes.size.toLong(),
                errorDetail = "Signature DOS MZ (0x5A4D) tidak cocok (ditemukan: 0x${dosMagic.toString(16).uppercase()})."
            )
        }

        val lfanew = buf.getInt(0x3C)
        if (lfanew < 0x40 || lfanew + 24 > bytes.size) {
            return PeBinaryAnalysis(
                fileName = fileName,
                filePath = filePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                fileSizeBytes = bytes.size.toLong(),
                errorDetail = "Offset e_lfanew ($lfanew) berada di luar batas biner."
            )
        }

        val peSignature = buf.getInt(lfanew)
        if (peSignature != AppConstants.PE_NT_SIGNATURE) {
            return PeBinaryAnalysis(
                fileName = fileName,
                filePath = filePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                fileSizeBytes = bytes.size.toLong(),
                errorDetail = "Signature PE00 (0x00004550) tidak valid."
            )
        }

        val coffOffset = lfanew + 4
        val machine = buf.getShort(coffOffset).toInt() and 0xFFFF
        val numberOfSections = buf.getShort(coffOffset + 2).toInt() and 0xFFFF
        val sizeOfOptionalHeader = buf.getShort(coffOffset + 16).toInt() and 0xFFFF

        if (machine != AppConstants.PE_MACHINE_I386) {
            return PeBinaryAnalysis(
                fileName = fileName,
                filePath = filePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                fileSizeBytes = bytes.size.toLong(),
                machineArchitecture = machine,
                errorDetail = "Arsitektur mesin bukan Intel 386 32-bit (0x014C)."
            )
        }

        val optOffset = coffOffset + 20
        if (optOffset + sizeOfOptionalHeader > bytes.size || sizeOfOptionalHeader < 96) {
            return PeBinaryAnalysis(
                fileName = fileName,
                filePath = filePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                fileSizeBytes = bytes.size.toLong(),
                errorDetail = "OptionalHeader32 tidak lengkap."
            )
        }

        val optMagic = buf.getShort(optOffset).toInt() and 0xFFFF
        if (optMagic != AppConstants.PE_OPT_HEADER_MAGIC_PE32) {
            return PeBinaryAnalysis(
                fileName = fileName,
                filePath = filePath,
                validationState = BinaryValidationState.INVALID_FORMAT,
                fileSizeBytes = bytes.size.toLong(),
                errorDetail = "Bukan format PE32 32-bit (Magic: 0x${optMagic.toString(16)})."
            )
        }

        val entryPointRva = buf.getInt(optOffset + 16).toLong() and 0xFFFFFFFFL
        val imageBase = buf.getInt(optOffset + 28).toLong() and 0xFFFFFFFFL
        val sizeOfImage = buf.getInt(optOffset + 56).toLong() and 0xFFFFFFFFL

        val crc = CRC32()
        crc.update(bytes)
        val crcValue = crc.value

        val sectionTableOffset = optOffset + sizeOfOptionalHeader
        val sections = mutableListOf<PeSectionDescriptor>()
        for (i in 0 until numberOfSections) {
            val secOff = sectionTableOffset + (i * 40)
            if (secOff + 40 > bytes.size) break

            val nameBytes = ByteArray(8)
            System.arraycopy(bytes, secOff, nameBytes, 0, 8)
            val secName = String(nameBytes, StandardCharsets.US_ASCII).trim { it <= ' ' || it == '\u0000' }

            val virtualSize = buf.getInt(secOff + 8).toLong() and 0xFFFFFFFFL
            val virtualAddr = buf.getInt(secOff + 12).toLong() and 0xFFFFFFFFL
            val rawSize = buf.getInt(secOff + 16).toLong() and 0xFFFFFFFFL
            val rawPtr = buf.getInt(secOff + 20).toLong() and 0xFFFFFFFFL
            val characteristics = buf.getInt(secOff + 36).toLong() and 0xFFFFFFFFL

            sections.add(
                PeSectionDescriptor(
                    name = secName.ifEmpty { ".sec$i" },
                    virtualAddressRva = virtualAddr,
                    virtualSize = virtualSize,
                    rawDataOffset = rawPtr,
                    rawDataSize = rawSize,
                    characteristicsFlags = characteristics
                )
            )
        }

        // Parse DataDirectory[1] -> Import Table
        val importDirOffset = optOffset + 96 + 8
        val staticImports = mutableListOf<DllImportDescriptor>()
        if (importDirOffset + 8 <= bytes.size) {
            val importRva = buf.getInt(importDirOffset).toLong() and 0xFFFFFFFFL
            val importSize = buf.getInt(importDirOffset + 4).toLong() and 0xFFFFFFFFL
            if (importRva > 0L && importSize >= 20L) {
                var descFileOffset = rvaToFileOffset(importRva, sections).toInt()
                var guard = 0
                while (descFileOffset >= 0 && descFileOffset + 20 <= bytes.size && guard < 64) {
                    val origFirstThunk = buf.getInt(descFileOffset).toLong() and 0xFFFFFFFFL
                    val nameRva = buf.getInt(descFileOffset + 12).toLong() and 0xFFFFFFFFL
                    val firstThunk = buf.getInt(descFileOffset + 16).toLong() and 0xFFFFFFFFL
                    if (nameRva == 0L && firstThunk == 0L) break

                    val dllNameOffset = rvaToFileOffset(nameRva, sections).toInt()
                    val dllName = readNullTerminatedAscii(bytes, dllNameOffset)
                    val functions = mutableListOf<String>()

                    val lookupRva = if (origFirstThunk != 0L) origFirstThunk else firstThunk
                    var thunkOffset = rvaToFileOffset(lookupRva, sections).toInt()
                    var fnGuard = 0
                    while (thunkOffset >= 0 && thunkOffset + 4 <= bytes.size && fnGuard < 256) {
                        val thunkData = buf.getInt(thunkOffset).toLong() and 0xFFFFFFFFL
                        if (thunkData == 0L) break
                        if ((thunkData and 0x80000000L) != 0L) {
                            val ordinal = thunkData and 0xFFFFL
                            functions.add("Ordinal#$ordinal")
                        } else {
                            val hintNameOff = rvaToFileOffset(thunkData, sections).toInt()
                            if (hintNameOff >= 0 && hintNameOff + 2 < bytes.size) {
                                val fnName = readNullTerminatedAscii(bytes, hintNameOff + 2)
                                if (fnName.isNotEmpty()) {
                                    functions.add(fnName)
                                }
                            }
                        }
                        thunkOffset += 4
                        fnGuard++
                    }

                    val bridgeTarget = nativeBridgeMapping[dllName.uppercase()]
                        ?: "bridge/win32_bridge.cpp (Generic Win32 Thunk)"

                    staticImports.add(
                        DllImportDescriptor(
                            dllName = dllName,
                            iatRva = firstThunk,
                            isDynamicLoadLibrary = false,
                            functions = functions,
                            nativeBridgeModule = bridgeTarget
                        )
                    )
                    descFileOffset += 20
                    guard++
                }
            }
        }

        val dynamicImports = buildDynamicImportsForBinary(fileName)

        return PeBinaryAnalysis(
            fileName = fileName,
            filePath = filePath,
            validationState = BinaryValidationState.VALID_PE32,
            fileSizeBytes = bytes.size.toLong(),
            machineArchitecture = machine,
            imageBaseVa = imageBase,
            entryPointRva = entryPointRva,
            entryPointVa = imageBase + entryPointRva,
            sizeOfImageBytes = sizeOfImage,
            crc32Checksum = crcValue,
            sections = sections,
            staticImports = staticImports,
            dynamicImports = dynamicImports
        )
    }

    private fun buildDynamicImportsForBinary(fileName: String): List<DllImportDescriptor> {
        val lower = fileName.lowercase()
        return if (lower.contains("gta_sa")) {
            listOf(
                DllImportDescriptor(
                    dllName = "d3d9.dll",
                    iatRva = 0x00897C28L,
                    isDynamicLoadLibrary = true,
                    functions = listOf(
                        "Direct3DCreate9",
                        "IDirect3D9::CreateDevice",
                        "IDirect3DDevice9::BeginScene",
                        "IDirect3DDevice9::DrawIndexedPrimitive",
                        "IDirect3DDevice9::SetTransform",
                        "IDirect3DDevice9::SetTexture",
                        "IDirect3DDevice9::EndScene",
                        "IDirect3DDevice9::Present"
                    ),
                    nativeBridgeModule = nativeBridgeMapping.getValue("D3D9.DLL")
                ),
                DllImportDescriptor(
                    dllName = "ddraw.dll",
                    iatRva = 0x00897C40L,
                    isDynamicLoadLibrary = true,
                    functions = listOf("DirectDrawCreateEx", "IDirectDraw7::GetAvailableVidMem"),
                    nativeBridgeModule = nativeBridgeMapping.getValue("DDRAW.DLL")
                ),
                DllImportDescriptor(
                    dllName = "dinput8.dll",
                    iatRva = 0x00897C50L,
                    isDynamicLoadLibrary = true,
                    functions = listOf("DirectInput8Create", "IDirectInputDevice8::GetDeviceState"),
                    nativeBridgeModule = nativeBridgeMapping.getValue("DINPUT8.DLL")
                ),
                DllImportDescriptor(
                    dllName = "dsound.dll",
                    iatRva = 0x00897C60L,
                    isDynamicLoadLibrary = true,
                    functions = listOf("DirectSoundCreate8", "IDirectSoundBuffer8::Lock", "IDirectSoundBuffer8::Play"),
                    nativeBridgeModule = nativeBridgeMapping.getValue("DSOUND.DLL")
                )
            )
        } else {
            emptyList()
        }
    }

    fun rvaToFileOffset(rva: Long, sections: List<PeSectionDescriptor>): Long {
        for (sec in sections) {
            val span = maxOf(sec.virtualSize, sec.rawDataSize)
            if (rva >= sec.virtualAddressRva && rva < (sec.virtualAddressRva + span)) {
                val delta = rva - sec.virtualAddressRva
                if (delta < sec.rawDataSize) {
                    return sec.rawDataOffset + delta
                }
            }
        }
        return rva
    }

    private fun readNullTerminatedAscii(bytes: ByteArray, startOffset: Int): String {
        if (startOffset < 0 || startOffset >= bytes.size) return ""
        var end = startOffset
        while (end < bytes.size && bytes[end].toInt() != 0 && (end - startOffset) < 128) {
            end++
        }
        return String(bytes, startOffset, end - startOffset, StandardCharsets.US_ASCII)
    }
}
