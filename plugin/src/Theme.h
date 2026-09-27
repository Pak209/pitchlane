#pragma once
// Visual theme: colours (dark navy / charcoal panels, lavender accents, cyan live pitch),
// embedded Inter font, icon paths and the LookAndFeel used by every control.

#include <juce_gui_basics/juce_gui_basics.h>

namespace pitchlane::theme {

namespace col {
inline const juce::Colour window     { 0xff10141c };  // outer background
inline const juce::Colour header     { 0xff141a24 };
inline const juce::Colour panel      { 0xff131922 };  // left panel / bottom bar
inline const juce::Colour roll       { 0xff171d28 };  // note area (white-key rows)
inline const juce::Colour rollDark   { 0xff141a24 };  // note area (black-key rows)
inline const juce::Colour ruler      { 0xff0f141b };
inline const juce::Colour lane       { 0xff151b25 };
inline const juce::Colour hintBar    { 0xff10151c };
inline const juce::Colour field      { 0xff0d1118 };  // text fields / dropdowns
inline const juce::Colour border     { 0xff2a3140 };
inline const juce::Colour divider    { 0xff212835 };
inline const juce::Colour gridRow    { 0xff1d2430 };
inline const juce::Colour gridBeat   { 0xff1e2531 };
inline const juce::Colour gridBar    { 0xff2b3344 };
inline const juce::Colour text       { 0xffe4e7f0 };
inline const juce::Colour textDim    { 0xff8b93a7 };
inline const juce::Colour textFaint  { 0xff5c6477 };
inline const juce::Colour lavender   { 0xff9d8cff };  // accent (outlines, logo)
inline const juce::Colour lavenderHi { 0xffc9cfff };  // note highlight
inline const juce::Colour noteFill   { 0xffa49cf6 };
inline const juce::Colour noteEdge   { 0xff8173e8 };
inline const juce::Colour accentFill { 0xff1f2150 };  // selected segment / active button fill
inline const juce::Colour badge      { 0xff4a4a7a };
inline const juce::Colour cyan       { 0xff5ee6f2 };  // live pitch, in tune
inline const juce::Colour cyanNote   { 0xff8ad6de };  // live note name
inline const juce::Colour off        { 0xfff97759 };  // outside tolerance (orange-red)
inline const juce::Colour green      { 0xff3ee08a };
inline const juce::Colour amber      { 0xfff5b942 };
inline const juce::Colour whiteKey   { 0xffc9cbd3 };
inline const juce::Colour blackKey   { 0xff0e1218 };
} // namespace col

enum class Weight { Regular, Medium, SemiBold };

/** Inter at the given size / weight (falls back to the system sans-serif if loading fails). */
juce::FontOptions font(float size, Weight w = Weight::Regular);
juce::Typeface::Ptr typeface(Weight w);

namespace icons {
juce::Path wave(juce::Rectangle<float> r);          // logo
juce::Path folder(juce::Rectangle<float> r);
juce::Path link(juce::Rectangle<float> r);
juce::Path gear(juce::Rectangle<float> r);
juce::Path chevronDown(juce::Rectangle<float> r);
juce::Path chevronRight(juce::Rectangle<float> r);
juce::Path chevronLeft(juce::Rectangle<float> r);
juce::Path arrowLeft(juce::Rectangle<float> r);
juce::Path arrowRight(juce::Rectangle<float> r);
} // namespace icons

/** Soft glow behind a shape (a few expanding translucent strokes). */
void glowRoundedRect(juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float radius);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override;
    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
    int getCallOutBoxBorderSize(const juce::CallOutBox&) override { return 14; }
    float getCallOutBoxCornerSize(const juce::CallOutBox&) override { return 10.f; }
    void drawProgressBar(juce::Graphics&, juce::ProgressBar&, int w, int h, double progress, const juce::String&) override;
};

} // namespace pitchlane::theme
