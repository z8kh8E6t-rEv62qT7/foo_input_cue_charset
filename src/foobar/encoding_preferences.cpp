#include "foobar_sdk.hpp"
#include <foobar2000/helpers/atl-misc.h>
#include <foobar2000/helpers/DarkMode.h>

#include "encoding_preferences.hpp"
#include "charset_analyzer.hpp"
#include "cue_encoding.hpp"
#include "resource.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace cue_charset::foobar_component {
namespace {

constexpr GUID priority_setting_guid = {
    0x6103c5e9, 0xe93a, 0x4a57, {0xa1, 0xf4, 0x2d, 0x3b, 0x81, 0xc8, 0x5a, 0x10}};
constexpr GUID preferences_guid = {
    0xf3102c0d, 0x6c7e, 0x4c91, {0x89, 0xb3, 0x5c, 0x26, 0x65, 0x79, 0xac, 0x42}};

cfg_string g_encoding_priority(priority_setting_guid, detail::default_encoding_priority.data());

class EncodingPreferences : public CDialogImpl<EncodingPreferences>,
    public preferences_page_instance {
public:
    enum { IDD = IDD_ENCODING_PREFERENCES };
    explicit EncodingPreferences(preferences_page_callback::ptr callback)
        : callback_(std::move(callback)) {}

    t_uint32 get_state() override {
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        const auto saved = g_encoding_priority.get();
        if (detail::serialize_encoding_priority(draft_) != saved.c_str()) {
            state |= preferences_state::changed;
        }
        return state;
    }

    void apply() override {
        try {
            detail::validate_encoding_priority(shared_charset_analyzer(), draft_);
            g_encoding_priority.set(detail::serialize_encoding_priority(draft_).c_str());
            status("Saved. Reload info from file(s) to refresh existing tracks.");
            callback_->on_state_changed();
        } catch (const std::exception& error) {
            status(error.what());
        }
    }

    void reset() override {
        draft_ = detail::parse_encoding_priority(detail::default_encoding_priority);
        refresh(0);
        status("Default restored. Click Apply to save.");
        callback_->on_state_changed();
    }

    BEGIN_MSG_MAP_EX(EncodingPreferences)
        MSG_WM_INITDIALOG(on_init)
        COMMAND_HANDLER_EX(IDC_ENCODING_LIST, LBN_SELCHANGE, on_selection)
        COMMAND_HANDLER_EX(IDC_ENCODING_ADD, BN_CLICKED, on_add)
        COMMAND_HANDLER_EX(IDC_ENCODING_REMOVE, BN_CLICKED, on_remove)
        COMMAND_HANDLER_EX(IDC_ENCODING_UP, BN_CLICKED, on_move)
        COMMAND_HANDLER_EX(IDC_ENCODING_DOWN, BN_CLICKED, on_move)
    END_MSG_MAP()

private:
    BOOL on_init(CWindow, LPARAM) {
        dark_.AddDialogWithControls(*this);
        SendDlgItemMessage(IDC_ENCODING_NAME, EM_SETLIMITTEXT,
            detail::max_encoding_name_length, 0);
        try {
            draft_ = configured_encoding_priority();
        } catch (const std::exception& error) {
            status(error.what());
        }
        refresh(0);
        return FALSE;
    }

    WTL::CListBox list() const { return WTL::CListBox(GetDlgItem(IDC_ENCODING_LIST)); }

    void status(const char* text) { uSetDlgItemText(m_hWnd, IDC_ENCODING_STATUS, text); }

    void refresh(const int selection) {
        auto control = list();
        control.ResetContent();
        for (const auto& encoding : draft_) {
            control.AddString(pfc::stringcvt::string_wide_from_utf8(encoding.c_str()));
        }
        if (!draft_.empty()) {
            control.SetCurSel(std::clamp(selection, 0, static_cast<int>(draft_.size()) - 1));
        }
        update_buttons();
    }

    void update_buttons() {
        const int selected = list().GetCurSel();
        const bool valid = selected >= 0 && static_cast<std::size_t>(selected) < draft_.size();
        ::EnableWindow(GetDlgItem(IDC_ENCODING_REMOVE), valid);
        ::EnableWindow(GetDlgItem(IDC_ENCODING_UP), valid && selected > 0);
        ::EnableWindow(GetDlgItem(IDC_ENCODING_DOWN), valid &&
            static_cast<std::size_t>(selected + 1) < draft_.size());
        ::EnableWindow(GetDlgItem(IDC_ENCODING_ADD), draft_.size() < detail::max_encoding_priorities);
    }

    void changed(const int selected) {
        refresh(selected);
        status("Pending changes. Click Apply to save.");
        callback_->on_state_changed();
    }

    void on_selection(UINT, int, CWindow) { update_buttons(); }

    void on_add(UINT, int, CWindow) {
        try {
            pfc::string8 value;
            uGetDlgItemText(m_hWnd, IDC_ENCODING_NAME, value);
            const auto name = detail::parse_encoding_priority(value.c_str());
            if (name.size() != 1) throw Error("Enter one encoding name");
            auto text = detail::serialize_encoding_priority(draft_);
            if (!text.empty()) text += '\n';
            text += name.front();
            auto updated = detail::parse_encoding_priority(text);
            detail::validate_encoding_priority(shared_charset_analyzer(), name);
            draft_ = std::move(updated);
            uSetDlgItemText(m_hWnd, IDC_ENCODING_NAME, "");
            changed(static_cast<int>(draft_.size()) - 1);
        } catch (const std::exception& error) {
            status(error.what());
        }
    }

    void on_remove(UINT, int, CWindow) {
        const int selected = list().GetCurSel();
        if (selected < 0 || static_cast<std::size_t>(selected) >= draft_.size()) return;
        draft_.erase(draft_.begin() + selected);
        changed(selected);
    }

    void on_move(UINT, const int id, CWindow) {
        const int selected = list().GetCurSel();
        const int target = selected + (id == IDC_ENCODING_UP ? -1 : 1);
        if (selected < 0 || target < 0 ||
            static_cast<std::size_t>(selected) >= draft_.size() ||
            static_cast<std::size_t>(target) >= draft_.size()) return;
        std::swap(draft_[static_cast<std::size_t>(selected)], draft_[static_cast<std::size_t>(target)]);
        changed(target);
    }

    preferences_page_callback::ptr callback_;
    fb2k::CDarkModeHooks dark_;
    std::vector<std::string> draft_;
};

class EncodingPreferencesPage : public preferences_page_impl<EncodingPreferences> {
public:
    const char* get_name() override { return "CUE Charset Input"; }
    GUID get_guid() override { return preferences_guid; }
    GUID get_parent_guid() override { return guid_tools; }
};

preferences_page_factory_t<EncodingPreferencesPage> g_encoding_preferences_factory;

} // namespace

std::vector<std::string> configured_encoding_priority() {
    const auto text = g_encoding_priority.get();
    return detail::parse_encoding_priority(text.c_str());
}

} // namespace cue_charset::foobar_component
