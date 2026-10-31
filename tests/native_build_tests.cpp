#include "sporekit_build.hpp"
#include <cstdio>
#include <string>

int main()
{
    const char* original = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
    const char* laa = "47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab";
    if (!skg::supported_gog2024(original) || !skg::supported_gog2024(laa) ||
        skg::supported_gog2024(nullptr) || skg::supported_gog2024("") ||
        skg::supported_gog2024("dc04aee5")) return 1;
    for (const char* hash : {original, laa}) {
        for (size_t i = 0; i < 64; ++i) {
            std::string other(hash);
            other[i] = other[i] == '0' ? '1' : '0';
            if (skg::supported_gog2024(other.c_str())) return 1;
        }
        if (skg::supported_gog2024((std::string(hash) + "0").c_str())) return 1;
    }
    std::puts("native executable identities: PASS");
    return 0;
}
