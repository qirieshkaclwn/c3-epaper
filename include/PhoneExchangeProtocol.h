#pragma once

#include <stddef.h>
#include <stdint.h>
#include <vector>

struct PhoneExchangeData {
    uint32_t unixTime = 0;
    bool vpnConnected = false;
    bool playbackPlaying = false;

    std::vector<uint8_t> artist;
    std::vector<uint8_t> track;

    bool hasUnixTime = false;
    bool hasVpn = false;
    bool hasPlayback = false;
    bool hasArtist = false;
    bool hasTrack = false;
};

struct PhoneExchangePacketHeader {
    uint8_t flags = 0;
    uint16_t msgId = 0;
    uint16_t fragOffset = 0;
    uint16_t payloadLen = 0;

    bool moreFragments() const { return (flags & 0x01) != 0; }
    bool isRequest() const { return (flags & 0x02) != 0; }
};

struct PhoneExchangeFragmentAccumulator {
    bool active = false;
    uint16_t msgId = 0;
    bool isRequest = false;
    uint32_t lastUpdateMs = 0;
    bool hasLastFragment = false;
    uint16_t expectedTotalLen = 0;
    std::vector<uint8_t> buffer;
    std::vector<uint8_t> receivedMask;
};

class PhoneExchangeProtocol {
public:
    static constexpr uint8_t kFlagMoreFragments = 0x01;
    static constexpr uint8_t kFlagRequest = 0x02;
    static constexpr uint8_t kProtocolVersion = 1;
    static constexpr uint16_t kHeaderSize = 7;
    static constexpr uint32_t kReassemblyTimeoutMs = 5000;
    static constexpr uint16_t kMaxStringLen = 0xFFFF;

    // Поля (для конструктора сообщений)
    static constexpr uint8_t kFieldUnixTime = 0x01;
    static constexpr uint8_t kFieldVpn = 0x02;
    static constexpr uint8_t kFieldPlayback = 0x04;
    static constexpr uint8_t kFieldArtist = 0x08;
    static constexpr uint8_t kFieldTrack = 0x10;
    static constexpr uint8_t kAllFieldMask = kFieldUnixTime | kFieldVpn | kFieldPlayback | kFieldArtist | kFieldTrack;

    // Теги payload 
    static constexpr uint8_t kTagUnixTime = 0x01;
    static constexpr uint8_t kTagVpn = 0x02;
    static constexpr uint8_t kTagPlayback = 0x03;
    static constexpr uint8_t kTagArtist = 0x04;
    static constexpr uint8_t kTagTrack = 0x05;
    static constexpr uint8_t kTagRequestTime = 0x10;
    static constexpr uint8_t kTagRequestVpn = 0x11;
    static constexpr uint8_t kTagRequestPlayback = 0x12;
    static constexpr uint8_t kTagRequestArtist = 0x13;
    static constexpr uint8_t kTagRequestTrack = 0x14;

    // Универсальный конструктор DATA payload с произвольным набором полей.
    static bool encodeDataPayload(const PhoneExchangeData& data, uint8_t fieldMask, std::vector<uint8_t>& outPayload);

    // Универсальный конструктор REQUEST payload с произвольным набором запросов.
    static bool encodeRequestPayload(uint8_t requestFieldMask, std::vector<uint8_t>& outPayload);

    // Разбить payload на BLE-пакеты с header (flags,msg_id,frag_offset,payload_len).
    static bool fragmentPayload(const std::vector<uint8_t>& payload, uint16_t msgId, bool request, uint16_t mtu, std::vector<std::vector<uint8_t>>& outPackets);

    // Распарсить один BLE-пакет.
    static bool parsePacket(const uint8_t* packet, size_t packetLen, PhoneExchangePacketHeader& outHeader, std::vector<uint8_t>& outPayload);

    // Сборка фрагментов. completed=true, когда собралось полное логическое сообщение.
    static bool consumeFragment(
        PhoneExchangeFragmentAccumulator& accumulator,
        const uint8_t* packet,
        size_t packetLen,
        uint32_t nowMs,
        bool& completed,
        bool& outRequest,
        uint16_t& outMsgId,
        std::vector<uint8_t>& outFullPayload);

    static void resetAccumulator(PhoneExchangeFragmentAccumulator& accumulator);

    // Десериализация payload.
    static bool decodeDataPayload(const uint8_t* payload, size_t payloadLen, PhoneExchangeData& outData);
    static bool decodeRequestPayload(const uint8_t* payload, size_t payloadLen, uint8_t& outRequestFieldMask);

private:
    static bool isValidFieldMask(uint8_t fieldMask);
    static void writeU16BE(uint8_t* dst, uint16_t value);
    static void writeU32BE(uint8_t* dst, uint32_t value);
    static uint16_t readU16BE(const uint8_t* src);
    static uint32_t readU32BE(const uint8_t* src);
};
