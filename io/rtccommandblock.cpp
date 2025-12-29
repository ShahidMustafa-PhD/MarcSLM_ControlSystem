#include "rtccommandblock.h"
#include <algorithm>

namespace marc {

const RTCCommandBlock::ParameterSegment* RTCCommandBlock::getSegmentFor(size_t cmdIndex) const {
    for (const auto& seg : parameterSegments) {
        if (cmdIndex >= seg.startCmd && cmdIndex <= seg.endCmd) {
            return &seg;
        }
    }
    return nullptr;
}

void RTCCommandBlock::addParameterSegment(uint32_t buildStyleId,
                                          double laserPower, double laserSpeed, double jumpSpeed,
                                          uint32_t laserMode, double laserFocus) {
    ParameterSegment seg;
    seg.buildStyleId = buildStyleId;
    seg.laserPower = laserPower;
    seg.laserSpeed = laserSpeed;
    seg.jumpSpeed = jumpSpeed;
    seg.laserMode = laserMode;
    seg.laserFocus = laserFocus;

    if (!parameterSegments.empty()) {
        seg.startCmd = parameterSegments.back().endCmd + 1;
    } else {
        seg.startCmd = 0;
    }

    parameterSegments.push_back(seg);
}

} // namespace marc
