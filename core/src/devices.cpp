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
        // Google Play. Assumed identical for Watch 4. Digest still unknown.
        op4.companion = {"com.heytap.health.international", ""};
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
