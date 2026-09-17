#pragma once

#include <juce_dsp/juce_dsp.h>
#include "ButterLookAndFeel.h"
#include "../PluginProcessor.h"

// Interactive EQ curve with a live pre/post spectrum, Pro-Q style:
// drag a dot to move a band, scroll to change Q, double-click a dot to toggle it,
// double-click empty space to drop in a new band, right-click a dot for its shape.
class EqDisplay : public juce::Component
{
public:
    static inline const juce::Colour bandColours[bf::ParametricEq::numBands] {
        ButterLookAndFeel::tomato, ButterLookAndFeel::tangerine, ButterLookAndFeel::mint,
        ButterLookAndFeel::sky, ButterLookAndFeel::lilac, ButterLookAndFeel::bubblegum
    };

    std::function<void (int)> onSelect;

    explicit EqDisplay (ButterfaderAudioProcessor& p)
        : processor (p), fft (fftOrder), window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        for (auto* s : { &pre, &post })
        {
            s->ring.assign ((size_t) fftSize, 0.0f);
            s->smoothed.fill (-100.0f);
        }
        scratch.assign ((size_t) fftSize * 2, 0.0f);
        pullBuffer.assign (1 << 15, 0.0f);
    }

    int getSelected() const { return selected; }

    void setSelected (int band)
    {
        selected = band;
        if (onSelect) onSelect (band);
        repaint();
    }

    // Called from the editor's timer.
    void refresh()
    {
        updateSpectrum (pre, processor.preTap);
        updateSpectrum (post, processor.postTap);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using LAF = ButterLookAndFeel;
        const auto r = getLocalBounds().toFloat();
        g.setColour (LAF::ink);
        g.fillRoundedRectangle (r, 10.0f);

        // Grid
        g.setFont (LAF::chunky (10.0f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xForFreq (f);
            g.setColour (LAF::white.withAlpha (0.08f));
            g.drawVerticalLine ((int) x, 6.0f, r.getBottom() - 6.0f);
            g.setColour (LAF::white.withAlpha (0.35f));
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ x, r.getBottom() - 11.0f }), juce::Justification::centred);
        }
        for (float db = -18.0f; db <= 18.0f; db += 6.0f)
        {
            const float y = yForGain (db);
            g.setColour (LAF::white.withAlpha (db == 0.0f ? 0.2f : 0.07f));
            g.drawHorizontalLine ((int) y, 6.0f, r.getRight() - 6.0f);
            g.setColour (LAF::white.withAlpha (0.35f));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (30.0f, 12.0f).withCentre ({ r.getRight() - 20.0f, y - 7.0f }),
                        juce::Justification::centred);
        }

        // Spectra
        drawSpectrum (g, pre, false);
        drawSpectrum (g, post, true);

        // Curves
        const double sr = processor.getCurrentSampleRate();
        const bool eqOn = processor.apvts.getRawParameterValue ("eqOn")->load() > 0.5f;
        std::array<bf::EqBand, bf::ParametricEq::numBands> bands;
        for (int b = 0; b < bf::ParametricEq::numBands; ++b)
            bands[(size_t) b] = processor.getBandSettings (b);

        if (juce::isPositiveAndBelow (selected, bf::ParametricEq::numBands) && bands[(size_t) selected].on)
        {
            juce::Path fill;
            fill.startNewSubPath (0.0f, yForGain (0.0f));
            for (float x = 0.0f; x <= r.getWidth(); x += 2.0f)
                fill.lineTo (x, yForGain ((float) bf::ParametricEq::bandMagnitudeDb (bands[(size_t) selected], freqForX (x), sr)));
            fill.lineTo (r.getWidth(), yForGain (0.0f));
            fill.closeSubPath();
            g.setColour (bandColours[selected].withAlpha (0.25f));
            g.fillPath (fill);
        }

        juce::Path curve;
        for (float x = 0.0f; x <= r.getWidth(); x += 2.0f)
        {
            double total = 0.0;
            if (eqOn)
                for (auto& band : bands)
                    total += bf::ParametricEq::bandMagnitudeDb (band, freqForX (x), sr);
            const float y = yForGain ((float) total);
            if (x == 0.0f) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (LAF::butter.withAlpha (eqOn ? 1.0f : 0.3f));
        g.strokePath (curve, { 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

        // Nodes
        for (int b = 0; b < bf::ParametricEq::numBands; ++b)
        {
            const auto c = nodePosition (bands[(size_t) b]);
            const float size = b == selected ? 22.0f : 18.0f;
            const auto dot = juce::Rectangle<float> (size, size).withCentre (c);
            if (bands[(size_t) b].on)
            {
                g.setColour (juce::Colours::black);
                g.fillEllipse (dot.translated (2.0f, 2.0f));
                g.setColour (bandColours[b]);
                g.fillEllipse (dot);
                g.setColour (LAF::ink);
                g.drawEllipse (dot, 2.0f);
            }
            else
            {
                g.setColour (bandColours[b].withAlpha (0.45f));
                g.drawEllipse (dot, 2.0f);
            }
            g.setColour (bands[(size_t) b].on ? LAF::ink : bandColours[b].withAlpha (0.6f));
            g.setFont (LAF::chunky (11.0f));
            g.drawText (juce::String (b + 1), dot, juce::Justification::centred);
        }

        if (hoverText.isNotEmpty())
        {
            g.setFont (LAF::chunky (12.0f));
            g.setColour (LAF::butter);
            g.drawText (hoverText, getLocalBounds().reduced (12, 8).removeFromTop (16), juce::Justification::topLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int hit = hitTestNode (e.position);
        const auto f = freqForX (e.position.x);
        hoverText = hit >= 0 ? "band " + juce::String (hit + 1) + " - drag me, scroll for width"
                             : (f >= 1000.0 ? juce::String (f / 1000.0, 2) + " kHz" : juce::String ((int) f) + " Hz")
                                   + "   " + juce::String (gainForY (e.position.y), 1) + " dB";
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override { hoverText = {}; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = hitTestNode (e.position);
        if (dragging < 0)
            return;
        setSelected (dragging);

        if (e.mods.isPopupMenu())
        {
            showBandMenu (dragging);
            dragging = -1;
            return;
        }
        if (! processor.getBandSettings (dragging).on)
            setParam (dragging, "On", 1.0f);
        for (auto* what : { "Freq", "Gain" })
            param (dragging, what)->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;
        setParam (dragging, "Freq", (float) juce::jlimit (20.0, 20000.0, freqForX (e.position.x)));
        if (hasGain (processor.getBandSettings (dragging).type) && ! e.mods.isShiftDown())
            setParam (dragging, "Gain", juce::jlimit (-24.0f, 24.0f, gainForY (e.position.y)));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging >= 0)
            for (auto* what : { "Freq", "Gain" })
                param (dragging, what)->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int hit = hitTestNode (e.position);
        if (hit >= 0)
        {
            setParam (hit, "On", processor.getBandSettings (hit).on ? 0.0f : 1.0f);
            return;
        }
        for (int b = 0; b < bf::ParametricEq::numBands; ++b)
        {
            if (! processor.getBandSettings (b).on)
            {
                setParam (b, "Type", 0.0f);
                setParam (b, "Freq", (float) freqForX (e.position.x));
                setParam (b, "Gain", gainForY (e.position.y));
                setParam (b, "Q", 1.0f);
                setParam (b, "On", 1.0f);
                setSelected (b);
                return;
            }
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        int band = hitTestNode (e.position);
        if (band < 0) band = selected;
        if (band < 0) return;
        const float q = processor.getBandSettings (band).q;
        setParam (band, "Q", juce::jlimit (0.1f, 18.0f, q * std::pow (1.25f, wheel.deltaY * 4.0f)));
    }

private:
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder, numColumns = 256;

    struct Spectrum
    {
        std::vector<float> ring;
        int writePos = 0;
        std::array<float, numColumns> smoothed;
    };

    static bool hasGain (bf::FilterType t)
    {
        return t == bf::FilterType::bell || t == bf::FilterType::lowShelf || t == bf::FilterType::highShelf;
    }

    juce::RangedAudioParameter* param (int band, const char* what)
    {
        return processor.apvts.getParameter (ButterfaderAudioProcessor::bandId (band, what));
    }

    void setParam (int band, const char* what, float value)
    {
        if (auto* p = param (band, what))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void showBandMenu (int band)
    {
        juce::PopupMenu menu;
        const auto current = (int) processor.getBandSettings (band).type;
        const auto& names = ButterfaderAudioProcessor::filterTypeNames();
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, i == current);
        menu.addSeparator();
        menu.addItem (100, "Turn band off");
        menu.addItem (101, "Reset band gain");
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [this, band] (int result)
                            {
                                if (result >= 1 && result < 100) { setParam (band, "Type", (float) (result - 1)); setParam (band, "On", 1.0f); }
                                else if (result == 100) setParam (band, "On", 0.0f);
                                else if (result == 101) setParam (band, "Gain", 0.0f);
                            });
    }

    int hitTestNode (juce::Point<float> pos) const
    {
        int best = -1;
        float bestDist = 14.0f;
        for (int b = 0; b < bf::ParametricEq::numBands; ++b)
        {
            const float d = nodePosition (processor.getBandSettings (b)).getDistanceFrom (pos);
            if (d < bestDist) { bestDist = d; best = b; }
        }
        return best;
    }

    juce::Point<float> nodePosition (const bf::EqBand& band) const
    {
        const float y = hasGain (band.type) ? yForGain (band.gainDb) : yForGain (0.0f);
        return { xForFreq (band.freq), y };
    }

    float xForFreq (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0) * getWidth()); }
    double freqForX (float x) const { return 20.0 * std::pow (1000.0, x / (double) juce::jmax (1, getWidth())); }
    float yForGain (float db) const { return getHeight() * 0.5f - db / 20.0f * getHeight() * 0.5f; }
    float gainForY (float y) const { return (getHeight() * 0.5f - y) / (getHeight() * 0.5f) * 20.0f; }
    float yForSpectrum (float db) const { return juce::jmap (db, -96.0f, -6.0f, (float) getHeight(), 10.0f); }

    void updateSpectrum (Spectrum& s, AnalyserTap& tap)
    {
        const int got = tap.pull (pullBuffer.data(), (int) pullBuffer.size());
        for (int i = 0; i < got; ++i)
        {
            s.ring[(size_t) s.writePos] = pullBuffer[(size_t) i];
            s.writePos = (s.writePos + 1) % fftSize;
        }

        for (int i = 0; i < fftSize; ++i)
            scratch[(size_t) i] = s.ring[(size_t) ((s.writePos + i) % fftSize)];
        std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);
        window.multiplyWithWindowingTable (scratch.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (scratch.data(), true);

        const double sr = processor.getCurrentSampleRate();
        const float norm = 4.0f / fftSize;
        for (int c = 0; c < numColumns; ++c)
        {
            const double f0 = 20.0 * std::pow (1000.0, c / (double) numColumns);
            const double f1 = 20.0 * std::pow (1000.0, (c + 1) / (double) numColumns);
            int b0 = (int) (f0 / sr * fftSize), b1 = juce::jmax (b0 + 1, (int) (f1 / sr * fftSize));
            b1 = juce::jmin (b1, fftSize / 2);
            float mag = 0.0f;
            for (int b = juce::jmax (1, b0); b < b1; ++b)
                mag = juce::jmax (mag, scratch[(size_t) b]);
            // +4.5 dB/oct tilt so speech reads flat-ish, like most analysers.
            const float db = juce::Decibels::gainToDecibels (mag * norm, -120.0f) + 4.5f * (float) std::log2 (f0 / 1000.0);
            auto& sm = s.smoothed[(size_t) c];
            sm = db > sm ? sm + (db - sm) * 0.6f : sm + (db - sm) * 0.12f;
        }
    }

    void drawSpectrum (juce::Graphics& g, const Spectrum& s, bool filled)
    {
        using LAF = ButterLookAndFeel;
        juce::Path path;
        const float w = (float) getWidth();
        path.startNewSubPath (0.0f, (float) getHeight());
        for (int c = 0; c < numColumns; ++c)
            path.lineTo ((c + 0.5f) / numColumns * w, yForSpectrum (s.smoothed[(size_t) c]));
        path.lineTo (w, (float) getHeight());
        if (filled)
        {
            path.closeSubPath();
            g.setGradientFill (juce::ColourGradient (LAF::butter.withAlpha (0.35f), 0.0f, 0.0f,
                                                     LAF::butter.withAlpha (0.05f), 0.0f, (float) getHeight(), false));
            g.fillPath (path);
        }
        else
        {
            g.setColour (LAF::white.withAlpha (0.25f));
            g.strokePath (path, juce::PathStrokeType (1.2f));
        }
    }

    ButterfaderAudioProcessor& processor;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    Spectrum pre, post;
    std::vector<float> scratch, pullBuffer;
    int selected = 0, dragging = -1;
    juce::String hoverText;
};
