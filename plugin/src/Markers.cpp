#include "Markers.h"

#include <algorithm>
#include <cmath>

namespace pitchlane {

using namespace juce;

const Identifier MarkerModel::treeType { "Markers" };

namespace {
const Identifier markerId { "Marker" };
const Identifier timeId { "t" };
const Identifier nameId { "name" };
constexpr size_t kMaxMarkers = 256;

String cleanName(const String& s)
{
    auto n = s.trim().substring(0, 64);
    return n.isEmpty() ? String("Section") : n;
}
} // namespace

void MarkerModel::sortMarkers(std::vector<SectionMarker>& m)
{
    std::stable_sort(m.begin(), m.end(), [](const SectionMarker& a, const SectionMarker& b) { return a.time < b.time; });
}

std::vector<SectionMarker> MarkerModel::getMarkers() const
{
    const ScopedLock sl(lock_);
    return markers_;
}

int MarkerModel::size() const
{
    const ScopedLock sl(lock_);
    return static_cast<int>(markers_.size());
}

int MarkerModel::add(double time, const String& name)
{
    int index = -1;
    {
        const ScopedLock sl(lock_);
        if (markers_.size() >= kMaxMarkers || !std::isfinite(time)) return -1;
        SectionMarker m { std::max(0.0, time), cleanName(name) };
        auto it = std::upper_bound(markers_.begin(), markers_.end(), m.time,
                                   [](double t, const SectionMarker& x) { return t < x.time; });
        index = static_cast<int>(it - markers_.begin());
        markers_.insert(it, m);
    }
    sendChangeMessage();
    return index;
}

bool MarkerModel::rename(int index, const String& name)
{
    {
        const ScopedLock sl(lock_);
        if (index < 0 || index >= static_cast<int>(markers_.size())) return false;
        markers_[static_cast<size_t>(index)].name = cleanName(name);
    }
    sendChangeMessage();
    return true;
}

bool MarkerModel::remove(int index)
{
    {
        const ScopedLock sl(lock_);
        if (index < 0 || index >= static_cast<int>(markers_.size())) return false;
        markers_.erase(markers_.begin() + index);
    }
    sendChangeMessage();
    return true;
}

int MarkerModel::move(int index, double time)
{
    int newIndex = -1;
    {
        const ScopedLock sl(lock_);
        if (index < 0 || index >= static_cast<int>(markers_.size()) || !std::isfinite(time)) return -1;
        auto m = markers_[static_cast<size_t>(index)];
        m.time = std::max(0.0, time);
        markers_.erase(markers_.begin() + index);
        auto it = std::upper_bound(markers_.begin(), markers_.end(), m.time,
                                   [](double t, const SectionMarker& x) { return t < x.time; });
        newIndex = static_cast<int>(it - markers_.begin());
        markers_.insert(it, m);
    }
    sendChangeMessage();
    return newIndex;
}

void MarkerModel::setMarkers(std::vector<SectionMarker> markers)
{
    markers.erase(std::remove_if(markers.begin(), markers.end(),
                                 [](const SectionMarker& m) { return !std::isfinite(m.time); }),
                  markers.end());
    if (markers.size() > kMaxMarkers) markers.resize(kMaxMarkers);
    for (auto& m : markers)
    {
        m.time = std::max(0.0, m.time);
        m.name = cleanName(m.name);
    }
    sortMarkers(markers);
    {
        const ScopedLock sl(lock_);
        markers_ = std::move(markers);
    }
    sendChangeMessage();
}

void MarkerModel::clear()
{
    setMarkers({});
}

int MarkerModel::indexAt(double time) const
{
    const ScopedLock sl(lock_);
    auto it = std::upper_bound(markers_.begin(), markers_.end(), time,
                               [](double t, const SectionMarker& x) { return t < x.time; });
    return static_cast<int>(it - markers_.begin()) - 1;
}

String MarkerModel::letterFor(int index)
{
    if (index < 0) return {};
    String s;
    int n = index;
    do
    {
        s = String::charToString(static_cast<juce_wchar>('A' + n % 26)) + s;
        n = n / 26 - 1;
    } while (n >= 0);
    return s;
}

ValueTree MarkerModel::toTree() const
{
    ValueTree t(treeType);
    for (const auto& m : getMarkers())
    {
        ValueTree c(markerId);
        c.setProperty(timeId, m.time, nullptr);
        c.setProperty(nameId, m.name, nullptr);
        t.appendChild(c, nullptr);
    }
    return t;
}

void MarkerModel::fromTree(const ValueTree& tree)
{
    std::vector<SectionMarker> ms;
    if (tree.hasType(treeType))
        for (const auto& c : tree)
            if (c.hasType(markerId) && c.hasProperty(timeId))
                ms.push_back({ static_cast<double>(c.getProperty(timeId)), c.getProperty(nameId).toString() });
    setMarkers(std::move(ms));
}

} // namespace pitchlane
