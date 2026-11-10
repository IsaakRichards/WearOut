// SPDX-License-Identifier: Apache-2.0
#include "wearout/devices.h"

#include <algorithm>
#include <cctype>

namespace wearout {

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
} // namespace

const std::vector<WatchProfile> &knownWatches() {
    static const std::vector<WatchProfile> profiles = [] {
        std::vector<WatchProfile> v;

        WatchProfile op4;
        op4.id = "oneplus-watch-4";
        op4.displayName = "OnePlus Watch 4";
        // Assumption: OnePlus watches advertise "OnePlus Watch <n> <suffix>".
        // Confirm with `bluetoothctl devices` once the watch is in pairing mode.
        op4.btNamePrefixes = {"OnePlus Watch 4"};
        op4.dualEngine = true; // confirmed: Snapdragon W5 + BES2800
        // Confirmed from OnePlus' Watch 3 FAQ: setup is done by OHealth from
        // Google Play. Assumed identical for Watch 4.
        // OHealth signs with rotated keys (APK Signature Scheme v3.1), checked
        // with tools/apk_identity.py:
        //   Google Play bundle, v3.1 block (new key, what Android 13+ reports):
        //     sha1 hex    030ac13604c0782011aa95a18bda33dc93a94f7f
        //     sha1 b64url AwrBNgTAeCARqpWhi9oz3JOpT38
        //     sha256 hex  a1e80e14e2dcc6571f5799f4935efb1b655ed8a27cfbedce5db64edbfefcc47a
        //   Earlier APK, v3 block (original key, kept for older Android):
        //     sha1 hex    b58d9a4ce294abe1ab8e577d20233122b1a49199
        //     sha1 b64url tY2aTOKUq-Grjld9ICMxIrGkkZk
        //     sha256 hex  38bd2bbfa86127bd03c11195b507321358fe2ba116c54a16c713b91e39fa393e
        // Which key and which encoding the watch checks is unconfirmed. Phones
        // pairing a Watch 4 run Android 13+, so the rotated key goes first;
        // lowercase SHA-1 hex follows microG.
        op4.companion.packageName = "com.heytap.health.international";
        op4.companion.signatureDigest = "030ac13604c0782011aa95a18bda33dc93a94f7f";
        op4.companion.alternateDigests = {"b58d9a4ce294abe1ab8e577d20233122b1a49199"};
        v.push_back(op4);

        // Generic fallback for any other Wear OS watch. Kept last.
        WatchProfile generic;
        generic.id = "wear-os-generic";
        generic.displayName = "Wear OS watch";
        generic.btNamePrefixes = {"Pixel Watch", "Galaxy Watch", "TicWatch", "Wear OS"};
        v.push_back(generic);
        return v;
    }();
    return profiles;
}

const WatchProfile *matchWatch(const std::string &btName) {
    const std::string n = lower(btName);
    for (const auto &p : knownWatches()) {
        for (const auto &prefix : p.btNamePrefixes) {
            if (n.rfind(lower(prefix), 0) == 0)
                return &p;
        }
    }
    return nullptr;
}

} // namespace wearout
