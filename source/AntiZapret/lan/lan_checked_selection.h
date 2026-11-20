#pragma once
#if defined(ANTIZAPRET_LAN)
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace lan
{
// The picker supplies a preorder tree with exclusive subtree ends. Only checked
// entries and their actual directory ancestors are copied. A checked directory
// does not imply that unchecked descendants should be sent.
template<class Entry>
std::vector<std::size_t> CheckedSelection(const std::vector<Entry>& items)
{
    std::vector<std::size_t> ancestors;
    std::vector<bool> keep(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        while (!ancestors.empty() && items[ancestors.back()].end == i) ancestors.pop_back();
        const auto& e = items[i];
        if (e.depth != ancestors.size() || e.end <= i || e.end > items.size() ||
            (!e.directory && e.end != i + 1) || (!ancestors.empty() && e.end > items[ancestors.back()].end))
            throw std::runtime_error("Malformed selection tree");
        if (e.checked)
        {
            keep[i] = true;
            for (auto parent : ancestors) keep[parent] = true;
        }
        if (e.directory) ancestors.push_back(i);
    }
    std::vector<std::size_t> selected;
    for (std::size_t i = 0; i < keep.size(); ++i) if (keep[i]) selected.push_back(i);
    return selected;
}
}
#endif
