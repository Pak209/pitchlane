#include "Theme.h"

#include "PitchLaneBinaryData.h"

namespace pitchlane::theme {

using namespace juce;

// ---- fonts ------------------------------------------------------------------------------

Typeface::Ptr typeface(Weight w)
{
    // Typefaces are created once and shared by every editor instance.
    static Typeface::Ptr regular = Typeface::createSystemTypefaceFor(PitchLaneBinaryData::InterRegular_ttf,
                                                                     PitchLaneBinaryData::InterRegular_ttfSize);
    static Typeface::Ptr medium = Typeface::createSystemTypefaceFor(PitchLaneBinaryData::InterMedium_ttf,
                                                                    PitchLaneBinaryData::InterMedium_ttfSize);
    static Typeface::Ptr semibold = Typeface::createSystemTypefaceFor(PitchLaneBinaryData::InterSemiBold_ttf,
                                                                      PitchLaneBinaryData::InterSemiBold_ttfSize);
    switch (w)
    {
        case Weight::Medium:   return medium;
        case Weight::SemiBold: return semibold;
        case Weight::Regular:
        default:               return regular;
    }
}

FontOptions font(float size, Weight w)
{
    if (auto tf = typeface(w)) return FontOptions(tf).withHeight(size);
    return FontOptions(size, w == Weight::Regular ? Font::plain : Font::bold);
}

// ---- icons ------------------------------------------------------------------------------

namespace icons {

Path wave(Rectangle<float> r)
{
    // One and a half periods of a smooth wave, rising to the right (logo mark).
    Path p;
    const float x0 = r.getX(), w = r.getWidth(), cy = r.getCentreY(), a = r.getHeight() * 0.42f;
    p.startNewSubPath(x0, cy + a * 0.6f);
    const int n = 48;
    for (int i = 1; i <= n; ++i)
    {
        const float t = static_cast<float>(i) / n;
        const float y = cy + a * 0.6f * std::cos(t * MathConstants<float>::pi * 3.f) - a * 0.35f * t;
        p.lineTo(x0 + t * w, y);
    }
    return p;
}

Path folder(Rectangle<float> r)
{
    Path p;
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    p.startNewSubPath(x, y + h * 0.15f);
    p.lineTo(x + w * 0.38f, y + h * 0.15f);
    p.lineTo(x + w * 0.48f, y + h * 0.3f);
    p.lineTo(x + w, y + h * 0.3f);
    p.lineTo(x + w, y + h * 0.9f);
    p.lineTo(x, y + h * 0.9f);
    p.closeSubPath();
    return p;
}

Path link(Rectangle<float> r)
{
    // Two interlocking rounded links at 45 degrees (stroke this path).
    Path p;
    const float s = jmin(r.getWidth(), r.getHeight());
    const float lw = s * 0.62f, lh = s * 0.34f;
    Path a, b;
    a.addRoundedRectangle(-lw * 0.5f, -lh * 0.5f, lw, lh, lh * 0.5f);
    b = a;
    a.applyTransform(AffineTransform::translation(-s * 0.17f, 0.f));
    b.applyTransform(AffineTransform::translation(s * 0.17f, 0.f));
    p.addPath(a);
    p.addPath(b);
    p.applyTransform(AffineTransform::rotation(-MathConstants<float>::pi * 0.25f).translated(r.getCentre()));
    return p;
}

Path gear(Rectangle<float> r)
{
    Path p;
    const auto c = r.getCentre();
    const float ro = jmin(r.getWidth(), r.getHeight()) * 0.5f, ri = ro * 0.72f;
    const int teeth = 8;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a0 = MathConstants<float>::twoPi * (i - 0.5f) / (teeth * 2);
        const float a1 = MathConstants<float>::twoPi * (i + 0.5f) / (teeth * 2);
        const float rr = (i % 2 == 0) ? ro : ri;
        const Point<float> p0 = c.getPointOnCircumference(rr, a0), p1 = c.getPointOnCircumference(rr, a1);
        if (i == 0) p.startNewSubPath(p0); else p.lineTo(p0);
        p.lineTo(p1);
    }
    p.closeSubPath();
    p.addEllipse(c.x - ro * 0.34f, c.y - ro * 0.34f, ro * 0.68f, ro * 0.68f);
    p.setUsingNonZeroWinding(false);
    return p;
}

Path chevronDown(Rectangle<float> r)
{
    Path p;
    p.startNewSubPath(r.getX(), r.getY() + r.getHeight() * 0.3f);
    p.lineTo(r.getCentreX(), r.getY() + r.getHeight() * 0.75f);
    p.lineTo(r.getRight(), r.getY() + r.getHeight() * 0.3f);
    return p;
}

Path chevronRight(Rectangle<float> r)
{
    Path p;
    p.startNewSubPath(r.getX() + r.getWidth() * 0.3f, r.getY());
    p.lineTo(r.getX() + r.getWidth() * 0.75f, r.getCentreY());
    p.lineTo(r.getX() + r.getWidth() * 0.3f, r.getBottom());
    return p;
}

Path chevronLeft(Rectangle<float> r)
{
    Path p;
    p.startNewSubPath(r.getX() + r.getWidth() * 0.7f, r.getY());
    p.lineTo(r.getX() + r.getWidth() * 0.25f, r.getCentreY());
    p.lineTo(r.getX() + r.getWidth() * 0.7f, r.getBottom());
    return p;
}

Path arrowLeft(Rectangle<float> r)
{
    Path p;
    const float cy = r.getCentreY(), hh = r.getHeight() * 0.35f;
    p.startNewSubPath(r.getRight(), cy);
    p.lineTo(r.getX(), cy);
    p.startNewSubPath(r.getX() + hh, cy - hh);
    p.lineTo(r.getX(), cy);
    p.lineTo(r.getX() + hh, cy + hh);
    return p;
}

Path arrowRight(Rectangle<float> r)
{
    Path p;
    const float cy = r.getCentreY(), hh = r.getHeight() * 0.35f;
    p.startNewSubPath(r.getX(), cy);
    p.lineTo(r.getRight(), cy);
    p.startNewSubPath(r.getRight() - hh, cy - hh);
    p.lineTo(r.getRight(), cy);
    p.lineTo(r.getRight() - hh, cy + hh);
    return p;
}

} // namespace icons

void glowRoundedRect(Graphics& g, Rectangle<float> r, float corner, Colour c, float radius)
{
    const int steps = 4;
    for (int i = steps; i >= 1; --i)
    {
        const float grow = radius * static_cast<float>(i) / steps;
        g.setColour(c.withMultipliedAlpha(0.10f * (1.f - static_cast<float>(i - 1) / steps)));
        g.fillRoundedRectangle(r.expanded(grow), corner + grow);
    }
}

// ---- LookAndFeel -----------------------------------------------------------------------------

LookAndFeel::LookAndFeel()
{
    setColour(ResizableWindow::backgroundColourId, col::window);
    setColour(TextButton::buttonColourId, col::field);
    setColour(TextButton::buttonOnColourId, col::accentFill);
    setColour(TextButton::textColourOffId, col::text);
    setColour(TextButton::textColourOnId, col::lavenderHi);
    setColour(ComboBox::backgroundColourId, col::field);
    setColour(ComboBox::outlineColourId, col::border);
    setColour(ComboBox::textColourId, col::text);
    setColour(ComboBox::arrowColourId, col::textDim);
    setColour(PopupMenu::backgroundColourId, Colour(0xff1a2030));
    setColour(PopupMenu::textColourId, col::text);
    setColour(PopupMenu::headerTextColourId, col::textDim);
    setColour(PopupMenu::highlightedBackgroundColourId, col::accentFill);
    setColour(PopupMenu::highlightedTextColourId, col::lavenderHi);
    setColour(Label::textColourId, col::text);
    setColour(Slider::textBoxTextColourId, col::text);
    setColour(Slider::textBoxBackgroundColourId, col::field);
    setColour(Slider::textBoxOutlineColourId, col::border);
    setColour(Slider::trackColourId, col::lavender);
    setColour(Slider::backgroundColourId, col::field);
    setColour(Slider::thumbColourId, col::lavenderHi);
    setColour(TextEditor::backgroundColourId, col::field);
    setColour(TextEditor::textColourId, col::text);
    setColour(TextEditor::outlineColourId, col::border);
    setColour(TextEditor::focusedOutlineColourId, col::lavender);
    setColour(TextEditor::highlightColourId, col::accentFill);
    setColour(CaretComponent::caretColourId, col::lavenderHi);
    setColour(ToggleButton::textColourId, col::text);
    setColour(ToggleButton::tickColourId, col::lavenderHi);
    setColour(ToggleButton::tickDisabledColourId, col::textFaint);
    setColour(TooltipWindow::backgroundColourId, Colour(0xff1d2433));
    setColour(TooltipWindow::textColourId, col::text);
    setColour(TooltipWindow::outlineColourId, col::border);
    setColour(ProgressBar::backgroundColourId, col::field);
    setColour(ProgressBar::foregroundColourId, col::lavender);
    setColour(AlertWindow::backgroundColourId, Colour(0xff1a2030));
    setColour(AlertWindow::textColourId, col::text);
    setColour(AlertWindow::outlineColourId, col::border);
    setColour(ScrollBar::thumbColourId, col::border);
}

Typeface::Ptr LookAndFeel::getTypefaceForFont(const Font& f)
{
    if (f.getTypefaceName() == Font::getDefaultSansSerifFontName() || f.getTypefaceName().isEmpty())
        if (auto tf = typeface(f.isBold() ? Weight::SemiBold : Weight::Regular))
            return tf;
    return LookAndFeel_V4::getTypefaceForFont(f);
}

void LookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    const float corner = jmin(8.f, r.getHeight() * 0.3f);
    Colour fill = on ? col::accentFill : col::field;
    if (down) fill = fill.brighter(0.12f);
    else if (over) fill = fill.brighter(0.06f);
    if (!b.isEnabled()) fill = fill.withMultipliedAlpha(0.5f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, corner);
    const bool accent = b.getProperties()["accent"];
    g.setColour(on || accent ? col::lavender.withAlpha(b.isEnabled() ? (over ? 1.f : 0.85f) : 0.35f)
                             : (over ? col::border.brighter(0.25f) : col::border));
    g.drawRoundedRectangle(r, corner, on || accent ? 1.3f : 1.f);
}

Font LookAndFeel::getTextButtonFont(TextButton& b, int h)
{
    const bool accent = b.getProperties()["accent"];
    return Font(font(jmin(13.f, h * 0.42f), accent ? Weight::SemiBold : Weight::Medium));
}

void LookAndFeel::drawButtonText(Graphics& g, TextButton& b, bool, bool)
{
    const bool accent = b.getProperties()["accent"];
    const auto f = getTextButtonFont(b, b.getHeight());
    g.setFont(accent ? f.withExtraKerningFactor(0.06f) : f);
    Colour c = b.getToggleState() || accent ? col::lavenderHi : col::text;
    if (!b.isEnabled()) c = c.withMultipliedAlpha(0.4f);
    g.setColour(c);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(6, 2), Justification::centred, 1, 0.8f);
}

void LookAndFeel::drawToggleButton(Graphics& g, ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float h = jmin(18.f, r.getHeight() - 4.f);
    Rectangle<float> sw(r.getX() + 2.f, r.getCentreY() - h * 0.5f, h * 1.8f, h);
    const bool on = b.getToggleState();
    g.setColour(on ? col::accentFill : col::field);
    g.fillRoundedRectangle(sw, h * 0.5f);
    g.setColour(on ? col::lavender : (over ? col::border.brighter(0.2f) : col::border));
    g.drawRoundedRectangle(sw, h * 0.5f, 1.f);
    const float d = h - 6.f;
    g.setColour(on ? col::lavenderHi : col::textDim);
    g.fillEllipse(on ? sw.getRight() - d - 3.f : sw.getX() + 3.f, sw.getY() + 3.f, d, d);
    g.setColour(b.isEnabled() ? col::text : col::textFaint);
    g.setFont(font(13.f));
    g.drawFittedText(b.getButtonText(), r.withTrimmedLeft(sw.getWidth() + 10.f).toNearestInt(), Justification::centredLeft, 1);
}

void LookAndFeel::drawComboBox(Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& box)
{
    Rectangle<float> r(0.5f, 0.5f, w - 1.f, h - 1.f);
    g.setColour(col::field);
    g.fillRoundedRectangle(r, 6.f);
    g.setColour(box.isMouseOver(true) ? col::border.brighter(0.25f) : col::border);
    g.drawRoundedRectangle(r, 6.f, 1.f);
    auto arrow = Rectangle<float>(static_cast<float>(w) - 20.f, h * 0.5f - 3.f, 9.f, 6.f);
    g.setColour(col::textDim);
    g.strokePath(icons::chevronDown(arrow), PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
}

Font LookAndFeel::getComboBoxFont(ComboBox& box)
{
    return Font(font(jmin(13.5f, box.getHeight() * 0.45f)));
}

void LookAndFeel::positionComboBoxText(ComboBox& box, Label& label)
{
    label.setBounds(4, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void LookAndFeel::drawRotarySlider(Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const auto bounds = Rectangle<int>(x, y, w, h).toFloat();
    const float d = jmin(bounds.getWidth(), bounds.getHeight()) - 4.f;
    const auto c = bounds.getCentre();
    const float r = d * 0.5f;
    // body
    ColourGradient body(Colour(0xff2a3142), c.x, c.y - r, Colour(0xff0e1219), c.x, c.y + r, false);
    g.setGradientFill(body);
    g.fillEllipse(c.x - r, c.y - r, d, d);
    g.setColour(col::border.brighter(0.1f));
    g.drawEllipse(c.x - r, c.y - r, d, d, 1.f);
    // value arc
    const float arcR = r - 2.5f;
    const float angle = start + pos * (end - start);
    Path track;
    track.addCentredArc(c.x, c.y, arcR, arcR, 0.f, start, end, true);
    g.setColour(col::field.brighter(0.05f));
    g.strokePath(track, PathStrokeType(3.f, PathStrokeType::curved, PathStrokeType::rounded));
    Path arc;
    arc.addCentredArc(c.x, c.y, arcR, arcR, 0.f, start, angle, true);
    g.setColour(s.isEnabled() ? col::lavender : col::textFaint);
    g.strokePath(arc, PathStrokeType(3.f, PathStrokeType::curved, PathStrokeType::rounded));
    // pointer
    const auto tip = c.getPointOnCircumference(r * 0.55f, angle);
    const auto inner = c.getPointOnCircumference(r * 0.15f, angle);
    g.setColour(col::lavenderHi);
    g.drawLine({ inner, tip }, 2.f);
}

void LookAndFeel::drawLinearSlider(Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                   Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, 0.f, 0.f, style, s);
        return;
    }
    const float cy = y + h * 0.5f;
    Rectangle<float> track(static_cast<float>(x), cy - 2.f, static_cast<float>(w), 4.f);
    g.setColour(col::field.brighter(0.08f));
    g.fillRoundedRectangle(track, 2.f);
    g.setColour(col::lavender);
    g.fillRoundedRectangle(track.withRight(pos), 2.f);
    g.setColour(col::lavenderHi);
    g.fillEllipse(pos - 6.f, cy - 6.f, 12.f, 12.f);
}

Label* LookAndFeel::createSliderTextBox(Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(font(12.5f));
    l->setColour(Label::outlineColourId, Colours::transparentBlack);
    l->setColour(Label::backgroundColourId, Colours::transparentBlack);
    return l;
}

void LookAndFeel::drawPopupMenuBackground(Graphics& g, int w, int h)
{
    g.fillAll(findColour(PopupMenu::backgroundColourId));
    g.setColour(col::border);
    g.drawRect(0, 0, w, h);
}

Font LookAndFeel::getPopupMenuFont()
{
    return Font(font(14.f));
}

void LookAndFeel::drawTooltip(Graphics& g, const String& text, int w, int h)
{
    Rectangle<float> r(0.f, 0.f, static_cast<float>(w), static_cast<float>(h));
    g.setColour(findColour(TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle(r, 6.f);
    g.setColour(col::border);
    g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    AttributedString s;
    s.setJustification(Justification::centredLeft);
    s.append(text, Font(font(13.f)), col::text);
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths(s, static_cast<float>(w) - 20.f);
    tl.draw(g, r.reduced(10.f, 7.f));
}

Rectangle<int> LookAndFeel::getTooltipBounds(const String& text, Point<int> pos, Rectangle<int> parent)
{
    AttributedString s;
    s.append(text, Font(font(13.f)), col::text);
    TextLayout tl;
    tl.createLayoutWithBalancedLineLengths(s, 300.f);
    const int w = static_cast<int>(tl.getWidth() + 24.f), h = static_cast<int>(tl.getHeight() + 16.f);
    return Rectangle<int>(pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                          pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 8, w, h)
        .constrainedWithin(parent);
}

void LookAndFeel::drawCallOutBoxBackground(CallOutBox&, Graphics& g, const Path& path, Image&)
{
    g.setColour(Colours::black.withAlpha(0.35f));
    g.fillPath(path, AffineTransform::translation(0.f, 3.f));
    g.setColour(Colour(0xff171d29));
    g.fillPath(path);
    g.setColour(col::border.brighter(0.1f));
    g.strokePath(path, PathStrokeType(1.f));
}

void LookAndFeel::drawProgressBar(Graphics& g, ProgressBar&, int w, int h, double progress, const String& text)
{
    Rectangle<float> r(0.f, 0.f, static_cast<float>(w), static_cast<float>(h));
    g.setColour(col::field);
    g.fillRoundedRectangle(r, h * 0.5f);
    if (progress >= 0.0 && progress <= 1.0)
    {
        g.setColour(col::lavender);
        g.fillRoundedRectangle(r.withWidth(static_cast<float>(w * progress)), h * 0.5f);
    }
    g.setColour(col::text);
    g.setFont(font(11.f));
    g.drawText(text, r, Justification::centred, false);
}

} // namespace pitchlane::theme
