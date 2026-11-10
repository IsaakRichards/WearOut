// SPDX-License-Identifier: Apache-2.0
//
// Per-device knowledge. WearOut's first targets are:
//
//   watch: OnePlus Watch 4  (Wear OS 6 / OxygenOS Watch 8, Snapdragon W5 + BES2800)
//   phone: Commodore Callback 8020 (Sailfish OS, MediaTek Helio G81, keypad flip)
//
// Anything here that is not marked "confirmed" is a working assumption that
// must be checked against a real device. See docs/devices/.
#pragma once

#include <string>
#include <vector>

namespace wearout {

struct AppIdentity {
    std::string packageName;     // phone-side package the watch app trusts
    std::string signatureDigest; // SHA-1 of that package's signing cert (see tools/apk_identity.py)
    // Other certs the same app has been signed with (key rotation). Which
    // one the watch checks is unconfirmed, so these are fallbacks to try.
    std::vector<std::string> alternateDigests;
};

struct WatchProfile {
    std::string id;                          // stable key, e.g. "oneplus-watch-4"
    std::string displayName;
    std::vector<std::string> btNamePrefixes; // matched case-insensitively against the BlueZ Name/Alias
    bool dualEngine = false;                 // has a separate RTOS co-processor
    // Phone-side companion that set the watch up. For OnePlus this is
    // OHealth; its watch-side twin only accepts Data Layer traffic that
    // claims this identity. Digest is empty until extracted from the APK.
    AppIdentity companion;
};

// All profiles WearOut knows about, most specific first.
const std::vector<WatchProfile> &knownWatches();

// Returns the profile whose name prefix matches `btName`, or nullptr.
const WatchProfile *matchWatch(const std::string &btName);

} // namespace wearout
