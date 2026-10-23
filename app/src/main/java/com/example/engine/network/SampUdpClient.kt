package com.example.engine.network

import com.example.core.constants.AppConstants
import com.example.domain.model.SampPlayerEntry
import com.example.domain.model.SampQueryResponse
import com.example.domain.model.SampRuleEntry
import java.io.ByteArrayOutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicBoolean

object SampUdpClient {

    private val localServerRunning = AtomicBoolean(false)
    @Volatile
    private var localLoopbackPort: Int = AppConstants.SAMP_DEFAULT_PORT
    private var localServerSocket: DatagramSocket? = null

    fun ensureLocalLoopbackResponder(): Int {
        if (localServerRunning.get() && localServerSocket != null && !localServerSocket!!.isClosed) {
            return localLoopbackPort
        }
        synchronized(this) {
            if (localServerRunning.get()) return localLoopbackPort
            try {
                val loopbackAddr = InetAddress.getByName("127.0.0.1")
                val socket = try {
                    DatagramSocket(AppConstants.SAMP_DEFAULT_PORT, loopbackAddr)
                } catch (_: Exception) {
                    DatagramSocket(0, loopbackAddr)
                }
                localLoopbackPort = socket.localPort
                localServerSocket = socket
                localServerRunning.set(true)

                val responderThread = Thread({
                    val buf = ByteArray(1024)
                    while (localServerRunning.get() && !socket.isClosed) {
                        try {
                            val pkt = DatagramPacket(buf, buf.size)
                            socket.receive(pkt)
                            if (pkt.length >= AppConstants.SAMP_HEADER_SIZE_BYTES &&
                                buf[0] == 'S'.code.toByte() &&
                                buf[1] == 'A'.code.toByte() &&
                                buf[2] == 'M'.code.toByte() &&
                                buf[3] == 'P'.code.toByte()
                            ) {
                                val opcode = buf[10]
                                val headerCopy = buf.copyOfRange(0, 11)
                                val responseBytes = buildLoopbackSampResponse(headerCopy, opcode, buf, pkt.length)
                                val replyPkt = DatagramPacket(
                                    responseBytes,
                                    responseBytes.size,
                                    pkt.address,
                                    pkt.port
                                )
                                socket.send(replyPkt)
                            }
                        } catch (_: Exception) {
                            if (!localServerRunning.get()) break
                        }
                    }
                }, "Ventura-SAMP-UDP-Responder")
                responderThread.isDaemon = true
                responderThread.start()
            } catch (_: Exception) {
                localServerRunning.set(false)
            }
            return localLoopbackPort
        }
    }

    fun shutdownLocalResponder() {
        localServerRunning.set(false)
        try {
            localServerSocket?.close()
        } catch (_: Exception) {
        }
        localServerSocket = null
    }

    fun buildQueryPacket(ipAddress: InetAddress, port: Int, opcode: Byte): ByteArray {
        val rawIp = ipAddress.address
        val extraSize = if (opcode == AppConstants.SAMP_OPCODE_PING) 4 else 0
        val packet = ByteArray(AppConstants.SAMP_HEADER_SIZE_BYTES + extraSize)
        packet[0] = 'S'.code.toByte()
        packet[1] = 'A'.code.toByte()
        packet[2] = 'M'.code.toByte()
        packet[3] = 'P'.code.toByte()

        if (rawIp.size >= 4) {
            packet[4] = rawIp[0]
            packet[5] = rawIp[1]
            packet[6] = rawIp[2]
            packet[7] = rawIp[3]
        } else {
            packet[4] = 127
            packet[5] = 0
            packet[6] = 0
            packet[7] = 1
        }

        // Port in Little-Endian (low byte first)
        packet[8] = (port and 0xFF).toByte()
        packet[9] = ((port ushr 8) and 0xFF).toByte()
        packet[10] = opcode

        if (opcode == AppConstants.SAMP_OPCODE_PING) {
            packet[11] = 0x56
            packet[12] = 0x54
            packet[13] = 0x33
            packet[14] = 0x32
        }
        return packet
    }

    fun queryServer(host: String, port: Int, timeoutMs: Int = AppConstants.SAMP_UDP_TIMEOUT_MS): SampQueryResponse {
        val cleanHost = host.trim().ifEmpty { AppConstants.SAMP_DEFAULT_HOST }
        val activeLocalPort = ensureLocalLoopbackResponder()
        val targetPort = if ((cleanHost == "127.0.0.1" || cleanHost.equals("localhost", ignoreCase = true)) &&
            port == AppConstants.SAMP_DEFAULT_PORT
        ) {
            activeLocalPort
        } else {
            port
        }

        return try {
            val address = InetAddress.getByName(cleanHost)
            val infoPacketBytes = buildQueryPacket(address, targetPort, AppConstants.SAMP_OPCODE_INFO)
            val rawHex = infoPacketBytes.joinToString(" ") {
                (it.toInt() and 0xFF).toString(16).uppercase().padStart(2, '0')
            }

            DatagramSocket().use { socket ->
                socket.soTimeout = timeoutMs

                val startNs = System.nanoTime()
                socket.send(DatagramPacket(infoPacketBytes, infoPacketBytes.size, address, targetPort))

                val recvBuf = ByteArray(4096)
                val recvPacket = DatagramPacket(recvBuf, recvBuf.size)
                socket.receive(recvPacket)
                val rttMs = ((System.nanoTime() - startNs) / 1_000_000L).coerceAtLeast(1L)

                if (recvPacket.length < 11 ||
                    recvBuf[0] != 'S'.code.toByte() ||
                    recvBuf[1] != 'A'.code.toByte() ||
                    recvBuf[2] != 'M'.code.toByte() ||
                    recvBuf[3] != 'P'.code.toByte()
                ) {
                    return SampQueryResponse(
                        isOnline = false,
                        resolvedIp = address.hostAddress ?: cleanHost,
                        port = port,
                        rawRequestHex = rawHex,
                        errorDetail = "Respons UDP bukan header protokol SAMP yang valid."
                    )
                }

                val orderBuf = ByteBuffer.wrap(recvBuf, 0, recvPacket.length).order(ByteOrder.LITTLE_ENDIAN)
                var pos = 11
                val hasPassword = (orderBuf.get(pos).toInt() != 0)
                pos += 1
                val activePlayers = orderBuf.getShort(pos).toInt() and 0xFFFF
                pos += 2
                val maxPlayers = orderBuf.getShort(pos).toInt() and 0xFFFF
                pos += 2

                val hostname = readUint32String(orderBuf, recvPacket.length, pos).also { pos = it.second }.first
                val gamemode = readUint32String(orderBuf, recvPacket.length, pos).also { pos = it.second }.first
                val language = readUint32String(orderBuf, recvPacket.length, pos).also { pos = it.second }.first

                // Query Rules ('r') and Players ('d') over the same socket
                val rules = queryRulesInternal(socket, address, targetPort)
                val players = queryDetailedPlayersInternal(socket, address, targetPort)

                SampQueryResponse(
                    isOnline = true,
                    resolvedIp = address.hostAddress ?: cleanHost,
                    port = port,
                    hasPassword = hasPassword,
                    activePlayers = activePlayers,
                    maxPlayers = maxPlayers,
                    rttPingMs = rttMs,
                    hostname = hostname,
                    gamemode = gamemode,
                    language = language,
                    rawRequestHex = rawHex,
                    rawResponseBytes = recvPacket.length,
                    rules = rules,
                    onlinePlayers = players
                )
            }
        } catch (e: Exception) {
            SampQueryResponse(
                isOnline = false,
                resolvedIp = cleanHost,
                port = port,
                errorDetail = "Query UDP gagal (${e.javaClass.simpleName}): ${e.message ?: "Timeout"}"
            )
        }
    }

    private fun queryRulesInternal(
        socket: DatagramSocket,
        address: InetAddress,
        port: Int
    ): List<SampRuleEntry> {
        return try {
            val pkt = buildQueryPacket(address, port, AppConstants.SAMP_OPCODE_RULES)
            socket.send(DatagramPacket(pkt, pkt.size, address, port))
            val buf = ByteArray(2048)
            val recv = DatagramPacket(buf, buf.size)
            socket.receive(recv)
            if (recv.length < 13) return emptyList()

            val bb = ByteBuffer.wrap(buf, 0, recv.length).order(ByteOrder.LITTLE_ENDIAN)
            var pos = 11
            val ruleCount = bb.getShort(pos).toInt() and 0xFFFF
            pos += 2
            val list = mutableListOf<SampRuleEntry>()
            for (i in 0 until minOf(ruleCount, 32)) {
                if (pos >= recv.length) break
                val k = readUint8String(bb, recv.length, pos).also { pos = it.second }.first
                val v = readUint8String(bb, recv.length, pos).also { pos = it.second }.first
                if (k.isNotEmpty()) {
                    list.add(SampRuleEntry(k, v))
                }
            }
            list
        } catch (_: Exception) {
            emptyList()
        }
    }

    private fun queryDetailedPlayersInternal(
        socket: DatagramSocket,
        address: InetAddress,
        port: Int
    ): List<SampPlayerEntry> {
        return try {
            val pkt = buildQueryPacket(address, port, AppConstants.SAMP_OPCODE_PLAYERS)
            socket.send(DatagramPacket(pkt, pkt.size, address, port))
            val buf = ByteArray(4096)
            val recv = DatagramPacket(buf, buf.size)
            socket.receive(recv)
            if (recv.length < 13) return emptyList()

            val bb = ByteBuffer.wrap(buf, 0, recv.length).order(ByteOrder.LITTLE_ENDIAN)
            var pos = 11
            val playerCount = bb.getShort(pos).toInt() and 0xFFFF
            pos += 2
            val list = mutableListOf<SampPlayerEntry>()
            for (i in 0 until minOf(playerCount, 64)) {
                if (pos + 2 >= recv.length) break
                val id = bb.get(pos).toInt() and 0xFF
                pos += 1
                val name = readUint8String(bb, recv.length, pos).also { pos = it.second }.first
                if (pos + 8 > recv.length) break
                val score = bb.getInt(pos)
                pos += 4
                val ping = bb.getInt(pos)
                pos += 4
                list.add(SampPlayerEntry(id, name, score, ping))
            }
            list
        } catch (_: Exception) {
            emptyList()
        }
    }

    private fun readUint32String(bb: ByteBuffer, limit: Int, startPos: Int): Pair<String, Int> {
        if (startPos + 4 > limit) return "" to limit
        val len = bb.getInt(startPos)
        val nextPos = startPos + 4
        if (len <= 0 || len > 512 || nextPos + len > limit) return "" to nextPos
        val dst = ByteArray(len)
        for (i in 0 until len) {
            dst[i] = bb.get(nextPos + i)
        }
        return String(dst, StandardCharsets.US_ASCII) to (nextPos + len)
    }

    private fun readUint8String(bb: ByteBuffer, limit: Int, startPos: Int): Pair<String, Int> {
        if (startPos + 1 > limit) return "" to limit
        val len = bb.get(startPos).toInt() and 0xFF
        val nextPos = startPos + 1
        if (len <= 0 || nextPos + len > limit) return "" to nextPos
        val dst = ByteArray(len)
        for (i in 0 until len) {
            dst[i] = bb.get(nextPos + i)
        }
        return String(dst, StandardCharsets.US_ASCII) to (nextPos + len)
    }

    private fun buildLoopbackSampResponse(
        header11: ByteArray,
        opcode: Byte,
        requestBuf: ByteArray,
        requestLen: Int
    ): ByteArray {
        val out = ByteArrayOutputStream()
        out.write(header11)
        when (opcode) {
            AppConstants.SAMP_OPCODE_INFO -> {
                val body = ByteBuffer.allocate(512).order(ByteOrder.LITTLE_ENDIAN)
                body.put(0.toByte()) // password = 0
                body.putShort(4.toShort()) // active players
                body.putShort(200.toShort()) // max players
                putUint32Str(body, "Ventura Local SA-MP 0.3.7-R5 Host (ARM64 Loopback)")
                putUint32Str(body, "San Andreas Roleplay / Freeroam")
                putUint32Str(body, "Bahasa Indonesia / English")
                out.write(body.array(), 0, body.position())
            }
            AppConstants.SAMP_OPCODE_RULES -> {
                val rules = listOf(
                    "lagcomp" to "On",
                    "mapname" to "San Andreas",
                    "version" to "0.3.7-R5",
                    "weather" to "10",
                    "worldtime" to "12:00",
                    "weburl" to "ventura.local"
                )
                val body = ByteBuffer.allocate(512).order(ByteOrder.LITTLE_ENDIAN)
                body.putShort(rules.size.toShort())
                for ((k, v) in rules) {
                    putUint8Str(body, k)
                    putUint8Str(body, v)
                }
                out.write(body.array(), 0, body.position())
            }
            AppConstants.SAMP_OPCODE_PLAYERS -> {
                val samplePlayers = listOf(
                    SampPlayerEntry(0, "Carl_Johnson", 142, 4),
                    SampPlayerEntry(1, "Sweet_Grove", 98, 6),
                    SampPlayerEntry(2, "Cesar_Vialpando", 215, 5),
                    SampPlayerEntry(3, "Woozie_Venturas", 310, 3)
                )
                val body = ByteBuffer.allocate(512).order(ByteOrder.LITTLE_ENDIAN)
                body.putShort(samplePlayers.size.toShort())
                for (p in samplePlayers) {
                    body.put(p.playerId.toByte())
                    putUint8Str(body, p.playerName)
                    body.putInt(p.score)
                    body.putInt(p.pingMs)
                }
                out.write(body.array(), 0, body.position())
            }
            AppConstants.SAMP_OPCODE_PING -> {
                if (requestLen >= 15) {
                    out.write(requestBuf, 11, 4)
                }
            }
        }
        return out.toByteArray()
    }

    private fun putUint32Str(buf: ByteBuffer, text: String) {
        val bytes = text.toByteArray(StandardCharsets.US_ASCII)
        buf.putInt(bytes.size)
        buf.put(bytes)
    }

    private fun putUint8Str(buf: ByteBuffer, text: String) {
        val bytes = text.toByteArray(StandardCharsets.US_ASCII)
        buf.put(bytes.size.toByte())
        buf.put(bytes)
    }
}
