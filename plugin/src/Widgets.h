#pragma once
// Small styled controls used by the editor: value fields / steppers, segmented toggles,
// icon buttons, the Logic Sync toggle, the status dot and the ⓘ tooltip badge.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace pitchlane {

/** Small circled "i" that shows a tooltip on hover. */
class InfoDot : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InfoDot(const juce::String& tip) { setTooltip(tip); }
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
};

/** A rounded value box bound to a parameter.
    - optional ‹ › step arrows (Stepper) or a single › / ⌄ chevron that opens a menu;
    - vertical drag and mouse wheel change the value, double-click to type a value. */
class ValueField : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Style { Stepper, ChevronRight, ChevronDown, Plain };

    ValueField(juce::RangedAudioParameter* param, Style style);
    ~ValueField() override;

    std::function<juce::String(float value)> textFor;       // display text for a (denormalised) value
    std::function<float(const juce::String&)> parse;        // typed text -> value (nullptr: use the parameter's)
    std::function<void(juce::PopupMenu& menu)> buildMenu;   // chevron / click menu (item ids: your choice)
    std::function<void(int result)> menuResult;
    std::function<void()> onClick;                          // plain click (if no menu)
    float step = 1.f, fineStep = 1.f;                       // arrows / wheel / drag step (Shift = fine)
    float dragPixelsPerStep = 6.f;
    bool readOnly = false;

    void setValue(float v);                                 // one complete gesture, clamped
    float getValue() const;
    void refresh();                                         // repaint if the text changed

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    juce::String currentText() const;
    juce::Rectangle<float> leftArrow() const, rightArrow() const;
    void showMenu();
    void beginTyping();

    juce::RangedAudioParameter* param_;
    Style style_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    std::unique_ptr<juce::TextEditor> editor_;
    juce::String shownText_;
    float dragStartValue_ = 0.f;
    bool dragging_ = false, gesture_ = false;
    int pressedArrow_ = 0;
};

/** Row of mutually exclusive segments bound to a choice parameter (or to callbacks). */
class SegmentedControl : public juce::Component, public juce::TooltipClient
{
public:
    SegmentedControl(juce::StringArray labels, juce::RangedAudioParameter* param = nullptr);
    ~SegmentedControl() override;

    std::function<int()> getIndex;          // used when no parameter is bound
    std::function<void(int)> setIndex;
    void setTooltips(juce::StringArray tips) { tips_ = std::move(tips); }

    void refresh() { repaint(); }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent&) override { hover_ = -1; repaint(); }
    juce::String getTooltip() override { return hover_ >= 0 && hover_ < tips_.size() ? tips_[hover_] : juce::String(); }

private:
    int current() const;
    int segmentAt(float x) const;
    juce::StringArray labels_, tips_;
    juce::RangedAudioParameter* param_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    int hover_ = -1;
};

/** Borderless button that draws an icon path (stroked or filled). */
class IconButton : public juce::Button
{
public:
    using PathFn = std::function<juce::Path(juce::Rectangle<float>)>;
    IconButton(const juce::String& name, PathFn fn, bool filled, float iconSize);
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    bool drawFrame = false;

private:
    PathFn fn_;
    bool filled_;
    float size_;
};

/** "LOGIC SYNC" toggle with a link icon. */
class SyncButton : public juce::Button
{
public:
    SyncButton() : juce::Button("Logic Sync") { setClickingTogglesState(true); }
    void paintButton(juce::Graphics& g, bool over, bool down) override;
};

/** Status LED: green = host transport synced (brighter while playing), amber = sync on but
    no host timeline, grey = sync off / free clock. */
class StatusDot : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class State { Off, Waiting, Synced, Playing };
    void setState(State s, const juce::String& tip);
    void paint(juce::Graphics& g) override;

private:
    State state_ = State::Off;
};

/** "Reference:  <file name>  [folder]" box. Click anywhere to choose a vocal stem. */
class ReferenceField : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setFile(const juce::String& name, juce::Colour colour);
    std::function<void()> onClick;
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    juce::String name_;
    juce::Colour colour_ = theme::col::text;
};

/** Small caption with an ⓘ next to it (bottom-bar group titles). */
class Caption : public juce::Component
{
public:
    Caption(const juce::String& text, const juce::String& tip);
    void paint(juce::Graphics& g) override;
    void resized() override;
    int preferredWidth() const;

private:
    juce::String text_;
    InfoDot info_;
};

} // namespace pitchlane
