#include "PhoneExchangeProtocol.h"

#include <cstring>

constexpr uint8_t PhoneExchangeProtocol::kTagUnixTime;
constexpr uint8_t PhoneExchangeProtocol::kTagVpn;
constexpr uint8_t PhoneExchangeProtocol::kTagPlayback;
constexpr uint8_t PhoneExchangeProtocol::kTagArtist;
constexpr uint8_t PhoneExchangeProtocol::kTagTrack;
constexpr uint8_t PhoneExchangeProtocol::kTagRequestTime;
constexpr uint8_t PhoneExchangeProtocol::kTagRequestVpn;
constexpr uint8_t PhoneExchangeProtocol::kTagRequestPlayback;
constexpr uint8_t PhoneExchangeProtocol::kTagRequestArtist;
constexpr uint8_t PhoneExchangeProtocol::kTagRequestTrack;

bool PhoneExchangeProtocol::isValidFieldMask(uint8_t fieldMask) {
    // Разрешены только биты, описанные протоколом.
    return (fieldMask & ~kAllFieldMask) == 0;
}

void PhoneExchangeProtocol::writeU16BE(uint8_t* dst, uint16_t value) {
    // Сериализация 16-битного числа в сетевом (big-endian) порядке.
    dst[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[1] = static_cast<uint8_t>(value & 0xFF);
}

void PhoneExchangeProtocol::writeU32BE(uint8_t* dst, uint32_t value) {
    // Сериализация 32-битного числа в сетевом (big-endian) порядке.
    dst[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[3] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t PhoneExchangeProtocol::readU16BE(const uint8_t* src) {
    // Десериализация 16-битного числа из big-endian представления.
    return static_cast<uint16_t>(static_cast<uint16_t>(src[0]) << 8 | static_cast<uint16_t>(src[1]));
}

uint32_t PhoneExchangeProtocol::readU32BE(const uint8_t* src) {
    // Десериализация 32-битного числа из big-endian представления.
    return (static_cast<uint32_t>(src[0]) << 24) |
           (static_cast<uint32_t>(src[1]) << 16) |
           (static_cast<uint32_t>(src[2]) << 8) |
           static_cast<uint32_t>(src[3]);
}

bool PhoneExchangeProtocol::encodeDataPayload(const PhoneExchangeData& data, uint8_t fieldMask, std::vector<uint8_t>& outPayload) {
    // Формируем TLV-подобный payload DATA в порядке: time, vpn, playback, artist, track.
    outPayload.clear();
    if (!isValidFieldMask(fieldMask)) {
        return false;
    }

    if ((fieldMask & kFieldUnixTime) != 0) {
        // Time: tag + 4 байта unix-time.
        if (!data.hasUnixTime) return false;
        outPayload.push_back(kTagUnixTime);
        const size_t pos = outPayload.size();
        outPayload.resize(pos + 4);
        writeU32BE(&outPayload[pos], data.unixTime);
    }

    if ((fieldMask & kFieldVpn) != 0) {
        // VPN: tag + 1 байт (0/1).
        if (!data.hasVpn) return false;
        outPayload.push_back(kTagVpn);
        outPayload.push_back(data.vpnConnected ? 1 : 0);
    }

    if ((fieldMask & kFieldPlayback) != 0) {
        // Playback: tag + 1 байт (0/1).
        if (!data.hasPlayback) return false;
        outPayload.push_back(kTagPlayback);
        outPayload.push_back(data.playbackPlaying ? 1 : 0);
    }

    if ((fieldMask & kFieldArtist) != 0) {
        // Artist: tag + u16 длина + байты UTF-8.
        const size_t artistLen = data.artist.size();
        if (!data.hasArtist || artistLen > kMaxStringLen) return false;
        outPayload.push_back(kTagArtist);
        outPayload.push_back(static_cast<uint8_t>((artistLen >> 8) & 0xFF));
        outPayload.push_back(static_cast<uint8_t>(artistLen & 0xFF));
        if (artistLen > 0) {
            const size_t pos = outPayload.size();
            outPayload.resize(pos + artistLen);
            std::memcpy(&outPayload[pos], data.artist.data(), artistLen);
        }
    }

    if ((fieldMask & kFieldTrack) != 0) {
        // Track: tag + u16 длина + байты UTF-8.
        const size_t trackLen = data.track.size();
        if (!data.hasTrack || trackLen > kMaxStringLen) return false;
        outPayload.push_back(kTagTrack);
        outPayload.push_back(static_cast<uint8_t>((trackLen >> 8) & 0xFF));
        outPayload.push_back(static_cast<uint8_t>(trackLen & 0xFF));
        if (trackLen > 0) {
            const size_t pos = outPayload.size();
            outPayload.resize(pos + trackLen);
            std::memcpy(&outPayload[pos], data.track.data(), trackLen);
        }
    }

    return true;
}

bool PhoneExchangeProtocol::encodeRequestPayload(uint8_t requestFieldMask, std::vector<uint8_t>& outPayload) {
    // REQUEST payload — это просто набор тэгов запрашиваемых полей.
    outPayload.clear();
    if (!isValidFieldMask(requestFieldMask)) {
        return false;
    }

    if ((requestFieldMask & kFieldUnixTime) != 0) outPayload.push_back(kTagRequestTime);
    if ((requestFieldMask & kFieldVpn) != 0) outPayload.push_back(kTagRequestVpn);
    if ((requestFieldMask & kFieldPlayback) != 0) outPayload.push_back(kTagRequestPlayback);
    if ((requestFieldMask & kFieldArtist) != 0) outPayload.push_back(kTagRequestArtist);
    if ((requestFieldMask & kFieldTrack) != 0) outPayload.push_back(kTagRequestTrack);
    return true;
}

bool PhoneExchangeProtocol::fragmentPayload(const std::vector<uint8_t>& payload, uint16_t msgId, bool request, uint16_t mtu, std::vector<std::vector<uint8_t>>& outPackets) {
    // Разбиваем payload на BLE-пакеты фиксированного формата: header + chunk.
    outPackets.clear();
    if (mtu <= kHeaderSize) {
        return false;
    }
    if (payload.size() > 0xFFFFu) {
        return false;
    }

    const uint16_t maxChunk = static_cast<uint16_t>(mtu - kHeaderSize);
    if (payload.empty()) {
        // Пустой payload всё равно кодируется отдельным пакетом с нулевой длиной.
        std::vector<uint8_t> packet(kHeaderSize);
        packet[0] = request ? kFlagRequest : 0;
        writeU16BE(&packet[1], msgId);
        writeU16BE(&packet[3], 0);
        writeU16BE(&packet[5], 0);
        outPackets.push_back(packet);
        return true;
    }

    size_t offset = 0;
    while (offset < payload.size()) {
        const size_t remaining = payload.size() - offset;
        const uint16_t chunk = static_cast<uint16_t>(remaining > maxChunk ? maxChunk : remaining);
        const bool more = (offset + chunk) < payload.size();

        if ((offset + chunk) > 0xFFFFu) {
            return false;
        }

        std::vector<uint8_t> packet(kHeaderSize + chunk);
        uint8_t flags = request ? kFlagRequest : 0;
        if (more) flags |= kFlagMoreFragments;

        // Header: flags, msgId, offset, payloadLen.
        packet[0] = flags;
        writeU16BE(&packet[1], msgId);
        writeU16BE(&packet[3], static_cast<uint16_t>(offset));
        writeU16BE(&packet[5], chunk);
        std::memcpy(&packet[kHeaderSize], &payload[offset], chunk);
        outPackets.push_back(packet);

        offset += chunk;
    }

    return true;
}

bool PhoneExchangeProtocol::parsePacket(const uint8_t* packet, size_t packetLen, PhoneExchangePacketHeader& outHeader, std::vector<uint8_t>& outPayload) {
    // Парсим один фрагмент и валидируем его внутреннюю длину.
    outPayload.clear();
    if (packet == nullptr || packetLen < kHeaderSize) {
        return false;
    }

    outHeader.flags = packet[0];
    outHeader.msgId = readU16BE(&packet[1]);
    outHeader.fragOffset = readU16BE(&packet[3]);
    outHeader.payloadLen = readU16BE(&packet[5]);

    if (packetLen != static_cast<size_t>(kHeaderSize + outHeader.payloadLen)) {
        return false;
    }

    if ((static_cast<uint32_t>(outHeader.fragOffset) + static_cast<uint32_t>(outHeader.payloadLen)) > 0xFFFFu) {
        return false;
    }

    if (outHeader.payloadLen > 0) {
        // Копируем полезные данные фрагмента в отдельный буфер.
        outPayload.resize(outHeader.payloadLen);
        std::memcpy(outPayload.data(), &packet[kHeaderSize], outHeader.payloadLen);
    }
    return true;
}

void PhoneExchangeProtocol::resetAccumulator(PhoneExchangeFragmentAccumulator& accumulator) {
    // Полный сброс состояния reassembly.
    accumulator.active = false;
    accumulator.msgId = 0;
    accumulator.isRequest = false;
    accumulator.lastUpdateMs = 0;
    accumulator.hasLastFragment = false;
    accumulator.expectedTotalLen = 0;
    accumulator.buffer.clear();
    accumulator.receivedMask.clear();
}

bool PhoneExchangeProtocol::consumeFragment(
    PhoneExchangeFragmentAccumulator& accumulator,
    const uint8_t* packet,
    size_t packetLen,
    uint32_t nowMs,
    bool& completed,
    bool& outRequest,
    uint16_t& outMsgId,
    std::vector<uint8_t>& outFullPayload) {
    // Инкрементальная сборка сообщения из приходящих фрагментов.
    completed = false;
    outRequest = false;
    outMsgId = 0;
    outFullPayload.clear();

    PhoneExchangePacketHeader header;
    std::vector<uint8_t> fragmentPayload;
    if (!parsePacket(packet, packetLen, header, fragmentPayload)) {
        return false;
    }

    // Протухшее состояние сборки сбрасываем по таймауту.
    if (accumulator.active && (nowMs - accumulator.lastUpdateMs) > kReassemblyTimeoutMs) {
        resetAccumulator(accumulator);
    }

    const bool fragmentIsRequest = header.isRequest();

    if (!accumulator.active ||
        accumulator.msgId != header.msgId ||
        accumulator.isRequest != fragmentIsRequest) {
        // Новый поток фрагментов (или смена msgId/type) начинает сборку заново.
        resetAccumulator(accumulator);
        accumulator.active = true;
        accumulator.msgId = header.msgId;
        accumulator.isRequest = fragmentIsRequest;
    }

    accumulator.lastUpdateMs = nowMs;

    const size_t fragmentEnd = static_cast<size_t>(header.fragOffset) + static_cast<size_t>(header.payloadLen);
    if (fragmentEnd > 0xFFFFu) {
        resetAccumulator(accumulator);
        return false;
    }

    if (accumulator.buffer.size() < fragmentEnd) {
        accumulator.buffer.resize(fragmentEnd, 0);
        accumulator.receivedMask.resize(fragmentEnd, 0);
    }

    if (header.payloadLen > 0) {
        // Копируем байты фрагмента и отмечаем полученный диапазон.
        std::memcpy(&accumulator.buffer[header.fragOffset], fragmentPayload.data(), header.payloadLen);
        std::memset(&accumulator.receivedMask[header.fragOffset], 1, header.payloadLen);
    }

    if (!header.moreFragments()) {
        // Последний фрагмент задаёт ожидаемую итоговую длину сообщения.
        accumulator.hasLastFragment = true;
        accumulator.expectedTotalLen = static_cast<uint16_t>(fragmentEnd);
    }

    if (!accumulator.hasLastFragment) {
        return true;
    }

    if (accumulator.expectedTotalLen > accumulator.receivedMask.size()) {
        return true;
    }

    // Проверяем, что каждый байт до expectedTotalLen уже получен.
    for (uint16_t i = 0; i < accumulator.expectedTotalLen; ++i) {
        if (accumulator.receivedMask[i] == 0) {
            return true;
        }
    }

    // Готово: возвращаем полное сообщение и сбрасываем аккумулятор.
    outRequest = accumulator.isRequest;
    outMsgId = accumulator.msgId;
    outFullPayload.assign(accumulator.buffer.begin(), accumulator.buffer.begin() + accumulator.expectedTotalLen);
    completed = true;
    resetAccumulator(accumulator);
    return true;
}

bool PhoneExchangeProtocol::decodeDataPayload(const uint8_t* payload, size_t payloadLen, PhoneExchangeData& outData) {
    // Обратный разбор DATA payload (TLV-последовательность).
    if (payload == nullptr && payloadLen != 0) {
        return false;
    }

    outData = {};
    size_t pos = 0;

    while (pos < payloadLen) {
        const uint8_t tag = payload[pos++];

        if (tag == kTagUnixTime) {
            // Time: 4 байта unix-time.
            if (pos + 4 > payloadLen) return false;
            outData.unixTime = readU32BE(&payload[pos]);
            outData.hasUnixTime = true;
            pos += 4;
            continue;
        }

        if (tag == kTagVpn) {
            // VPN: bool в виде 0/1.
            if (pos + 1 > payloadLen) return false;
            const uint8_t value = payload[pos++];
            if (value > 1) return false;
            outData.vpnConnected = (value != 0);
            outData.hasVpn = true;
            continue;
        }

        if (tag == kTagPlayback) {
            // Playback: bool в виде 0/1.
            if (pos + 1 > payloadLen) return false;
            const uint8_t value = payload[pos++];
            if (value > 1) return false;
            outData.playbackPlaying = (value != 0);
            outData.hasPlayback = true;
            continue;
        }

        if (tag == kTagArtist || tag == kTagTrack) {
            // Строка: u16 длина + len байт данных.
            if (pos + 2 > payloadLen) return false;
            const uint16_t len = readU16BE(&payload[pos]);
            pos += 2;
            if (len > kMaxStringLen || pos + len > payloadLen) return false;

            if (tag == kTagArtist) {
                outData.artist.assign(&payload[pos], &payload[pos + len]);
                outData.hasArtist = true;
            } else {
                outData.track.assign(&payload[pos], &payload[pos + len]);
                outData.hasTrack = true;
            }
            pos += len;
            continue;
        }

        return false;
    }

    return true;
}

bool PhoneExchangeProtocol::decodeRequestPayload(const uint8_t* payload, size_t payloadLen, uint8_t& outRequestFieldMask) {
    // REQUEST — это список тэгов; собираем их в итоговую битовую маску.
    if (payload == nullptr && payloadLen != 0) {
        return false;
    }

    uint8_t mask = 0;
    for (size_t i = 0; i < payloadLen; ++i) {
        const uint8_t tag = payload[i];
        if (tag == kTagRequestTime) {
            mask |= kFieldUnixTime;
        } else if (tag == kTagRequestVpn) {
            mask |= kFieldVpn;
        } else if (tag == kTagRequestPlayback) {
            mask |= kFieldPlayback;
        } else if (tag == kTagRequestArtist) {
            mask |= kFieldArtist;
        } else if (tag == kTagRequestTrack) {
            mask |= kFieldTrack;
        } else {
            return false;
        }
    }

    outRequestFieldMask = mask;
    return true;
}
