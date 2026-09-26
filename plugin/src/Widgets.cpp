#include "Widgets.h"

namespace pitchlane {

using namespace juce;
namespace col = theme::col;

// ============================================================================ InfoDot

void InfoDot::paint(Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(1.f);
    const float d = jmin(r.getWidth(), r.getHeight());
    const auto c = Rectangle<float>(d, d).withCentre(r.getCentre());
    const auto colour = isMouseOver() ? col::text : col::textDim;
    g.setColour(colour);
    g.drawEllipse(c.reduced(0.5f), 1.f);
    g.fillEllipse(c.getCentreX() - 0.9f, c.getY() + d * 0.22f, 1.8f, 1.8f);
    g.fillRoundedRectangle(c.getCentreX() - 0.8f, c.getY() + d * 0.42f, 1.6f, d * 0.36f, 0.8f);
}

// ============================================================================ ValueField

ValueField::ValueField(RangedAudioParameter* param, Style style) : param_(param), style_(style)
{
    setWantsKeyboardFocus(false);
    setMouseCursor(MouseCursor::PointingHandCursor);
    if (param_ != nullptr)
        attachment_ = std::make_unique<ParameterAttachment>(*param_, [this](float) { refresh(); }, nullptr);
}

ValueField::~ValueField() = default;

float ValueField::getValue() const
{
    return param_ != nullptr ? param_->convertFrom0to1(param_->getValue()) : 0.f;
}

void ValueField::setValue(float v)
{
    if (param_ == nullptr || readOnly) return;
    const auto& range = param_->getNormalisableRange();
    v = range.snapToLegalValue(jlimit(range.start, range.end, v));
    if (gesture_) attachment_->setValueAsPartOfGesture(v);
    else attachment_->setValueAsCompleteGesture(v);
    refresh();
}

String ValueField::currentText() const
{
    const float v = getValue();
    if (textFor) return textFor(v);
    return param_ != nullptr ? param_->getCurrentValueAsText() : String();
}

void ValueField::refresh()
{
    const auto t = currentText();
    if (t != shownText_)
    {
        shownText_ = t;
        repaint();
    }
}

Rectangle<float> ValueField::leftArrow() const
{
    return style_ == Style::Stepper ? getLocalBounds().toFloat().removeFromLeft(26.f) : Rectangle<float>();
}

Rectangle<float> ValueField::rightArrow() const
{
    return style_ == Style::Plain ? Rectangle<float>() : getLocalBounds().toFloat().removeFromRight(26.f);
}

void ValueField::paint(Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(col::field);
    g.fillRoundedRectangle(r, 7.f);
    g.setColour(isMouseOver(true) && !readOnly ? col::border.brighter(0.25f) : col::border);
    g.drawRoundedRectangle(r, 7.f, 1.f);

    const auto chevronColour = readOnly ? col::textFaint : col::textDim;
    const PathStrokeType stroke(1.5f, PathStrokeType::curved, PathStrokeType::rounded);
    auto text = getLocalBounds().toFloat();
    if (style_ == Style::Stepper)
    {
        g.setColour(pressedArrow_ < 0 ? col::text : chevronColour);
        g.strokePath(theme::icons::chevronLeft(Rectangle<float>(6.f, 10.f).withCentre(leftArrow().getCentre())), stroke);
        text.removeFromLeft(26.f);
    }
    if (style_ != Style::Plain)
    {
        g.setColour(pressedArrow_ > 0 ? col::text : chevronColour);
        const auto c = rightArrow().getCentre();
        if (style_ == Style::ChevronDown)
            g.strokePath(theme::icons::chevronDown(Rectangle<float>(9.f, 6.f).withCentre(c)), stroke);
        else
            g.strokePath(theme::icons::chevronRight(Rectangle<float>(6.f, 10.f).withCentre(c)), stroke);
        text.removeFromRight(26.f);
    }
    if (editor_ != nullptr) return;
    g.setColour(readOnly ? col::text.withAlpha(0.75f) : col::text);
    g.setFont(theme::font(jmin(14.f, getHeight() * 0.42f), theme::Weight::Medium));
    const bool centred = style_ == Style::Stepper;
    g.drawFittedText(currentText(), text.reduced(centred ? 0.f : 12.f, 0.f).toNearestInt(),
                     centred ? Justification::centred : Justification::centredLeft, 1, 0.85f);
}

void ValueField::resized()
{
    if (editor_ != nullptr) editor_->setBounds(getLocalBounds().reduced(style_ == Style::Stepper ? 26 : 6, 4));
}

void ValueField::showMenu()
{
    PopupMenu m;
    buildMenu(m);
    SafePointer<ValueField> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()), [safe](int result) {
        if (safe != nullptr && result != 0 && safe->menuResult) safe->menuResult(result);
    });
}

void ValueField::mouseDown(const MouseEvent& e)
{
    dragging_ = false;
    pressedArrow_ = 0;
    dragStartValue_ = getValue();
    if (readOnly) return;
    const float s = e.mods.isShiftDown() ? fineStep : step;
    if (leftArrow().contains(e.position)) { pressedArrow_ = -1; setValue(getValue() - s); repaint(); return; }
    if (style_ == Style::Stepper && rightArrow().contains(e.position)) { pressedArrow_ = 1; setValue(getValue() + s); repaint(); return; }
}

void ValueField::mouseDrag(const MouseEvent& e)
{
    if (readOnly || pressedArrow_ != 0 || param_ == nullptr) return;
    const int dy = -e.getDistanceFromDragStartY();
    if (!dragging_ && std::abs(dy) < 3) return;
    if (!dragging_)
    {
        dragging_ = true;
        gesture_ = true;
        attachment_->beginGesture();
    }
    const float s = e.mods.isShiftDown() ? fineStep : step;
    setValue(dragStartValue_ + s * std::round(static_cast<float>(dy) / dragPixelsPerStep));
}

void ValueField::mouseUp(const MouseEvent& e)
{
    const bool wasArrow = pressedArrow_ != 0;
    pressedArrow_ = 0;
    if (dragging_)
    {
        dragging_ = false;
        gesture_ = false;
        attachment_->endGesture();
        repaint();
        return;
    }
    repaint();
    if (wasArrow || e.mouseWasDraggedSinceMouseDown() || e.getNumberOfClicks() > 1) return;
    if (buildMenu) showMenu();   // read-only fields may still offer an informational menu
    else if (onClick) onClick();
}

void ValueField::mouseDoubleClick(const MouseEvent& e)
{
    if (readOnly || param_ == nullptr || leftArrow().contains(e.position) || rightArrow().contains(e.position)) return;
    beginTyping();
}

void ValueField::beginTyping()
{
    editor_ = std::make_unique<TextEditor>();
    editor_->setFont(theme::font(13.f));
    editor_->setJustification(Justification::centred);
    editor_->setText(String(getValue(), step < 1.f ? 1 : 0), false);
    editor_->selectAll();
    addAndMakeVisible(*editor_);
    resized();
    editor_->grabKeyboardFocus();
    SafePointer<ValueField> safe(this);
    auto commit = [safe](bool apply) {
        if (safe == nullptr || safe->editor_ == nullptr) return;
        const auto txt = safe->editor_->getText();
        if (apply && txt.trim().isNotEmpty())
        {
            float v;
            if (safe->parse) v = safe->parse(txt);
            else v = safe->param_->convertFrom0to1(safe->param_->getValueForText(txt));
            safe->setValue(v);
        }
        MessageManager::callAsync([safe] { if (safe != nullptr) { safe->editor_.reset(); safe->repaint(); } });
    };
    editor_->onReturnKey = [commit] { commit(true); };
    editor_->onEscapeKey = [commit] { commit(false); };
    editor_->onFocusLost = [commit] { commit(true); };
    repaint();
}

void ValueField::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w)
{
    if (readOnly || param_ == nullptr) return;
    const float d = std::abs(w.deltaY) >= std::abs(w.deltaX) ? w.deltaY : -w.deltaX;
    if (std::abs(d) < 1e-9f) return;
    const float s = e.mods.isShiftDown() ? fineStep : step;
    setValue(getValue() + (d > 0 ? s : -s) * (w.isReversed ? -1.f : 1.f));
}

// ============================================================================ SegmentedControl

SegmentedControl::SegmentedControl(StringArray labels, RangedAudioParameter* param)
    : labels_(std::move(labels)), param_(param)
{
    if (param_ != nullptr)
        attachment_ = std::make_unique<ParameterAttachment>(*param_, [this](float) { repaint(); }, nullptr);
}

SegmentedControl::~SegmentedControl() = default;

int SegmentedControl::current() const
{
    if (param_ != nullptr) return roundToInt(param_->convertFrom0to1(param_->getValue()));
    return getIndex ? getIndex() : 0;
}

int SegmentedControl::segmentAt(float x) const
{
    if (labels_.isEmpty()) return -1;
    return jlimit(0, labels_.size() - 1, static_cast<int>(x / (static_cast<float>(getWidth()) / labels_.size())));
}

void SegmentedControl::paint(Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(col::field);
    g.fillRoundedRectangle(r, 7.f);
    g.setColour(col::border);
    g.drawRoundedRectangle(r, 7.f, 1.f);
    const int n = labels_.size();
    if (n == 0) return;
    const float w = r.getWidth() / n;
    const int sel = current();
    for (int i = 0; i < n; ++i)
    {
        auto seg = Rectangle<float>(r.getX() + i * w, r.getY(), w, r.getHeight());
        if (i == sel)
        {
            auto s = seg.reduced(1.5f);
            g.setColour(col::accentFill);
            g.fillRoundedRectangle(s, 6.f);
            g.setColour(col::lavender.withAlpha(0.9f));
            g.drawRoundedRectangle(s, 6.f, 1.2f);
        }
        else if (i > 0 && i - 1 != sel)
        {
            g.setColour(col::divider);
            g.drawVerticalLine(roundToInt(seg.getX()), seg.getY() + 8.f, seg.getBottom() - 8.f);
        }
        g.setColour(i == sel ? col::lavenderHi : (i == hover_ ? col::text : col::textDim));
        g.setFont(theme::font(jmin(13.f, getHeight() * 0.4f), i == sel ? theme::Weight::Medium : theme::Weight::Regular));
        g.drawFittedText(labels_[i], seg.reduced(4.f, 0.f).toNearestInt(), Justification::centred, 1, 0.8f);
    }
}

void SegmentedControl::mouseDown(const MouseEvent& e)
{
    const int i = segmentAt(e.position.x);
    if (i < 0) return;
    if (param_ != nullptr) attachment_->setValueAsCompleteGesture(static_cast<float>(i));
    else if (setIndex) setIndex(i);
    repaint();
}

void SegmentedControl::mouseMove(const MouseEvent& e)
{
    const int i = segmentAt(e.position.x);
    if (i != hover_) { hover_ = i; repaint(); }
}

// ============================================================================ IconButton

IconButton::IconButton(const String& name, PathFn fn, bool filled, float iconSize)
    : Button(name), fn_(std::move(fn)), filled_(filled), size_(iconSize)
{
    setTooltip(name);
    setWantsKeyboardFocus(false);
    setMouseCursor(MouseCursor::PointingHandCursor);
}

void IconButton::paintButton(Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (drawFrame)
    {
        g.setColour(down ? col::accentFill : col::field);
        g.fillRoundedRectangle(r.reduced(0.5f), 6.f);
        g.setColour(over ? col::border.brighter(0.25f) : col::border);
        g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
    }
    const auto icon = Rectangle<float>(size_, size_).withCentre(r.getCentre());
    auto c = getToggleState() ? col::lavenderHi : (over ? col::text : col::textDim);
    if (!isEnabled()) c = col::textFaint.withAlpha(0.5f);
    g.setColour(c);
    const auto p = fn_(icon);
    if (filled_) g.fillPath(p);
    else g.strokePath(p, PathStrokeType(1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

// ============================================================================ SyncButton

void SyncButton::paintButton(Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    g.setColour(on ? col::accentFill.withAlpha(down ? 1.f : 0.85f) : col::field);
    g.fillRoundedRectangle(r, 7.f);
    g.setColour(on ? col::lavender.withAlpha(over ? 1.f : 0.8f) : (over ? col::border.brighter(0.3f) : col::border));
    g.drawRoundedRectangle(r, 7.f, on ? 1.3f : 1.f);
    const auto textCol = on ? col::lavenderHi : col::textDim;
    const float iconS = jmin(15.f, r.getHeight() * 0.45f);
    const auto f = Font(theme::font(jmin(12.5f, r.getHeight() * 0.38f), theme::Weight::SemiBold)).withExtraKerningFactor(0.06f);
    const String label = "LOGIC SYNC";
    GlyphArrangement ga;
    ga.addLineOfText(f, label, 0.f, 0.f);
    const float tw = ga.getBoundingBox(0, -1, true).getWidth();
    const float total = iconS + 8.f + tw;
    float x = r.getCentreX() - total * 0.5f;
    g.setColour(textCol);
    g.strokePath(theme::icons::link(Rectangle<float>(x, r.getCentreY() - iconS * 0.5f, iconS, iconS)),
                 PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    x += iconS + 8.f;
    g.setFont(f);
    g.drawText(label, Rectangle<float>(x, r.getY(), tw + 4.f, r.getHeight()), Justification::centredLeft, false);
}

// ============================================================================ StatusDot

void StatusDot::setState(State s, const String& tip)
{
    if (s != state_) { state_ = s; repaint(); }
    if (tip != getTooltip()) setTooltip(tip);
}

void StatusDot::paint(Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float d = jmin(r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    const Colour led = state_ == State::Playing ? col::green
                     : state_ == State::Stopped ? col::amber
                     : state_ == State::NoInfo  ? col::textDim
                                                : col::textFaint;
    const float ringD = d * 0.8f, ledD = d * 0.38f;
    if (state_ == State::Stopped || state_ == State::Playing)
    {
        const float glow = state_ == State::Playing ? 1.f : 0.6f;
        for (int i = 3; i >= 1; --i)
        {
            const float gd = ledD + i * d * 0.12f;
            g.setColour(led.withAlpha(0.12f * glow));
            g.fillEllipse(c.x - gd * 0.5f, c.y - gd * 0.5f, gd, gd);
        }
    }
    g.setColour(col::field);
    g.fillEllipse(c.x - ringD * 0.5f, c.y - ringD * 0.5f, ringD, ringD);
    g.setColour(led.withAlpha(0.55f));
    g.drawEllipse(c.x - ringD * 0.5f, c.y - ringD * 0.5f, ringD, ringD, 1.2f);
    g.setColour(led);
    if (state_ == State::Off)
        g.drawEllipse(c.x - ledD * 0.5f, c.y - ledD * 0.5f, ledD, ledD, 1.2f);   // hollow: sync off
    else
        g.fillEllipse(c.x - ledD * 0.5f, c.y - ledD * 0.5f, ledD, ledD);
}

// ============================================================================ ReferenceField

void ReferenceField::setFile(const String& name, Colour colour)
{
    if (name != name_ || colour != colour_)
    {
        name_ = name;
        colour_ = colour;
        repaint();
    }
}

void ReferenceField::paint(Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(col::field);
    g.fillRoundedRectangle(r, 7.f);
    g.setColour(isMouseOver(true) ? col::border.brighter(0.25f) : col::border);
    g.drawRoundedRectangle(r, 7.f, 1.f);
    auto inner = r.reduced(12.f, 0.f);
    auto folder = inner.removeFromRight(18.f);
    g.setColour(isMouseOver(true) ? col::text : col::textDim);
    g.fillPath(theme::icons::folder(Rectangle<float>(15.f, 12.f).withCentre(folder.getCentre())));
    inner.removeFromRight(8.f);
    const auto labelFont = theme::font(13.f);
    g.setFont(labelFont);
    g.setColour(col::textDim);
    const String label = "Reference:";
    const float lw = GlyphArrangement::getStringWidth(Font(labelFont), label) + 8.f;
    g.drawText(label, inner.removeFromLeft(lw), Justification::centredLeft, false);
    g.setColour(colour_);
    g.setFont(theme::font(13.f, theme::Weight::Medium));
    g.drawText(name_, inner, Justification::centredLeft, true);
}

void ReferenceField::mouseUp(const MouseEvent& e)
{
    if (!e.mouseWasDraggedSinceMouseDown() && getLocalBounds().contains(e.getPosition()) && onClick) onClick();
}

// ============================================================================ Caption

Caption::Caption(const String& text, const String& tip) : text_(text), info_(tip)
{
    addAndMakeVisible(info_);
}

int Caption::preferredWidth() const
{
    return roundToInt(GlyphArrangement::getStringWidth(Font(theme::font(13.f)), text_)) + 24;
}

void Caption::resized()
{
    const int tw = roundToInt(GlyphArrangement::getStringWidth(Font(theme::font(13.f)), text_));
    info_.setBounds(jmin(getWidth() - 14, tw + 8), (getHeight() - 14) / 2, 14, 14);
}

void Caption::paint(Graphics& g)
{
    g.setColour(col::text.withAlpha(0.85f));
    g.setFont(theme::font(13.f));
    g.drawText(text_, getLocalBounds(), Justification::centredLeft, false);
}

} // namespace pitchlane
