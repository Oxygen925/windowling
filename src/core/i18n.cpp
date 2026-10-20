#include "core/i18n.h"

#include <toml++/toml.hpp>

namespace dp::core {

namespace {
void Flatten(const toml::table& t, const std::string& prefix, std::map<std::string, std::string>& out) {
    for (const auto& [k, v] : t) {
        const std::string key = prefix.empty() ? std::string(k.str()) : prefix + "." + std::string(k.str());
        if (const auto* sub = v.as_table()) Flatten(*sub, key, out);
        else if (auto s = v.value<std::string>()) out[key] = *s;
    }
}
}  // namespace

std::string Strings::Load(const std::string& text) {
    map_.clear();
    try {
        Flatten(toml::parse(text), "", map_);
    } catch (const toml::parse_error& e) {
        return std::string(e.description());
    }
    return {};
}

std::string Strings::Get(std::string_view key) const {
    if (auto it = map_.find(std::string(key)); it != map_.end()) return it->second;
    if (fallback_) return fallback_->Get(key);
    return std::string(key);
}

std::vector<std::string> Strings::Keys() const {
    std::vector<std::string> out;
    for (const auto& [k, _] : map_) out.push_back(k);
    return out;
}

std::string ResolveLanguage(std::string_view setting, std::string_view uiLocale) {
    if (setting == "ko" || setting == "en") return std::string(setting);
    return uiLocale.substr(0, 2) == "ko" ? "ko" : "en";
}

}  // namespace dp::core
