package com.example

import com.example.core.constants.AppConstants
import com.example.domain.model.BinaryValidationState
import com.example.engine.archive.Ver2ImgReader
import com.example.engine.network.SampUdpClient
import com.example.engine.pe.Pe32BinaryInspector
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class VenturaEngineTest {

    @Test
    fun verifyPe32BinaryInspectorAndVer2ImgReader() {
        val tempDir = Files.createTempDirectory("ventura_test_dir").toFile()
        try {
            val gtaExe = File(tempDir, AppConstants.FILE_GTA_SA_EXE)
            val peBuffer = ByteBuffer.allocate(2048).order(ByteOrder.LITTLE_ENDIAN)
            peBuffer.putShort(0, AppConstants.PE_DOS_SIGNATURE.toShort())
            peBuffer.putInt(0x3C, 0x80)
            peBuffer.putInt(0x80, AppConstants.PE_NT_SIGNATURE)
            peBuffer.putShort(0x84, AppConstants.PE_MACHINE_I386.toShort())
            peBuffer.putShort(0x86, 1.toShort()) // NumberOfSections = 1
            peBuffer.putShort(0x94, 0xE0.toShort()) // SizeOfOptionalHeader
            peBuffer.putShort(0x96, 0x010F.toShort()) // Characteristics
            peBuffer.putShort(0x98, AppConstants.PE_OPT_HEADER_MAGIC_PE32.toShort())
            peBuffer.putInt(0xA8, 0x00424570) // AddressOfEntryPoint RVA
            peBuffer.putInt(0xB4, AppConstants.GTASA_IMAGE_BASE.toInt())
            peBuffer.putInt(0xD0, 0x01600000) // SizeOfImage

            val sectionBase = 0x80 + 24 + 0xE0
            val secName = ".text".toByteArray(StandardCharsets.US_ASCII)
            for (i in secName.indices) {
                peBuffer.put(sectionBase + i, secName[i])
            }
            peBuffer.putInt(sectionBase + 8, 0x1000)
            peBuffer.putInt(sectionBase + 12, 0x1000)
            peBuffer.putInt(sectionBase + 16, 0x200)
            peBuffer.putInt(sectionBase + 20, 0x400)
            peBuffer.putInt(sectionBase + 36, 0x60000020)

            gtaExe.writeBytes(peBuffer.array())

            val gtaAnalysis = Pe32BinaryInspector.inspectFile(gtaExe)
            assertEquals(BinaryValidationState.VALID_PE32, gtaAnalysis.validationState)
            assertEquals(AppConstants.GTASA_IMAGE_BASE, gtaAnalysis.imageBaseVa)
            assertEquals(AppConstants.GTASA_ENTRY_POINT_VA, gtaAnalysis.entryPointVa)

            val modelsDir = File(tempDir, "models")
            modelsDir.mkdirs()
            val gta3Img = File(modelsDir, "gta3.img")
            val imgBuf = ByteBuffer.allocate(4096).order(ByteOrder.LITTLE_ENDIAN)
            val magic = AppConstants.IMG_MAGIC_VER2.toByteArray(StandardCharsets.US_ASCII)
            for (i in magic.indices) {
                imgBuf.put(i, magic[i])
            }
            imgBuf.putInt(4, 1) // 1 entry
            imgBuf.putInt(8, 1) // sector offset = 1 (2048)
            imgBuf.putShort(12, 1.toShort()) // 1 sector
            val entryName = "infernus.dff".toByteArray(StandardCharsets.US_ASCII)
            for (i in entryName.indices) {
                imgBuf.put(16 + i, entryName[i])
            }
            gta3Img.writeBytes(imgBuf.array())

            val imgReport = Ver2ImgReader.parseImgArchive(gta3Img)
            assertTrue(imgReport.isLoaded)
            assertEquals(AppConstants.IMG_MAGIC_VER2, imgReport.magicSignature)
            assertEquals(1, imgReport.dffModelCount)
        } finally {
            tempDir.deleteRecursively()
        }
    }

    @Test
    fun verifySampUdpLoopbackQueryProtocol() {
        try {
            val port = SampUdpClient.ensureLocalLoopbackResponder()
            val response = SampUdpClient.queryServer("127.0.0.1", port, timeoutMs = 2000)
            assertTrue(response.isOnline)
            assertTrue(response.hostname.contains("Ventura"))
            assertTrue(response.rules.isNotEmpty())
            assertTrue(response.onlinePlayers.isNotEmpty())
        } finally {
            SampUdpClient.shutdownLocalResponder()
        }
    }
}
