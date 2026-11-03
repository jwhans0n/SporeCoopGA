// The text side of the notification panels and of the co-op window's chat (portable:
// tests/text_tests.cpp). BUG-0129: lines were cut by bytes (a Cyrillic message got half the room,
// a CJK one ended inside a character) and the panel kind was guessed from English words in it.
#pragma once

#include <string>
#include <vector>

#include "net/utf8.h"

namespace notify_text {

inline bool g_fix = true; // false: the byte-based wrap and the old guesses (text_tests --old)

// one line of text that fits `cols` letter cells; s is cut at a space when one is in the right
// half of the line, else between two characters. The rest of s is returned in `rest`.
inline std::string take_line(const std::string& s, size_t cols, std::string& rest)
{
    size_t i = 0, used = 0, space = std::string::npos;
    while (i < s.size()) {
        size_t at = i;
        uint32_t cp = utf8::next(s, i);
        size_t w = utf8::wide(cp) ? 2 : 1;
        if (used + w > cols) {
            size_t cut = space != std::string::npos && (size_t)utf8::columns(s.substr(0, space)) >= cols / 2 ? space : at;
            rest = s.substr(cut + (cut < s.size() && s[cut] == ' ' ? 1 : 0));
            return s.substr(0, cut);
        }
        if (cp == ' ')
            space = at;
        used += w;
    }
    rest.clear();
    return s;
}

// s wrapped into lines of `cols` letter cells; at most max_lines (0 = any), the last one ends in
// "..." when text is left out
inline std::vector<std::string> wrap(const std::string& s, size_t cols, size_t max_lines = 0)
{
    std::vector<std::string> out;
    std::string left = s;
    while (!left.empty()) {
        if (max_lines && out.size() + 1 == max_lines && (size_t)utf8::columns(left) > cols) {
            std::string rest;
            out.push_back(take_line(left, cols - 3, rest) + "...");
            return out;
        }
        std::string rest;
        out.push_back(take_line(left, cols, rest));
        left.swap(rest);
    }
    if (out.empty())
        out.push_back("");
    return out;
}

// the panel: 46 letter cells per line, two lines
constexpr size_t kLine = 46;

inline std::vector<std::string> lines_of(const std::string& s)
{
    if (g_fix)
        return wrap(s, kLine, 2);
    // the code before BUG-0129: bytes
    if (s.size() <= kLine)
        return {s};
    size_t cut = s.rfind(' ', kLine);
    if (cut == std::string::npos || cut < kLine / 2)
        cut = kLine;
    std::string a = s.substr(0, cut), b = s.substr(cut + (s[cut] == ' ' ? 1 : 0));
    if (b.size() > kLine)
        b = b.substr(0, kLine - 3) + "...";
    return {a, b};
}

// The kind of a notice without one given by its code (system texts of the mod, in English): the
// panel colour, icon and time. Player chat always has its kind given ("chat") and is never guessed.
inline const char* kind_of(const std::string& s)
{
    auto has = [&](const char* w) { return s.find(w) != std::string::npos; };
    if (has("knocked out") || has("lost") || has("under attack") || has("failed"))
        return "danger";
    if (has("revived") || has("saved") || has("respawned") || has("evolved"))
        return "good";
    if (has(" left") || has("disconnected") || has("timeout") || has("closed"))
        return "leave";
    if (has("joined") || has("appeared") || has("connected") || has("hosting"))
        return "join";
    if (has("evolve") || has("ready") || has("vote"))
        return "vote";
    if (!g_fix && has(": ")) // system lines with a colon were styled as chat (BUG-0129)
        return "chat";
    return "info";
}

inline double life_of(const std::string& kind)
{
    return kind == "danger" ? 10 : kind == "vote" ? 10 : kind == "chat" ? 8 : 6;
}

} // namespace notify_text
