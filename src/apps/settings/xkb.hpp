#pragma once

#include <string>
#include <vector>

namespace tetra::settings {

// Keyboard layouts, variants and a few options, from xkeyboard-config's
// rules/evdev.lst.
class Xkb {
public:
    struct Entry {
        std::string name;        // "us", "intl", "ctrl:nocaps"
        std::string description; // "English (US)"
        std::string layout;      // variants: the layout they belong to
    };

    void load();

    [[nodiscard]] const std::vector<Entry>& layouts() const noexcept { return layouts_; }
    [[nodiscard]] std::vector<const Entry*> variants(const std::string& layout) const;
    [[nodiscard]] const Entry* layout(const std::string& name) const;
    [[nodiscard]] const Entry* option(const std::string& name) const;

private:
    std::vector<Entry> layouts_;
    std::vector<Entry> variants_;
    std::vector<Entry> options_;
};

} // namespace tetra::settings
