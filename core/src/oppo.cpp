// SPDX-License-Identifier: Apache-2.0
#include "wearout/oppo.h"

namespace wearout::oppo {

Channel channelOf(const std::string &path) {
    if (path == kToWatchMessage || path == kToPhoneMessage)
        return Channel::Message;
    if (path == kToWatchData || path == kToPhoneData)
        return Channel::Data;
    if (path == kToWatchCtrl || path == kToPhoneCtrl)
        return Channel::Ctrl;
    return Channel::None;
}

bool isToWatch(const std::string &path) {
    return path == kToWatchMessage || path == kToWatchData || path == kToWatchCtrl;
}

const std::vector<std::string> &knownProtoMessages() {
    // From `com.op.proto.*` in OHealth 5.0.11. Names only; layouts TBD.
    static const std::vector<std::string> msgs = {
        "AboutWatchProto", "AutoPauseSport", "BodyProperty", "CalorieGoal",
        "CapabilityHand", "CapabilityPass", "ConnectResult", "DeviceInfo",
        "ECGMeasureType", "ErrorCode", "FindDeviceProto", "FindPhoneProto",
        "HealthGoalResult", "HeartRateMeasure", "HeartRateWarn", "LaunchWatchApp",
        "MigrateStatus", "NightModeData", "OximetryState", "PasswordResult",
        "RaiseWristData", "StartSetting", "StepGoal", "SyncOOBEState", "TimeFormat",
    };
    return msgs;
}

} // namespace wearout::oppo
