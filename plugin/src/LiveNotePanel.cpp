#include "LiveNotePanel.h"

#include "Theme.h"
#include "UiModel.h"

namespace pitchlane {

using namespace juce;
namespace col = theme::col;

void LiveNotePanel::setReadout(const Readout& r, OctaveConvention conv, float tolerance)
{
    // Light display-only easing so the meter and confidence bar don't flicker at 60 Hz.
    shownConfidence_ += 0.25f * (jlimit(0.f, 1.f, r.confidence) - shownConfidence_);
    if (r.hasPitch) shownCents_ += 0.5f * (r.cents - shownCents_);
    r_ = r;
    conv_ = conv;
    tolerance_ = tolerance;
    repaint();
}

void LiveNotePanel::paint(Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(col::panel);
    g.fillRect(b);
    g.setColour(col::divider);
    g.drawVerticalLine(getWidth() - 1, 0.f, b.getHeight());

    b = b.reduced(18.f, 14.f);
    const float scale = jlimit(0.8f, 1.2f, b.getWidth() / 124.f);

    // ---- LIVE NOTE --------------------------------------------------------------------
    g.setColour(col::textDim);
    g.setFont(Font(theme::font(11.5f, theme::Weight::SemiBold)).withExtraKerningFactor(0.1f));
    g.drawText("LIVE NOTE", b.removeFromTop(18.f), Justification::centredLeft, false);
    b.removeFromTop(4.f);

    const bool off = r_.hasPitch && r_.hasTarget && r_.status != TuningStatus::InTune;
    const String note = r_.hasPitch ? String(noteName(r_.hasTarget ? r_.target : nearestNote(r_.midi), conv_))
                                    : String(CharPointer_UTF8("\xe2\x80\x94"));
    g.setColour(r_.hasPitch ? col::cyanNote : col::textFaint);
    g.setFont(theme::font(34.f * scale, theme::Weight::Medium));
    g.drawText(note, b.removeFromTop(40.f * scale), Justification::centredLeft, false);

    String sub;
    Colour subCol = col::textDim;
    if (!r_.hasPitch) sub = "Sing...";
    else
    {
        sub = ui::centsText(r_.cents);
        subCol = !r_.hasTarget ? col::textDim : (off ? col::off : col::cyan);
    }
    g.setColour(subCol);
    g.setFont(theme::font(17.f * scale, theme::Weight::Medium));
    g.drawText(sub, b.removeFromTop(24.f * scale), Justification::centredLeft, false);
    if (r_.hasPitch && !r_.hasTarget)
    {
        g.setColour(col::textFaint);
        g.setFont(theme::font(11.f));
        g.drawText("no reference note here", b.removeFromTop(14.f), Justification::centredLeft, false);
    }

    // ---- confidence (bottom) --------------------------------------------------------------
    auto conf = b.removeFromBottom(38.f);
    g.setColour(col::text.withAlpha(0.85f));
    g.setFont(theme::font(12.5f));
    g.drawText("Confidence", conf.removeFromTop(18.f), Justification::centredLeft, false);
    auto barRow = conf.withSizeKeepingCentre(conf.getWidth(), 8.f);
    auto pct = barRow.removeFromRight(40.f);
    barRow.removeFromRight(6.f);
    g.setColour(col::field);
    g.fillRoundedRectangle(barRow, 4.f);
    g.setColour(col::border);
    g.drawRoundedRectangle(barRow, 4.f, 1.f);
    const float c = r_.hasPitch || shownConfidence_ > 0.02f ? shownConfidence_ : 0.f;
    if (c > 0.f)
    {
        auto fill = barRow.withWidth(jmax(8.f, barRow.getWidth() * c));
        g.setColour(col::cyan.withAlpha(0.18f));
        g.fillRoundedRectangle(fill.expanded(0.f, 2.f), 5.f);
        g.setColour(col::cyan);
        g.fillRoundedRectangle(fill, 4.f);
    }
    g.setColour(col::text.withAlpha(0.85f));
    g.setFont(theme::font(12.f));
    g.drawText(String(roundToInt(c * 100.f)) + "%", pct, Justification::centredRight, false);
    b.removeFromBottom(16.f);

    // ---- vertical meter -------------------------------------------------------------------------
    b.removeFromTop(14.f);
    auto meterArea = b;
    const float trackW = 14.f;
    Rectangle<float> track(meterArea.getX() + 6.f, meterArea.getY() + 6.f, trackW, meterArea.getHeight() - 12.f);
    if (track.getHeight() < 40.f) return;
    g.setColour(col::field);
    g.fillRoundedRectangle(track.expanded(3.f), 7.f);
    g.setColour(col::border);
    g.drawRoundedRectangle(track.expanded(3.f), 7.f, 1.f);
    // ticks every 10 cents
    for (int ct = -50; ct <= 50; ct += 10)
    {
        const float y = track.getBottom() - ui::meterFraction(ct) * track.getHeight();
        g.setColour(Colours::white.withAlpha(ct == 0 ? 0.35f : 0.12f));
        g.drawHorizontalLine(roundToInt(y), track.getX() + 3.f, track.getRight() - 3.f);
    }
    const float y0 = track.getBottom() - 0.5f * track.getHeight();
    // in-tune zone
    const float tolFrac = jlimit(0.f, 0.5f, tolerance_ / 100.f);
    g.setColour(col::cyan.withAlpha(0.10f));
    g.fillRect(track.getX(), y0 - tolFrac * track.getHeight(), trackW, 2.f * tolFrac * track.getHeight());

    if (r_.hasPitch)
    {
        // Fill from the centre to the current deviation, coloured by the tuning status.
        const float yv = track.getBottom() - ui::meterFraction(shownCents_) * track.getHeight();
        const bool offNow = r_.hasTarget && std::abs(shownCents_) > tolerance_;
        const Colour fillCol = !r_.hasTarget ? col::textDim : offNow ? col::off : col::cyan;
        auto fill = Rectangle<float>::leftTopRightBottom(track.getX() + 2.f, jmin(yv, y0), track.getRight() - 2.f, jmax(yv, y0));
        g.setColour(fillCol.withAlpha(0.25f));
        g.fillRoundedRectangle(fill.expanded(2.f, 1.f), 3.f);
        g.setColour(fillCol);
        g.fillRoundedRectangle(fill, 2.f);
        // in-tune band edge marks
        g.setColour(col::cyan.withAlpha(0.8f));
        g.fillRect(track.getX() + 2.f, y0 - tolFrac * track.getHeight() - 1.f, trackW - 4.f, 2.f);
        g.fillRect(track.getX() + 2.f, y0 + tolFrac * track.getHeight() - 1.f, trackW - 4.f, 2.f);
        // value marker
        g.setColour(Colours::white);
        g.fillRoundedRectangle(track.getX() + 1.f, yv - 1.5f, trackW - 2.f, 3.f, 1.5f);
    }
    // centre marker
    g.setColour(Colours::white.withAlpha(0.9f));
    Path centre;
    centre.addTriangle(track.getRight() + 3.f, y0, track.getRight() + 8.f, y0 - 4.f, track.getRight() + 8.f, y0 + 4.f);
    g.fillPath(centre);

    // labels
    const float lx = track.getRight() + 16.f;
    auto label = [&](const String& s, float frac, bool strong) {
        const float y = track.getBottom() - frac * track.getHeight();
        g.setColour(strong ? col::text.withAlpha(0.85f) : col::textDim);
        g.setFont(Font(theme::font(11.f, strong ? theme::Weight::Medium : theme::Weight::Regular)).withExtraKerningFactor(0.04f));
        g.drawText(s, Rectangle<float>(lx, y - 8.f, meterArea.getRight() - lx, 16.f), Justification::centredLeft, false);
    };
    label("+50", 1.f, false);
    label("SHARP", 0.8f, false);
    label("IN TUNE", 0.5f, true);
    label("FLAT", 0.2f, false);
    label(String(CharPointer_UTF8("\xe2\x88\x92")) + "50", 0.f, false);
}

} // namespace pitchlane
