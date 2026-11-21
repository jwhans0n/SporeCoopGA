#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>
#include <vector>

// Value-only gate for the developer tree command. The candidate is an identity
// to compare, never a pointer that the caller may inspect before membership.
namespace sui_tree_root {
enum class Result { Found, Missing, Unavailable, Limit, Changed, InvalidGraph };
struct Limits { size_t nodes=65536, edges=65536; };

inline bool parse(std::string_view text, uint32_t& out)
{
    if (text.size()==10 && text[0]=='0' && (text[1]=='x' || text[1]=='X')) text.remove_prefix(2);
    if (text.size()!=8) return false;
    uint32_t value=0;
    for (char c:text) {
        unsigned digit;
        if (c>='0' && c<='9') digit=unsigned(c-'0');
        else if (c>='a' && c<='f') digit=unsigned(c-'a')+10;
        else if (c>='A' && c<='F') digit=unsigned(c-'A')+10;
        else return false;
        value=(value<<4)|digit;
    }
    if (!value) return false;
    out=value;
    return true;
}

// read_children is called only for the fresh main identity or a child identity
// returned by that traversal. A duplicate edge/cycle is not a valid UI tree.
// Finish the bounded traversal even after a match, so an incomplete tree cannot
// authorize a dump. Allocation/read failures leave output publication to caller.
template<class ReadChildren>
Result member(uintptr_t main, uintptr_t candidate, ReadChildren& read_children, Limits limits={})
{
    if (!main) return Result::Unavailable;
    if (!candidate) return Result::Missing;
    if (!limits.nodes) return Result::Limit;
    try {
        std::set<uintptr_t> visited{main};
        std::vector<uintptr_t> pending{main}, children;
        size_t edges=0;
        bool found=false;
        while (!pending.empty()) {
            const auto node=pending.back(); pending.pop_back();
            found=found || node==candidate;
            children.clear();
            if (!read_children(node,children)) return Result::Unavailable;
            if (children.size()>limits.edges-edges) return Result::Limit;
            edges+=children.size();
            for (auto child:children) {
                if (!child || !visited.insert(child).second) return Result::InvalidGraph;
                if (visited.size()>limits.nodes) return Result::Limit;
                pending.push_back(child);
            }
        }
        return found?Result::Found:Result::Missing;
    } catch (...) { return Result::Unavailable; }
}

// A getter can retire an otherwise unchanged main window's descendants. Walk
// again after the final fresh getter, with no native callbacks after that walk.
template<class CurrentMain, class ReadChildren>
Result resolve(uintptr_t candidate, CurrentMain current_main, ReadChildren read_children,
               uintptr_t& out, Limits limits={})
{
    try {
        const auto main=current_main();
        auto result=member(main,candidate,read_children,limits);
        if (result!=Result::Found) return result;
        if (current_main()!=main) return Result::Changed;
        result=member(main,candidate,read_children,limits);
        if (result==Result::Found) out=candidate;
        return result;
    } catch (...) { return Result::Unavailable; }
}
} // namespace sui_tree_root
