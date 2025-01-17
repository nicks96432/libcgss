#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "acb_cdata.h"
#include "acb_enum.h"
#include "acb_env_ns.h"
#include "kawashima/hca/CHcaDecoder.h"
#include "kawashima/hca/hca_utils.h"
#include "kawashima/wave/wave_native.h"
#include "takamori/exceptions/CArgumentException.h"
#include "takamori/streams/CMemoryStream.h"

#include "./internal/CHcaAth.h"
#include "./internal/CHcaChannel.h"
#include "./internal/CHcaCipher.h"
#include "./internal/CHcaData.h"

ACB_NS_BEGIN

CHcaDecoder::CHcaDecoder(IStream *stream): MyClass(stream, HCA_DECODER_CONFIG()) {}

CHcaDecoder::CHcaDecoder(IStream *stream, const HCA_DECODER_CONFIG &decoderConfig): MyBase(stream) {
    _cipher = nullptr;
    _ath    = nullptr;
    for (auto &_channel : _channels) {
        _channel = nullptr;
    }
    _waveHeaderBuffer = _hcaBlockBuffer = nullptr;
    _waveHeaderSize = _waveBlockSize = 0;
    _position                        = 0;
    _decoderConfig                   = decoderConfig;
    InitializeExtra();
}

CHcaDecoder::~CHcaDecoder() {
    for (const auto &v : _decodedBlocks) {
        delete[] v.second;
    }
    _decodedBlocks.clear();

    if (_waveHeaderBuffer) {
        delete[] _waveHeaderBuffer;
        _waveHeaderBuffer = nullptr;
    }

    if (_hcaBlockBuffer) {
        delete[] _hcaBlockBuffer;
        _hcaBlockBuffer = nullptr;
    }

    if (_ath) {
        delete _ath;
        _ath = nullptr;
    }

    if (_cipher) {
        delete _cipher;
        _cipher = nullptr;
    }

    for (auto &_channel : _channels) {
        if (_channel) {
            delete _channel;
            _channel = nullptr;
        }
    }
}

void CHcaDecoder::InitializeExtra() {
    auto &hcaInfo = _hcaInfo;

    // Initialize adjustment and cipher tables.
    _ath = new CHcaAth();
    if (!_ath->Init(hcaInfo.athType, hcaInfo.samplingRate)) {
        throw CException();
    }
    auto &cipherConfig      = _decoderConfig.cipherConfig;
    cipherConfig.cipherType = hcaInfo.cipherType;
    _cipher                 = new CHcaCipher(cipherConfig);

    // Prepare the channel decoders.
    _channels.fill(nullptr);
    std::array<std::uint8_t, 0x10> r = {};
    std::uint32_t b                  = hcaInfo.channelCount / hcaInfo.compR03;
    if (hcaInfo.compR07 && b > 1) {
        auto c = r.begin();
        for (auto i = 0; i < hcaInfo.compR03; ++i, c += b) {
            switch (b) {
            case 2:
            case 3:
                c[0] = 1;
                c[1] = 2;
                break;
            case 4:
                c[0] = 1;
                c[1] = 2;
                if (hcaInfo.compR04 == 0) {
                    c[2] = 1;
                    c[3] = 2;
                }
                break;
            case 5:
                c[0] = 1;
                c[1] = 2;
                if (hcaInfo.compR04 <= 2) {
                    c[3] = 1;
                    c[4] = 2;
                }
                break;
            case 6:
            case 7:
                c[0] = 1;
                c[1] = 2;
                c[4] = 1;
                c[5] = 2;
                // Fall through
            case 8:
                c[6] = 1;
                c[7] = 2;
                break;
            default:
                throw CArgumentException();
            }
        }
    }
    auto channel = _channels.begin();
    for (std::uint32_t i = 0; i < hcaInfo.channelCount; ++i, ++channel) {
        *channel           = new CHcaChannel();
        (*channel)->type   = r[i];
        (*channel)->value3 = &(*channel)->value[hcaInfo.compR06 + hcaInfo.compR07];
        (*channel)->count  = hcaInfo.compR06 + ((r[i] != 2) ? hcaInfo.compR07 : 0);
    }
}

auto CHcaDecoder::GetWaveHeaderSize() -> std::uint32_t {
    if (_waveHeaderSize) {
        return _waveHeaderSize;
    }

    auto &hcaInfo            = _hcaInfo;
    std::uint32_t sizeNeeded = sizeof(WaveRiffSection);
    if (hcaInfo.loopExists && !WaveSettings::SoftLoop) {
        sizeNeeded += sizeof(WaveSampleSection);
    }
    if (hcaInfo.commentLength > 0) {
        std::uint32_t noteSize = 4 + hcaInfo.commentLength + 1;
        // Pad by 4
        if (noteSize & 3u) {
            noteSize += 4 - (noteSize & 3u);
        }
        sizeNeeded += 8 + noteSize;
    }
    sizeNeeded += sizeof(WaveDataSection);
    _waveHeaderSize = sizeNeeded;
    return sizeNeeded;
}

auto CHcaDecoder::GenerateWaveHeader() -> const std::uint8_t * {
    if (_waveHeaderBuffer) {
        return _waveHeaderBuffer;
    }
    const auto &hcaInfo   = _hcaInfo;
    const auto headerSize = GetWaveHeaderSize();
    auto headerBuffer     = (_waveHeaderBuffer = new std::uint8_t[headerSize]);
    std::memset(headerBuffer, 0, headerSize);

    WaveRiffSection wavRiff = {
        {'R', 'I', 'F', 'F'},
        0, {'W', 'A', 'V', 'E'},
        {'f', 'm', 't', ' '},
        0x10, 0, 0, 0, 0, 0, 0
    };
    WaveSampleSection wavSmpl = {
        {'s', 'm', 'p', 'l'},
        0x3C, 0, 0, 0, 0x3C, 0, 0, 0, 1, 0x18, 0, 0, 0, 0, 0, 0
    };
    WaveNoteSection wavNote = {
        {'n', 'o', 't', 'e'},
        0, 0
    };
    WaveDataSection wavData = {
        {'d', 'a', 't', 'a'},
        0
    };

    wavRiff.fmtType         = static_cast<std::uint16_t>((WaveSettings::BitPerChannel > 0) ? 1 : 3);
    wavRiff.fmtChannelCount = static_cast<std::uint16_t>(hcaInfo.channelCount);
    wavRiff.fmtBitCount     = static_cast<std::uint16_t>(
        (WaveSettings::BitPerChannel > 0) ? WaveSettings::BitPerChannel : 32
    );
    wavRiff.fmtSamplingRate = hcaInfo.samplingRate;
    wavRiff.fmtSamplingSize =
        static_cast<std::uint16_t>(wavRiff.fmtBitCount / 8 * wavRiff.fmtChannelCount);
    wavRiff.fmtSamplesPerSec = wavRiff.fmtSamplingRate * wavRiff.fmtSamplingSize;
    if (hcaInfo.loopExists) {
        wavSmpl.samplePeriod =
            static_cast<std::uint32_t>(1 / (double)wavRiff.fmtSamplingRate * 1000000000);
        wavSmpl.loopStart = hcaInfo.loopStart * 0x80 * 8 + hcaInfo.fmtR02; // fmtR02 is muteFooter
        wavSmpl.loopEnd   = hcaInfo.loopEnd * 0x80 * 8;
        wavSmpl.loopPlayCount = (hcaInfo.loopR01 == 0x80) ? 0 : hcaInfo.loopR01;
    } else if (WaveSettings::SoftLoop) {
        wavSmpl.loopStart = 0;
        wavSmpl.loopEnd   = hcaInfo.blockCount * 0x80 * 8;
    }
    if (hcaInfo.commentLength > 0) {
        wavNote.noteSize = 4 + hcaInfo.commentLength + 1;
        if (wavNote.noteSize & 3u) {
            wavNote.noteSize += 4 - (wavNote.noteSize & 3u);
        }
    }
    wavData.dataSize = wavRiff.fmtSamplingSize *
                       (hcaInfo.blockCount * 0x80 * 8 +
                        (wavSmpl.loopEnd - wavSmpl.loopStart) * _decoderConfig.loopCount);
    wavRiff.riffSize = static_cast<std::uint32_t>(
        0x1C + ((hcaInfo.loopExists && !WaveSettings::SoftLoop) ? sizeof(wavSmpl) : 0) +
        (hcaInfo.commentLength > 0 ? 8 + wavNote.noteSize : 0) + sizeof(wavData) + wavData.dataSize
    );

    CMemoryStream memoryStream(headerBuffer, headerSize);

#define WRITE_STRUCT(var) memoryStream.Write(&(var), sizeof(var), 0, sizeof(var))

    WRITE_STRUCT(wavRiff);
    if (hcaInfo.loopExists && !WaveSettings::SoftLoop) {
        WRITE_STRUCT(wavSmpl);
    }
    if (hcaInfo.commentLength > 0) {
        WRITE_STRUCT(wavNote);
        memoryStream.Write(
            hcaInfo.comment, hcaInfo.commentLength + 1, 0, hcaInfo.commentLength + 1
        );
    }
    WRITE_STRUCT(wavData);

#undef WRITE_STRUCT

    return headerBuffer;
}

auto CHcaDecoder::GetWaveBlockSize() -> std::uint32_t {
    if (_waveBlockSize) {
        return _waveBlockSize;
    }
    std::uint32_t audioBitPerChannel =
        WaveSettings::BitPerChannel != 0 ? WaveSettings::BitPerChannel : sizeof(float);
    std::uint32_t waveBlockSize =
        0x80 * (audioBitPerChannel / sizeof(std::uint8_t)) * _hcaInfo.channelCount;
    _waveBlockSize = waveBlockSize;
    return waveBlockSize;
}

auto CHcaDecoder::DecodeBlock(std::uint32_t blockIndex) -> const std::uint8_t * {
    auto &decodedBlocks = _decodedBlocks;
    {
        const auto decodedItem = decodedBlocks.find(blockIndex);
        if (decodedItem != decodedBlocks.cend()) {
            return decodedItem->second;
        }
    }

    auto stream              = _baseStream;
    const auto &hcaInfo      = _hcaInfo;
    const auto waveBlockSize = GetWaveBlockSize();
    auto channels            = _channels.cbegin();

    auto hcaBlockBuffer = _hcaBlockBuffer ? _hcaBlockBuffer : new std::uint8_t[hcaInfo.blockSize];
    _hcaBlockBuffer     = hcaBlockBuffer;

    stream->Seek(hcaInfo.dataOffset + hcaInfo.blockSize * blockIndex, StreamSeekOrigin::Begin);
    auto actualRead = stream->Read(hcaBlockBuffer, hcaInfo.blockSize, 0, hcaInfo.blockSize);
    if (actualRead < hcaInfo.blockSize) {
        throw CException(OpResult::DecodeFailed);
    }

    // Compute block checksum.
    if (ComputeChecksum(hcaBlockBuffer, hcaInfo.blockSize, 0) != 0) {
        throw CException(OpResult::ChecksumError);
    }

    // Decrypt block if needed.
    _cipher->Decrypt(hcaBlockBuffer, hcaInfo.blockSize);

    CHcaData data(hcaBlockBuffer, hcaInfo.blockSize, hcaInfo.blockSize);

    const auto magic = data.GetBit(16);
    if (magic != 0xffff) {
        throw CException(OpResult::DecodeFailed);
    }

    // Actual decoding process.
    auto a = (data.GetBit(9) << 8u) - data.GetBit(7);
    for (std::uint32_t i = 0; i < hcaInfo.channelCount; ++i) {
        CHcaChannel::Decode1(*(channels + i), &data, hcaInfo.compR09, a, _ath->GetTable());
    }
    for (auto i = 0; i < 8; ++i) {
        for (std::uint32_t j = 0; j < hcaInfo.channelCount; ++j) {
            CHcaChannel::Decode2(*(channels + j), &data);
        }
        for (std::uint32_t j = 0; j < hcaInfo.channelCount; ++j) {
            CHcaChannel::Decode3(
                *(channels + j),
                hcaInfo.compR09,
                hcaInfo.compR08,
                hcaInfo.compR07 + hcaInfo.compR06,
                hcaInfo.compR05
            );
        }
        for (std::uint32_t j = 0; j < hcaInfo.channelCount - 1; ++j) {
            CHcaChannel::Decode4(
                *(channels + j),
                *(channels + j + 1),
                i,
                hcaInfo.compR05 - hcaInfo.compR06,
                hcaInfo.compR06,
                hcaInfo.compR07
            );
        }
        for (std::uint32_t j = 0; j < hcaInfo.channelCount; ++j) {
            CHcaChannel::Decode5(*(channels + j), i);
        }
    }

    // Generate wave data.
    const auto waveBlockBuffer = new std::uint8_t[waveBlockSize];
    const auto decodeFunc      = _decoderConfig.decodeFunc;
    std::uint32_t cursor       = 0;
    if (decodeFunc) {
        for (auto i = 0; i < 8; ++i) {
            for (auto j = 0; j < 0x80; ++j) {
                for (std::uint32_t k = 0; k < hcaInfo.channelCount; ++k) {
                    auto f = channels[k]->wave[i][j] * hcaInfo.rvaVolume;
                    f      = std::clamp(f, -1.0f, 1.0f);
                    cursor = decodeFunc(f, waveBlockBuffer, cursor);
                }
            }
        }
    }

    decodedBlocks[blockIndex] = waveBlockBuffer;
    return waveBlockBuffer;
}

auto CHcaDecoder::GetPosition() -> std::uint64_t {
    return _position;
}

void CHcaDecoder::SetPosition(std::uint64_t value) {
    _position = value;
}

auto CHcaDecoder::MapLoopedPosition(std::uint64_t linearPosition) -> std::uint64_t {
    const auto &decoderConfig = _decoderConfig;
    const auto waveHeaderSize = decoderConfig.waveHeaderEnabled ? GetWaveHeaderSize() : 0;
    const auto &hcaInfo       = _hcaInfo;
    if (!hcaInfo.loopExists || !decoderConfig.loopEnabled) {
        return linearPosition;
    }

    // Now, linearPosition points to the audio data.
    const auto waveBlockSize   = GetWaveBlockSize();
    const auto beforeLoopStart = hcaInfo.loopStart > 1 ? hcaInfo.loopStart - 1 : 0;
    const auto inLoop          = hcaInfo.loopEnd - hcaInfo.loopStart + 1;
    if (linearPosition <= waveHeaderSize + (beforeLoopStart + inLoop) * waveBlockSize) {
        return linearPosition;
    }

    if (decoderConfig.loopCount == 0) {
        throw CArgumentException("CHcaDecoder::MapLoopedPosition");
    }
    auto loopCount = (linearPosition - waveHeaderSize -
                      static_cast<std::uint64_t>(beforeLoopStart) * waveBlockSize) /
                     (static_cast<std::uint64_t>(inLoop) * waveBlockSize);
    loopCount = std::min(static_cast<std::uint32_t>(loopCount), decoderConfig.loopCount);
    return linearPosition - loopCount * inLoop * waveBlockSize - waveHeaderSize;
}

auto CHcaDecoder::GetLength() -> std::uint64_t {
    const auto &hcaInfo       = _hcaInfo;
    const auto &decoderConfig = _decoderConfig;
    if (hcaInfo.loopExists && decoderConfig.loopEnabled) {
        if (decoderConfig.loopCount == 0) {
            throw CArgumentException("CHcaDecoder::GetLength");
        }
        std::uint64_t total = 0;
        if (decoderConfig.waveHeaderEnabled) {
            total += GetWaveHeaderSize();
        }
        const auto beforeLoopStart = hcaInfo.loopStart > 1 ? hcaInfo.loopStart - 1 : 0;
        const auto afterLoopEnd =
            hcaInfo.loopEnd < hcaInfo.blockCount - 1 ? hcaInfo.blockCount - 1 - hcaInfo.loopEnd : 0;
        const auto inLoop = hcaInfo.loopEnd - hcaInfo.loopStart + 1;
        total += static_cast<std::uint64_t>(beforeLoopStart + afterLoopEnd) * GetWaveBlockSize();
        total += static_cast<std::uint64_t>(inLoop) * decoderConfig.loopCount * GetWaveBlockSize();
        return total;
    } else {
        if (decoderConfig.waveHeaderEnabled) {
            return GetWaveHeaderSize() + GetWaveBlockSize() * hcaInfo.blockCount;
        } else {
            return static_cast<std::uint64_t>(GetWaveBlockSize()) * hcaInfo.blockCount;
        }
    }
}

auto CHcaDecoder::Read(void *buffer, std::size_t bufferSize, std::size_t offset, std::size_t count)
    -> std::size_t {
    if (!buffer) {
        throw CArgumentException("CHcaDecoder::Read");
    }
    bufferSize = std::min(count, bufferSize - offset);
    if (bufferSize == 0) {
        return bufferSize;
    }
    auto byteBuffer = static_cast<std::uint8_t *>(buffer);

    const auto &decoderConfig   = _decoderConfig;
    auto streamPosition         = GetPosition();
    auto mappedPosition         = MapLoopedPosition(streamPosition);
    const auto waveStreamLength = GetLength();
    if (mappedPosition >= waveStreamLength) {
        return 0;
    }

    const auto waveHeaderSize = decoderConfig.waveHeaderEnabled ? GetWaveHeaderSize() : 0;
    std::size_t totalRead     = 0;
    if (mappedPosition < waveHeaderSize) {
        const auto headerLeftLength = static_cast<std::size_t>(waveHeaderSize - mappedPosition);
        const auto headerCopyLength = std::min(headerLeftLength, bufferSize);
        std::memcpy(byteBuffer + offset, GenerateWaveHeader() + mappedPosition, headerCopyLength);
        streamPosition += headerCopyLength;
        if (bufferSize <= headerCopyLength) {
            SetPosition(streamPosition);
            return bufferSize;
        }
        bufferSize -= headerCopyLength;
        offset += headerCopyLength;
        totalRead += headerCopyLength;
        mappedPosition += headerCopyLength;
    }

    // Now mappedPosition is pointed inside audio data.
    const auto waveBlockSize = GetWaveBlockSize();
    while (bufferSize > 0 && mappedPosition < waveStreamLength) {
        const auto blockIndex =
            static_cast<std::uint32_t>((mappedPosition - waveHeaderSize) / waveBlockSize);
        const auto startOffset = (mappedPosition - waveHeaderSize) % waveBlockSize;
        const auto blockData   = DecodeBlock(blockIndex);
        const auto copyLength  = std::min(
            waveStreamLength - mappedPosition,
            std::min(
                static_cast<std::uint64_t>(waveBlockSize) - startOffset,
                static_cast<std::uint64_t>(bufferSize)
            )
        );
        std::memcpy(
            byteBuffer + offset, blockData + startOffset, static_cast<std::size_t>(copyLength)
        );
        streamPosition += copyLength;
        bufferSize -= copyLength;
        offset += copyLength;
        totalRead += copyLength;
        mappedPosition += copyLength;
    }

    SetPosition(streamPosition);
    return totalRead;
}

ACB_NS_END
