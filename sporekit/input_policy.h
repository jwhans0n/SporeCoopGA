// Startup-only policy for explicitly isolated, hidden Windows lab instances.
#pragma once

#include <string_view>

namespace sk {
enum class InputPolicyReason { Legacy, Exclusive, Arguments, Invisible, Profile, Instance };
struct InputPolicy {
    bool requested = false, exclusive = false;
    InputPolicyReason reason = InputPolicyReason::Legacy;
};

inline const char* input_policy_reason(InputPolicyReason reason)
{
    switch (reason) {
    case InputPolicyReason::Legacy: return "not-requested";
    case InputPolicyReason::Exclusive: return "hidden-private-lab";
    case InputPolicyReason::Arguments: return "ambiguous-arguments";
    case InputPolicyReason::Invisible: return "invisible-required";
    case InputPolicyReason::Profile: return "explicit-winlab-profile-required";
    case InputPolicyReason::Instance: return "instance-at-least-two-required";
    }
    return "invalid-policy";
}

inline bool input_private_profile(std::wstring_view value)
{
    if (!value.starts_with(L"winlab_") || value.size() <= 7 || value.size() > 64) return false;
    for (wchar_t c : value)
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
              (c >= L'0' && c <= L'9') || c == L'_' || c == L'-')) return false;
    return true;
}

inline InputPolicy select_input_policy(std::wstring_view command, bool invisible,
                                      std::wstring_view installed_profile, int instance)
{
    InputPolicy result;
    if (command.size() > 32767) { result.reason = InputPolicyReason::Arguments; return result; }
    unsigned argument = 0, requested = 0, hidden = 0, profiles = 0;
    std::wstring_view profile;
    bool valid = true;
    const auto space = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    while (!command.empty()) {
        while (!command.empty() && space(command.front())) command.remove_prefix(1);
        if (command.empty()) break;
        std::wstring_view token;
        if (command.front() == L'"') {
            command.remove_prefix(1);
            const auto end = command.find(L'"');
            if (end == command.npos) { valid = false; break; }
            token = command.substr(0, end); command.remove_prefix(end + 1);
            if (!command.empty() && !space(command.front())) { valid = false; break; }
        } else {
            size_t end = 0;
            while (end < command.size() && !space(command[end])) ++end;
            token = command.substr(0, end); command.remove_prefix(end);
            if (token.find(L'"') != token.npos) { valid = false; break; }
        }
        if (!argument++) continue; // The executable name is never an option.
        if (token == L"-sporekit-exclusive-input") ++requested;
        if (token == L"-sporekit-invisible") ++hidden;
        constexpr std::wstring_view prefix = L"-sporekit-profile:";
        if (token.starts_with(prefix)) { ++profiles; profile = token.substr(prefix.size()); }
    }
    result.requested = requested != 0;
    if (!result.requested) return result;
    if (!valid || requested != 1 || hidden > 1 || profiles > 1) result.reason = InputPolicyReason::Arguments;
    else if (!invisible || hidden != 1) result.reason = InputPolicyReason::Invisible;
    else if (profiles != 1 || !input_private_profile(profile) || profile != installed_profile) result.reason = InputPolicyReason::Profile;
    else if (instance < 2) result.reason = InputPolicyReason::Instance;
    else { result.exclusive = true; result.reason = InputPolicyReason::Exclusive; }
    return result;
}
} // namespace sk
