#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "acb_cdata.h"
#include "acb_env.h"
#include "acb_env_ns.h"
#include "ichinose/CAcbHelper.h"
#include "ichinose/CAfs2Archive.h"
#include "takamori/exceptions/CFormatException.h"
#include "takamori/streams/CBinaryReader.h"
#include "takamori/streams/IStream.h"

ACB_NS_BEGIN

static constexpr std::array<std::uint8_t, 4> Afs2Signature = {0x41, 0x46, 0x53, 0x32}; // 'AFS2'
static constexpr std::int32_t InvalidCueId                 = -1;

CAfs2Archive::CAfs2Archive(
    acb::IStream *stream, std::uint64_t offset, const std::string &fileName, bool_t disposeStream
) {
    _stream         = stream;
    _streamOffset   = offset;
    _disposeStream  = disposeStream;
    _byteAlignment  = 0;
    _version        = 0;
    _hcaKeyModifier = 0;

    _fileName = fileName;

    Initialize();
}

CAfs2Archive::~CAfs2Archive() {
    if (_disposeStream) {
        delete _stream;
        _stream = nullptr;
    }
}

auto CAfs2Archive::IsAfs2Archive(IStream *stream, std::uint64_t offset) -> bool_t {
    std::array<std::uint8_t, 4> fileSignature = {};

    auto pos = stream->GetPosition();
    stream->SetPosition(offset);
    CBinaryReader::PeekBytes(
        stream, fileSignature.data(), fileSignature.size(), 0, fileSignature.size()
    );
    stream->SetPosition(pos);

    bool_t b = TRUE;

    for (std::size_t i = 0; i < fileSignature.size(); ++i) {
        b = static_cast<bool_t>(b && fileSignature[i] == Afs2Signature[i]);
    }

    return b;
}

void CAfs2Archive::Initialize() {
    auto stream = _stream;
    auto offset = _streamOffset;

    if (!IsAfs2Archive(stream, offset)) {
        throw CFormatException("The file is not a valid AFS2 archive.");
    }

    CBinaryReader reader(stream);

    const auto version = reader.PeekUInt32LE(offset + 4);
    _version           = version;

    const auto fileCount = reader.PeekInt32LE(offset + 8);

    if (fileCount > UINT16_MAX) {
        throw CFormatException("File count exceeds max file entries.");
    }

    const auto byteAlignment = reader.PeekUInt32LE(offset + 12);
    _byteAlignment           = byteAlignment & 0xffff;
    _hcaKeyModifier          = static_cast<std::uint16_t>(byteAlignment >> 16);

    const auto offsetFieldSize = (version >> 8) & 0xff;
    std::uint32_t offsetMask   = 0;

    for (std::size_t i = 0; i < offsetFieldSize; ++i) {
        offsetMask |= static_cast<std::uint32_t>(0xff << (i * 8));
    }

    auto prevCueId           = InvalidCueId;
    auto fileOffsetFieldBase = 0x10 + fileCount * 2;

    for (std::int32_t i = 0; i < fileCount; ++i) {
        auto currentOffsetFieldBase = fileOffsetFieldBase + offsetFieldSize * i;
        AFS2_FILE_RECORD record     = {};

        record.cueId         = reader.PeekUInt16LE(offset + (0x10 + 2 * i));
        record.fileOffsetRaw = reader.PeekUInt32LE(offset + currentOffsetFieldBase);

        record.fileOffsetRaw &= offsetMask;
        record.fileOffsetRaw += offset;

        record.fileOffsetAligned =
            CAcbHelper::RoundUpToAlignment(record.fileOffsetRaw, (std::uint64_t)GetByteAlignment());

        if (i == fileCount - 1) {
            record.fileSize =
                reader.PeekUInt32LE(offset + currentOffsetFieldBase + offsetFieldSize) + offset -
                record.fileOffsetAligned;
        }

        if (prevCueId != InvalidCueId) {
            auto &rec    = _files[prevCueId];
            rec.fileSize = record.fileOffsetRaw - rec.fileOffsetAligned;
        }

        _files[record.cueId] = record;

        prevCueId = record.cueId;
    }
}

auto CAfs2Archive::GetFiles() const -> const std::map<std::uint32_t, AFS2_FILE_RECORD> & {
    return _files;
}

auto CAfs2Archive::GetVersion() const -> std::uint32_t {
    return _version;
}

auto CAfs2Archive::GetStream() const -> IStream * {
    return _stream;
}

auto CAfs2Archive::GetByteAlignment() const -> std::uint32_t {
    return _byteAlignment;
}

auto CAfs2Archive::GetHcaKeyModifier() const -> std::uint16_t {
    return _hcaKeyModifier;
}

auto CAfs2Archive::GetFileName() const -> const std::string & {
    return _fileName;
}

ACB_NS_END
