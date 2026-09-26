#include "StateCodec.h"

namespace pitchlane::state {

using namespace juce;

namespace ids {
static const Identifier root { "PitchLaneState" };
static const Identifier version { "version" };
static const Identifier reference { "Reference" };
static const Identifier note { "Note" };
static const Identifier sourcePath { "sourcePath" };
static const Identifier start { "s" };
static const Identifier length { "l" };
static const Identifier pitch { "p" };
static const Identifier confidence { "c" };
static const Identifier velocity { "v" };
} // namespace ids

ValueTree notesToTree(const NoteList& notes)
{
    ValueTree ref(ids::reference);
    for (const auto& n : notes)
    {
        ValueTree t(ids::note);
        t.setProperty(ids::start, n.start, nullptr);
        t.setProperty(ids::length, n.length, nullptr);
        t.setProperty(ids::pitch, n.pitch, nullptr);
        t.setProperty(ids::confidence, static_cast<double>(n.confidence), nullptr);
        t.setProperty(ids::velocity, n.velocity, nullptr);
        ref.appendChild(t, nullptr);
    }
    return ref;
}

NoteList notesFromTree(const ValueTree& ref)
{
    NoteList notes;
    for (const auto& t : ref)
    {
        if (!t.hasType(ids::note)) continue;
        RefNote n;
        n.start = static_cast<double>(t.getProperty(ids::start, 0.0));
        n.length = static_cast<double>(t.getProperty(ids::length, 0.1));
        n.pitch = static_cast<int>(t.getProperty(ids::pitch, 60));
        n.confidence = static_cast<float>(static_cast<double>(t.getProperty(ids::confidence, 1.0)));
        n.velocity = static_cast<int>(t.getProperty(ids::velocity, 100));
        notes.push_back(n);
    }
    sanitiseNotes(notes);
    sortNotes(notes);
    return notes;
}

ValueTree makeStateTree(AudioProcessorValueTreeState& apvts, const ReferenceModel& model, const MarkerModel* markers)
{
    ValueTree root(ids::root);
    root.setProperty(ids::version, kVersion, nullptr);
    root.appendChild(apvts.copyState(), nullptr);
    auto ref = notesToTree(model.getNotes());
    ref.setProperty(ids::sourcePath, model.getSourcePath(), nullptr);
    root.appendChild(ref, nullptr);
    if (markers != nullptr) root.appendChild(markers->toTree(), nullptr);
    return root;
}

bool applyStateTree(const ValueTree& root, AudioProcessorValueTreeState& apvts, ReferenceModel& model, MarkerModel* markers)
{
    if (!root.hasType(ids::root)) return false;
    const auto params = root.getChildWithName(apvts.state.getType());
    if (params.isValid()) apvts.replaceState(params.createCopy());
    const auto ref = root.getChildWithName(ids::reference);
    if (ref.isValid())
    {
        model.setNotes(notesFromTree(ref), false);
        model.setSourcePath(ref.getProperty(ids::sourcePath, String()).toString());
    }
    if (markers != nullptr) markers->fromTree(root.getChildWithName(MarkerModel::treeType)); // v1 state: no markers
    return true;
}

void writeBinary(AudioProcessorValueTreeState& apvts, const ReferenceModel& model, MemoryBlock& dest,
                 const MarkerModel* markers)
{
    if (auto xml = makeStateTree(apvts, model, markers).createXml())
        AudioProcessor::copyXmlToBinary(*xml, dest);
}

bool readBinary(const void* data, int size, AudioProcessorValueTreeState& apvts, ReferenceModel& model,
                MarkerModel* markers)
{
    if (auto xml = AudioProcessor::getXmlFromBinary(data, size))
        return applyStateTree(ValueTree::fromXml(*xml), apvts, model, markers);
    return false;
}

} // namespace pitchlane::state
