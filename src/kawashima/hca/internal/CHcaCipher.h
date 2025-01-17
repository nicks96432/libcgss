#ifndef ACB_KAWASHIMA_HCA_CHCACIPHER_H_
#define ACB_KAWASHIMA_HCA_CHCACIPHER_H_

#include <array>
#include <cstdint>

#include "acb_cdata.h"
#include "acb_enum.h"
#include "acb_env.h"
#include "kawashima/hca/CHcaCipherConfig.h"

ACB_NS_BEGIN

class CHcaCipher {

public:
    CHcaCipher();

    CHcaCipher(const HCA_CIPHER_CONFIG &config);

    CHcaCipher(const CHcaCipherConfig &config);

    CHcaCipher(const CHcaCipher &);

    auto InitEncryptTable() -> bool_t;

    void Decrypt(std::uint8_t *data, std::uint32_t size) const;

    void Encrypt(std::uint8_t *data, std::uint32_t size) const;

private:
    auto Init(const CHcaCipherConfig &config) -> bool_t;

    static constexpr std::uint32_t TableSize = 0x100;

    std::array<std::uint8_t, TableSize> _decryptTable;
    std::array<std::uint8_t, TableSize> _encryptTable;

    void Init0();

    void Init1();

    void Init56(std::uint32_t key1, std::uint32_t key2);

    void Init56_CreateTable(std::array<std::uint8_t, 0x10> &table, std::uint8_t key);

    HcaCipherType _cipherType;
};

ACB_NS_END

#endif // ACB_KAWASHIMA_HCA_CHCACIPHER_H_
