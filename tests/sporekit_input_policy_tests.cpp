#include "../sporekit/input_policy.h"
#include <cstdio>
#include <string>

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main()
{
    using namespace sk;
    const std::wstring flags = L" -sporekit-exclusive-input -sporekit-invisible -sporekit-profile:winlab_input_95";
    const auto policy = [&](std::wstring_view command, bool invisible = true, std::wstring_view profile = L"winlab_input_95", int instance = 95) {
        return select_input_policy(command, invisible, profile, instance);
    };
    for (const auto& exe : {L"SporeApp.exe", L"\"D:\\Private Lab\\SporeApp.exe\""}) {
        const auto result = policy(std::wstring(exe) + flags);
        CHECK(result.exclusive && result.requested && result.reason == InputPolicyReason::Exclusive);
    }
    CHECK(policy(L"game -sporekit-exclusive-input\t-sporekit-invisible\t-sporekit-profile:winlab_input_95").exclusive);
    CHECK(policy(L"game \"-sporekit-exclusive-input\" -sporekit-invisible \"-sporekit-profile:winlab_input_95\"").exclusive);
    CHECK(!policy(L"game -sporekit-invisible -sporekit-profile:winlab_input_95").requested);
    CHECK(!policy(L"game -sporekit-exclusive-input-suffix -sporekit-invisible -sporekit-profile:winlab_input_95").requested);
    CHECK(!policy(L"\"game -sporekit-exclusive-input\" -sporekit-invisible -sporekit-profile:winlab_input_95").requested);
    CHECK(!policy(L"-sporekit-exclusive-input -sporekit-invisible -sporekit-profile:winlab_input_95").requested);
    CHECK(!policy(L"game -sporekit-exclusive-input -sporekit-profile:winlab_input_95").exclusive);
    CHECK(policy(L"game" + flags, false).reason == InputPolicyReason::Invisible);
    CHECK(policy(L"game" + flags, true, L"personal").reason == InputPolicyReason::Profile);
    CHECK(policy(L"game" + flags, true, L"winlab_input_95", 1).reason == InputPolicyReason::Instance);
    CHECK(policy(L"game" + flags, true, L"winlab_input_95", 2).exclusive);
    for (const auto& tail : {L" -sporekit-exclusive-input", L" -sporekit-invisible", L" -sporekit-profile:winlab_input_95", L" -sporekit-profile:personal"})
        CHECK(policy(L"game" + flags + tail).reason == InputPolicyReason::Arguments);
    CHECK(!policy(L"game" + flags + L" \"unterminated").exclusive);
    CHECK(!policy(L"game" + flags + L" \"extra\"suffix").exclusive);
    CHECK(!policy(L"game" + flags + L" bad\"token").exclusive);
    CHECK(!policy(L"game -sporekit-exclusive-input -sporekit-invisible").exclusive);
    for (const auto& name : {L"", L"winlab_", L"Winlab_input", L"inst95", L"personal", L"winlab_../personal", L"winlab_x/y", L"winlab_x\\y", L"winlab_x:y", L"winlab_x.y", L"winlab_x y", L"winlab_\u00e9"}) {
        CHECK(!input_private_profile(name));
        CHECK(!policy(L"game -sporekit-exclusive-input -sporekit-invisible -sporekit-profile:" + std::wstring(name), true, name).exclusive);
    }
    CHECK(input_private_profile(L"winlab_A-95_2"));
    CHECK(input_private_profile(L"winlab_" + std::wstring(57, L'x')));
    CHECK(!input_private_profile(L"winlab_" + std::wstring(58, L'x')));
    CHECK(!policy(std::wstring(32768, L'x') + flags).exclusive);
    for (auto reason : {InputPolicyReason::Legacy, InputPolicyReason::Exclusive, InputPolicyReason::Arguments,
                        InputPolicyReason::Invisible, InputPolicyReason::Profile, InputPolicyReason::Instance})
        CHECK(std::string(input_policy_reason(reason)).size() < 64);
    std::printf("sporekit input policy: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
