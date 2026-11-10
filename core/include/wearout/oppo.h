// SPDX-License-Identifier: Apache-2.0
//
// OnePlus/OPPO ("HeyTap") Wearable layer.
//
// Finding from scanning the OHealth APK (see docs/devices/ohealth-scan.md):
// OnePlus does NOT use a separate Bluetooth link for its watch features. Its
// phone-side service `com.oplus.wearable.linkservice.transport.gms`
// registers a standard Wear OS WearableListenerService and tunnels its own
// protocol *over the Google Data Layer* on a handful of path prefixes. The
// payloads are protobuf messages in the `com.op.proto` namespace.
//
// So WearOut's existing Wearable transport already carries OnePlus traffic:
// to act as OHealth we route these paths and (eventually) encode/decode the
// com.op.proto messages. Nothing here needs a second radio.
//
// Everything in this file is derived from static strings in OHealth and is
// unverified against live traffic; treat message field layouts as TODO until
// a capture confirms them.
#pragma once

#include <string>
#include <vector>

namespace wearout::oppo {

// Data Layer path prefixes (confirmed from OHealth's manifest intent filters
// and DEX strings). The companion listens on the /heytap/phone/* side; the
// watch listens on /heytap/watch/*.
constexpr const char *kToWatchMessage = "/heytap/watch/message";
constexpr const char *kToWatchData = "/heytap/watch/data";
constexpr const char *kToWatchCtrl = "/heytap/watch/ctrl";
constexpr const char *kToPhoneMessage = "/heytap/phone/message";
constexpr const char *kToPhoneData = "/heytap/phone/data";
constexpr const char *kToPhoneCtrl = "/heytap/phone/ctrl";

enum class Channel { None, Message, Data, Ctrl };

// Classify a Data Layer path as an OPPO tunnel path, and say whether it is
// addressed to the watch or to the phone.
Channel channelOf(const std::string &path);
bool isToWatch(const std::string &path);
inline bool isOppoPath(const std::string &path) { return channelOf(path) != Channel::None; }

// Known com.op.proto message types (names only, from the APK). Used for
// labelling traffic in logs until the wire layouts are reverse engineered.
// Keep in sync with docs/devices/ohealth-scan.md.
const std::vector<std::string> &knownProtoMessages();

} // namespace wearout::oppo
