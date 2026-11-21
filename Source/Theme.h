#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Visual language taken from the reference interface sketch: a flat #383838
    panel, outlined rounded boxes, filled grey pads, teardrop rotary dials with a
    bead indicator, and a two-handle rail fader. Only appearance is defined here;
    no geometry or behaviour is changed by this file.
*/
namespace gh::theme
{
inline const juce::Colour back      { 0xff383838 };
inline const juce::Colour ink       { 0xff727272 };
inline const juce::Colour inkDim    { 0xff5a5a5a };
inline const juce::Colour inkBright { 0xffa9a9a9 };
inline const juce::Colour textLight { 0xff9a9a9a };
inline const juce::Colour textDim   { 0xff8a8a8a };
inline const juce::Colour textDark  { 0xff2f2f2f };

inline constexpr float boxCorner  = 4.5f;
inline constexpr float padCorner  = 6.0f;
inline constexpr float boxStroke  = 1.6f;

/** Outlined control: transparent interior with a light stroke. */
inline void outlinedBox (juce::Graphics& g, juce::Rectangle<float> r,
                         float corner = boxCorner, float thickness = boxStroke)
{
    g.setColour (ink);
    g.drawRoundedRectangle (r.reduced (thickness * 0.5f), corner, thickness);
}

/** Filled control (pads, primary buttons). */
inline void filledBox (juce::Graphics& g, juce::Rectangle<float> r, float corner,
                       juce::Colour fill)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);
    g.setColour (inkDim);
    g.drawRoundedRectangle (r.reduced (0.7f), corner, 1.2f);
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

class Look final : public juce::LookAndFeel_V4
{
public:
    /** AUv3 hosts only render the plugin's own view, so popups must be children of it
        rather than separate desktop windows, which would never be shown. */
    void setPopupsStayInsideEditor (bool shouldStayInside) { popupsStayInside = shouldStayInside; }

    juce::Component* getParentComponentForMenuOptions (const juce::PopupMenu::Options& options) override
    {
        if (! popupsStayInside)
            return juce::LookAndFeel_V4::getParentComponentForMenuOptions (options);

        if (auto* target = options.getTargetComponent())
            return target->getTopLevelComponent();

        return nullptr;
    }

    Look()
    {
        setColour (juce::ResizableWindow::backgroundColourId, back);
        setColour (juce::TextButton::buttonColourId, ink);
        setColour (juce::TextButton::buttonOnColourId, inkBright);
        setColour (juce::Label::textColourId, textLight);
        setColour (juce::Slider::thumbColourId, inkBright);
        setColour (juce::Slider::rotarySliderFillColourId, inkBright);
        setColour (juce::Slider::rotarySliderOutlineColourId, inkDim);
        setColour (juce::Slider::textBoxTextColourId, textLight);
        setColour (juce::Slider::textBoxOutlineColourId, inkDim);
        setColour (juce::Slider::textBoxBackgroundColourId, back);
        setColour (juce::PopupMenu::backgroundColourId, back);
        setColour (juce::PopupMenu::textColourId, textLight);
        setColour (juce::PopupMenu::headerTextColourId, textDim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, ink.withAlpha (0.28f));
        setColour (juce::PopupMenu::highlightedTextColourId, inkBright);
        setColour (juce::ComboBox::backgroundColourId, back);
        setColour (juce::ComboBox::textColourId, textLight);
        setColour (juce::ComboBox::outlineColourId, ink);
        setColour (juce::ComboBox::arrowColourId, textLight);
    }

    //==============================================================================
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return font (juce::jlimit (11.0f, 16.0f, (float) buttonHeight * 0.34f));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }

    //==============================================================================
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto on = button.getToggleState();

        if (on)
        {
            g.setColour (ink.withAlpha (highlighted ? 0.30f : 0.22f));
            g.fillRoundedRectangle (r, boxCorner);
        }
        else if (highlighted || down)
        {
            g.setColour (ink.withAlpha (down ? 0.16f : 0.09f));
            g.fillRoundedRectangle (r, boxCorner);
        }

        outlinedBox (g, r);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool down) override
    {
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (button.getToggleState() ? inkBright
                                             : (down ? textDim : textLight));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 0),
                          juce::Justification::centred, 2);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool highlighted, bool down) override
    {
        auto r = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto on = button.getToggleState();

        if (on)
        {
            g.setColour (ink.withAlpha (highlighted ? 0.30f : 0.22f));
            g.fillRoundedRectangle (r, boxCorner);
        }
        else if (highlighted || down)
        {
            g.setColour (ink.withAlpha (down ? 0.16f : 0.09f));
            g.fillRoundedRectangle (r, boxCorner);
        }

        outlinedBox (g, r);

        auto text = r.reduced (10.0f, 0.0f);
        auto value = text.removeFromRight (34.0f);

        g.setFont (font (juce::jlimit (9.0f, 12.0f, r.getHeight() * 0.32f)));
        g.setColour (textDim);
        g.drawFittedText (button.getButtonText(), text.toNearestInt(),
                          juce::Justification::centredLeft, 2);

        g.setColour (on ? inkBright : textDim);
        g.drawText (on ? "ON" : "OFF", value.toNearestInt(),
                    juce::Justification::centredRight, false);
    }

    //==============================================================================
    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        outlinedBox (g, r);

        const auto c = juce::Point<float> (r.getRight() - 16.0f, r.getCentreY());
        juce::Path chevron;
        chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
        chevron.lineTo (c.x, c.y + 2.5f);
        chevron.lineTo (c.x + 4.0f, c.y - 2.0f);
        g.setColour (textLight);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        juce::ignoreUnused (box);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, textLight);
    }

    //==============================================================================
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider& slider) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height)
                        .reduced (3.0f);
        const auto c = area.getCentre();

        // The text box sits below the dial, so bias the body towards the top.
        const auto usable = juce::jmin (area.getWidth(), area.getHeight() * 0.82f);
        const auto arcR = usable * 0.5f;
        const auto bodyR = arcR - 6.0f;
        const auto cy = area.getY() + arcR + 2.0f;
        const auto angle = startAngle + sliderPos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (c.x, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (inkDim);
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        if (sliderPos > 0.0002f)
        {
            juce::Path filled;
            filled.addCentredArc (c.x, cy, arcR, arcR, 0.0f, startAngle, angle, true);
            g.setColour (inkBright);
            g.strokePath (filled, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        }

        // Teardrop body.
        g.setColour (ink);
        g.fillEllipse (c.x - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        juce::Path tail;
        tail.startNewSubPath (c.x - bodyR * 0.40f, cy + bodyR * 0.72f);
        tail.lineTo (c.x + bodyR * 0.40f, cy + bodyR * 0.72f);
        tail.lineTo (c.x, cy + bodyR * 1.62f);
        tail.closeSubPath();
        g.fillPath (tail);

        // Bead on the arc.
        g.setColour (inkBright);
        const auto bx = c.x + arcR * std::cos (angle);
        const auto by = cy + arcR * std::sin (angle);
        g.fillEllipse (bx - 3.6f, by - 3.6f, 7.2f, 7.2f);

        juce::ignoreUnused (slider);
    }

    //==============================================================================
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                    minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const auto midY = (float) y + (float) height * 0.5f;
        const auto left = (float) x + 4.0f;
        const auto right = (float) x + (float) width - 4.0f;

        g.setColour (inkDim);
        g.fillRect (juce::Rectangle<float> (left, midY - 1.0f, right - left, 2.0f));

        g.setColour (ink);
        g.fillRect (juce::Rectangle<float> (juce::jmin (sliderPos, right), midY - 1.0f,
                                            std::abs (right - sliderPos), 2.0f));

        g.setColour (inkBright);
        g.fillRect (juce::Rectangle<float> (sliderPos - 3.0f, midY - 9.0f, 6.0f, 18.0f));

        juce::ignoreUnused (minSliderPos, maxSliderPos, slider);
    }

    //==============================================================================
    void drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (back);
        g.fillRoundedRectangle (r, boxCorner);
        outlinedBox (g, r);
        g.setColour (textLight);
        g.setFont (font (12.0f));
        g.drawFittedText (text, r.reduced (8.0f, 4.0f).toNearestInt(),
                          juce::Justification::centredLeft, 3);
    }

private:
    bool popupsStayInside = false;
};
} // namespace gh::theme
