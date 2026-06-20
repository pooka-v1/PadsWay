#pragma once
#include <string>

namespace Strings {
    // Loads strings from data/strings/strings_{locale}.json.
    // Falls back to key name if the file is missing or a key is not found.
    void load(const std::string& locale);
}

// Returns the UI string for the given dot-separated key (e.g. "tab.pads").
// Never returns null — falls back to the key itself if not found.
const char* tr(const std::string& key);

// Convenience for ImGui labels that need a hidden ID suffix.
// trid("btn.save", "mapSave") -> "Save##mapSave"
// The returned std::string is valid for the duration of the calling expression.
inline std::string trid(const char* key, const char* imgui_id) {
    return std::string(tr(key)) + "##" + imgui_id;
}
