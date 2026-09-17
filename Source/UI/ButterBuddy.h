#pragma once

#include "ButterLookAndFeel.h"

// The mascot: a stick of butter who rides the fader.
class ButterBuddy
{
public:
    enum class Mood { sleepy, riding, cool, suspicious, sweaty, sniffing, shocked };

    void tick() { phase += 0.08f; }

    // Draws the buddy standing with its feet at `feet`, scaled to height `h`.
    void draw (juce::Graphics& g, juce::Point<float> feet, float h, Mood mood) const
    {
        using LAF = ButterLookAndFeel;
        const float w = h * 0.62f;
        float bob = mood == Mood::sleepy ? std::sin (phase * 0.4f) * h * 0.015f
                                         : std::abs (std::sin (phase * (mood == Mood::sweaty ? 1.6f : 0.8f))) * h * 0.04f;
        const float wobble = mood == Mood::shocked ? std::sin (phase * 6.0f) * 0.06f : 0.0f;

        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (wobble, feet.x, feet.y));

        const auto body = juce::Rectangle<float> (w, h * 0.78f).withBottomY (feet.y - h * 0.06f - bob).withX (feet.x - w * 0.5f);
        const float depth = w * 0.22f;

        // Legs
        g.setColour (LAF::ink);
        g.drawLine (body.getX() + w * 0.3f, body.getBottom() - 2.0f, body.getX() + w * 0.26f, feet.y, 3.5f);
        g.drawLine (body.getRight() - w * 0.3f, body.getBottom() - 2.0f, body.getRight() - w * 0.26f, feet.y, 3.5f);

        // Top face (the 3D bit) and side
        juce::Path top;
        top.startNewSubPath (body.getX(), body.getY());
        top.lineTo (body.getX() + depth, body.getY() - depth * 0.7f);
        top.lineTo (body.getRight() + depth, body.getY() - depth * 0.7f);
        top.lineTo (body.getRight(), body.getY());
        top.closeSubPath();
        juce::Path side;
        side.startNewSubPath (body.getRight(), body.getY());
        side.lineTo (body.getRight() + depth, body.getY() - depth * 0.7f);
        side.lineTo (body.getRight() + depth, body.getBottom() - depth * 0.7f);
        side.lineTo (body.getRight(), body.getBottom());
        side.closeSubPath();

        g.setColour (LAF::ink);
        g.fillRect (body.translated (4.0f, 4.0f));
        g.setColour (LAF::butter.brighter (0.45f));
        g.fillPath (top);
        g.setColour (LAF::butter.darker (0.18f));
        g.fillPath (side);
        g.setColour (LAF::butter);
        g.fillRect (body);

        // Foil wrapper on the lower half
        const auto foil = body.withTrimmedTop (body.getHeight() * 0.62f);
        g.setColour (LAF::white);
        g.fillRect (foil);
        g.setColour (LAF::sky);
        g.fillRect (foil.withTrimmedTop (foil.getHeight() * 0.35f).withHeight (foil.getHeight() * 0.22f));

        g.setColour (LAF::ink);
        g.strokePath (top, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
        g.strokePath (side, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
        g.drawRect (body, 2.5f);
        g.drawLine (foil.getX(), foil.getY(), foil.getRight(), foil.getY(), 2.0f);

        drawFace (g, body.withHeight (body.getHeight() * 0.62f), mood);

        if (mood == Mood::sweaty)
        {
            for (int i = 0; i < 2; ++i)
            {
                const float t = std::fmod (phase * 0.6f + i * 0.5f, 1.0f);
                const auto c = juce::Point<float> (body.getX() - 6.0f - i * 8.0f, body.getY() + 8.0f + t * 22.0f);
                juce::Path drop;
                drop.addEllipse (c.x - 3.5f, c.y, 7.0f, 8.0f);
                drop.addTriangle (c.x - 3.2f, c.y + 2.0f, c.x + 3.2f, c.y + 2.0f, c.x, c.y - 5.0f);
                g.setColour (LAF::sky.withAlpha (1.0f - t));
                g.fillPath (drop);
            }
        }
        else if (mood == Mood::sleepy)
        {
            g.setColour (LAF::ink.withAlpha (0.7f));
            for (int i = 0; i < 3; ++i)
            {
                const float t = std::fmod (phase * 0.15f + i / 3.0f, 1.0f);
                g.setFont (LAF::chunky (10.0f + 8.0f * t));
                g.drawText ("z", juce::Rectangle<float> (40.0f, 20.0f).withCentre ({ body.getRight() + depth + 4.0f + t * 16.0f, body.getY() - 8.0f - t * 30.0f }),
                            juce::Justification::centred);
            }
        }
        else if (mood == Mood::suspicious)
        {
            g.setColour (LAF::tomato);
            g.setFont (LAF::chunky (h * 0.3f));
            g.drawText ("?", juce::Rectangle<float> (h * 0.3f, h * 0.3f).withCentre ({ body.getRight() + depth + 8.0f, body.getY() - 10.0f + bob }),
                        juce::Justification::centred);
        }
        else if (mood == Mood::sniffing)
        {
            g.setColour (LAF::ink.withAlpha (0.6f));
            for (int i = 0; i < 3; ++i)
            {
                const float t = std::fmod (phase * 0.5f + i / 3.0f, 1.0f);
                g.drawEllipse (juce::Rectangle<float> (4.0f + 10.0f * t, 4.0f + 10.0f * t)
                                   .withCentre ({ body.getX() - 8.0f - 18.0f * t, body.getCentreY() - 6.0f }), 2.0f);
            }
        }
    }

private:
    void drawFace (juce::Graphics& g, juce::Rectangle<float> face, Mood mood) const
    {
        using LAF = ButterLookAndFeel;
        const float s = face.getWidth();
        const auto eyeL = juce::Point<float> (face.getX() + s * 0.32f, face.getY() + face.getHeight() * 0.38f);
        const auto eyeR = juce::Point<float> (face.getX() + s * 0.68f, eyeL.y);
        const float eye = s * 0.11f;
        const bool blink = std::fmod (phase, 9.0f) < 0.25f;

        g.setColour (LAF::ink);
        switch (mood)
        {
            case Mood::cool:
            {
                auto lens = [&] (juce::Point<float> c) { g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.3f, s * 0.17f).withCentre (c), 3.0f); };
                lens (eyeL); lens (eyeR);
                g.drawLine (eyeL.x, eyeL.y, eyeR.x, eyeR.y, 2.5f);
                g.setColour (LAF::white.withAlpha (0.8f));
                g.drawLine (eyeL.x - s * 0.1f, eyeL.y - 2.0f, eyeL.x - s * 0.04f, eyeL.y - 4.0f, 2.0f);
                break;
            }
            case Mood::sleepy:
                g.drawLine (eyeL.x - eye, eyeL.y, eyeL.x + eye, eyeL.y, 2.5f);
                g.drawLine (eyeR.x - eye, eyeR.y, eyeR.x + eye, eyeR.y, 2.5f);
                break;
            case Mood::suspicious:
                g.fillRect (juce::Rectangle<float> (eye * 2.2f, eye * 0.7f).withCentre (eyeL));
                g.fillRect (juce::Rectangle<float> (eye * 2.2f, eye * 0.7f).withCentre (eyeR));
                g.drawLine (eyeL.x - eye * 1.4f, eyeL.y - eye * 1.6f, eyeL.x + eye, eyeL.y - eye * 0.9f, 2.5f);
                break;
            case Mood::shocked:
                g.drawEllipse (juce::Rectangle<float> (eye * 2.4f, eye * 2.4f).withCentre (eyeL), 2.5f);
                g.drawEllipse (juce::Rectangle<float> (eye * 2.4f, eye * 2.4f).withCentre (eyeR), 2.5f);
                g.fillEllipse (juce::Rectangle<float> (eye * 0.8f, eye * 0.8f).withCentre (eyeL));
                g.fillEllipse (juce::Rectangle<float> (eye * 0.8f, eye * 0.8f).withCentre (eyeR));
                break;
            default:
                if (blink)
                {
                    g.drawLine (eyeL.x - eye, eyeL.y, eyeL.x + eye, eyeL.y, 2.5f);
                    g.drawLine (eyeR.x - eye, eyeR.y, eyeR.x + eye, eyeR.y, 2.5f);
                }
                else
                {
                    g.fillEllipse (juce::Rectangle<float> (eye * 1.6f, eye * 1.9f).withCentre (eyeL));
                    g.fillEllipse (juce::Rectangle<float> (eye * 1.6f, eye * 1.9f).withCentre (eyeR));
                    g.setColour (LAF::white);
                    g.fillEllipse (juce::Rectangle<float> (eye * 0.5f, eye * 0.5f).withCentre (eyeL.translated (-eye * 0.25f, -eye * 0.35f)));
                    g.fillEllipse (juce::Rectangle<float> (eye * 0.5f, eye * 0.5f).withCentre (eyeR.translated (-eye * 0.25f, -eye * 0.35f)));
                    g.setColour (LAF::ink);
                }
                break;
        }

        // Cheeks
        g.setColour (LAF::bubblegum.withAlpha (0.6f));
        g.fillEllipse (juce::Rectangle<float> (s * 0.16f, s * 0.09f).withCentre (eyeL.translated (-s * 0.06f, s * 0.17f)));
        g.fillEllipse (juce::Rectangle<float> (s * 0.16f, s * 0.09f).withCentre (eyeR.translated (s * 0.06f, s * 0.17f)));

        // Mouth
        g.setColour (LAF::ink);
        const auto m = juce::Point<float> (face.getCentreX(), face.getY() + face.getHeight() * 0.72f);
        juce::Path mouth;
        switch (mood)
        {
            case Mood::riding:
            case Mood::cool:
                mouth.startNewSubPath (m.x - s * 0.16f, m.y - 2.0f);
                mouth.quadraticTo (m.x, m.y + s * 0.14f, m.x + s * 0.16f, m.y - 2.0f);
                g.strokePath (mouth, { 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
                break;
            case Mood::sweaty:
                mouth.startNewSubPath (m.x - s * 0.16f, m.y);
                for (int i = 1; i <= 4; ++i)
                    mouth.lineTo (m.x - s * 0.16f + i * s * 0.08f, m.y + ((i % 2) ? -3.0f : 3.0f));
                g.strokePath (mouth, { 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });
                break;
            case Mood::shocked:
                g.fillEllipse (juce::Rectangle<float> (s * 0.14f, s * 0.18f).withCentre (m));
                break;
            case Mood::sniffing:
                g.fillEllipse (juce::Rectangle<float> (s * 0.08f, s * 0.08f).withCentre (m));
                break;
            case Mood::suspicious:
                g.drawLine (m.x - s * 0.12f, m.y + 2.0f, m.x + s * 0.12f, m.y - 2.0f, 2.5f);
                break;
            case Mood::sleepy:
                g.drawEllipse (juce::Rectangle<float> (s * 0.1f, s * 0.07f).withCentre (m), 2.0f);
                break;
        }
    }

    float phase = 0.0f;
};
