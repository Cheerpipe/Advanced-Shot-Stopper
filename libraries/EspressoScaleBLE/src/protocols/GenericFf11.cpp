#include "ScaleProtocol.h"

namespace {

static const uint8_t GENERIC_PRODUCT = 0x03;
static const uint8_t GENERIC_TYPE = 0x0a;

static const char *const kGenericPrefixes[] = {"BOOKO"};

static const ScaleFeatureSet kGenericFeatures = {
    SCALE_CORE_FEATURES | ScaleFeatureCombinedTareStart |
        ScaleFeatureIndependentBeep | ScaleFeatureVolume |
        ScaleFeatureCommandAudibleFeedback | ScaleFeaturePowerOff,
    0,
    5,
    0,
    8000,
    100,
    500
};

void fillGenericCommand(uint8_t out[6], uint8_t data1, uint8_t data2, uint8_t data3) {
    out[0] = GENERIC_PRODUCT;
    out[1] = GENERIC_TYPE;
    out[2] = data1;
    out[3] = data2;
    out[4] = data3;
    out[5] = static_cast<uint8_t>(out[0] ^ out[1] ^ out[2] ^ out[3] ^ out[4]);
}

bool genericSupportedPacketLength(int length) {
    return length == 20;
}

bool parseGenericWeight(const uint8_t *data, int length, float *weight) {
    if (length != 20 || data[0] != 0x03 || data[1] != 0x0b ||
        (data[6] != '-' && data[6] != '+' && data[6] != ' ' &&
         data[6] != 0x00)) {
        return false;
    }
    uint8_t checksum = 0;
    for (int i = 0; i < length - 1; ++i) checksum ^= data[i];
    if (checksum != data[length - 1]) return false;

    const uint32_t raw = (static_cast<uint32_t>(data[7]) << 16) |
                         (static_cast<uint32_t>(data[8]) << 8) |
                         data[9];
    *weight = static_cast<float>(raw) / 100.0f;
    if (data[6] == '-') {
        *weight = -*weight;
    }
    return scaleValidWeight(*weight);
}

bool parseGenericTimer(const uint8_t *data, int length, uint32_t *timerMs) {
    float ignoredWeight = 0.0f;
    if (!parseGenericWeight(data, length, &ignoredWeight)) {
        return false;
    }
    *timerMs = (static_cast<uint32_t>(data[2]) << 16) |
               (static_cast<uint32_t>(data[3]) << 8) | data[4];
    return true;
}

bool encodeGenericCommand(ScaleOp op, uint8_t arg, uint8_t *out, int *length) {
    const uint8_t code = scaleBookooOpcode(op);
    if (code == 0) return false;
    uint8_t command[6];
    fillGenericCommand(command, code, 0x00,
                       op == ScaleOp::SetVolume ? arg : 0);
    return scaleCopyPayload(command, sizeof(command), out, length);
}

} // namespace

const ScaleProtocol kScaleProtocolGenericFf11 = {
    "bookoo_generic",
    "Generic scale detected",
    "ff11",
    "ff12",
    kGenericPrefixes,
    sizeof(kGenericPrefixes) / sizeof(kGenericPrefixes[0]),
    kGenericFeatures,
    &genericSupportedPacketLength,
    &parseGenericWeight,
    &parseGenericTimer,
    &encodeGenericCommand,
    nullptr,
    0,
    false
};
