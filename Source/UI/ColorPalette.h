// ==========================================
// File: ColorPalette.h
// ANATOMY パステル・カラーテーマシステム
// ダーク背景 × パステルアクセント
// ==========================================
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace AnatomyColors
{
    // 背景・基本色（テーマで書き換わる）
    inline juce::Colour bg        { 0xff17141f };
    inline juce::Colour panel     { 0xff201c2b };
    inline juce::Colour panelLine { 0x22ffffff };
    inline juce::Colour grid      { 0x14ffffff };
    inline juce::Colour text      { 0xffe9e3f2 };
    inline juce::Colour textDim   { 0xff8d86a0 };
    inline juce::Colour knobTrack { 0xff2a2536 };

    // パステルパレット（テーマで書き換わる）
    inline juce::Colour mint      { 0xffb5ead7 };
    inline juce::Colour pink      { 0xffffb7c5 };
    inline juce::Colour lavender  { 0xffc7ceea };
    inline juce::Colour peach     { 0xffffdac1 };
    inline juce::Colour babyBlue  { 0xffaed9f7 };
    inline juce::Colour sage      { 0xffe2f0cb };
    inline juce::Colour rose      { 0xffffb7b2 };
    inline juce::Colour lilac     { 0xffe0c3fc };

    // ANATOMY固有のセクションアクセント（テーマ適用時に再計算）
    inline juce::Colour accentFull       { 0xffc7ceea }; // Lavender / BabyBlue
    inline juce::Colour accentTransient  { 0xffb5ead7 }; // Mint / Cyan
    inline juce::Colour accentTonal      { 0xffffb7c5 }; // Pink / Magenta
    inline juce::Colour accentWarning    { 0xffffdac1 }; // Peach / Yellow

    // --- テーマ定義 ---
    struct Theme
    {
        juce::uint32 bg, panel, text, textDim, knobTrack;
        juce::uint32 mint, pink, lavender, peach, babyBlue, sage, rose, lilac;
    };

    inline const std::array<Theme, 10>& themes()
    {
        static const std::array<Theme, 10> t = { {
            // 0 Midnight (default: ANATOMY Dark Slate & Pastel)
            // mint(Trans)=#5eead4, pink(Tonal)=#f472b6, lavender(Full)=#a78bfa, peach(Layer)=#fb923c
            { 0xff17141f, 0xff201c2b, 0xffe9e3f2, 0xff8d86a0, 0xff2a2536,
              0xff5eead4, 0xfff472b6, 0xffa78bfa, 0xfffb923c, 0xffaed9f7, 0xffe2f0cb, 0xffffb7b2, 0xffe0c3fc },
            // 1 Sakura (warm pink / magenta / peach / green)
            // mint(Trans)=#4ade80, pink(Tonal)=#fb7185, lavender(Full)=#e879f9, peach(Layer)=#fdba74
            { 0xff1e1418, 0xff2b1c22, 0xfff3e6ec, 0xffa2888f, 0xff36272e,
              0xff4ade80, 0xfffb7185, 0xffe879f9, 0xfffdba74, 0xfff5b8cf, 0xfff0dccb, 0xffffb0ad, 0xfff0c3e6 },
            // 2 Ocean (teal / cyan / indigo / gold)
            // mint(Trans)=#2dd4bf, pink(Tonal)=#38bdf8, lavender(Full)=#818cf8, peach(Layer)=#facc15
            { 0xff0f1720, 0xff17222f, 0xffdce9f2, 0xff7f93a2, 0xff203039,
              0xff2dd4bf, 0xff38bdf8, 0xff818cf8, 0xfffacc15, 0xff8fc9f7, 0xffbfe8e0, 0xff9fd2d8, 0xffaed0f0 },
            // 3 Forest (emerald / lime / cyan / amber)
            // mint(Trans)=#34d399, pink(Tonal)=#a3e635, lavender(Full)=#22d3ee, peach(Layer)=#f59e0b
            { 0xff121a14, 0xff1b2620, 0xffe4f0e6, 0xff85988c, 0xff26332c,
              0xff34d399, 0xffa3e635, 0xff22d3ee, 0xfff59e0b, 0xffaee0c9, 0xffd2f0b8, 0xffb8e0a0, 0xffcde0c3 },
            // 4 Sunset (crimson / orange / violet / yellow)
            // mint(Trans)=#f43f5e, pink(Tonal)=#fb923c, lavender(Full)=#c084fc, peach(Layer)=#fde047
            { 0xff1f1512, 0xff2b1e18, 0xfff3e9e0, 0xffa08f82, 0xff362a24,
              0xfff43f5e, 0xfffb923c, 0xffc084fc, 0xfffde047, 0xfff7c98f, 0xfff0d9b8, 0xffffb0a0, 0xfff0c3c8 },
            // 5 Mono (grayscale: 濃さ・明度で明確に差別化: Trans=100%白, Full=87%薄灰, Tonal=53%中灰, Layer=27%濃灰)
            { 0xff141414, 0xff1e1e1e, 0xffe6e6e6, 0xff8c8c8c, 0xff2b2b2b,
              0xffffffff, 0xff888888, 0xffdddddd, 0xff444444, 0xffbbbbbb, 0xffaaaaaa, 0xff666666, 0xff999999 },
            // 6 Neon (cyberpunk: aqua / hotpink / purple / orange)
            // mint(Trans)=#00ffcc, pink(Tonal)=#ff007f, lavender(Full)=#9933ff, peach(Layer)=#ffaa00
            { 0xff0d0d14, 0xff16161f, 0xffe9eeff, 0xff7d84a0, 0xff20202e,
              0xff00ffcc, 0xffff007f, 0xff9933ff, 0xffffaa00, 0xff4dd0ff, 0xffb0ff4d, 0xffff6b6b, 0xffc44dff },
            // 7 Vaporwave (cyan / hot pink / purple / mint)
            // mint(Trans)=#01cdfe, pink(Tonal)=#ff71ce, lavender(Full)=#b967ff, peach(Layer)=#05ffa1
            { 0xff15111f, 0xff1f1830, 0xffefe6f6, 0xff938aa8, 0xff2b2240,
              0xff01cdfe, 0xffff71ce, 0xffb967ff, 0xff05ffa1, 0xff8fd0ff, 0xffd0b0ff, 0xffff9fd8, 0xffc79fff },
            // 8 Amber (amber / ochre-red / violet / sage)
            // mint(Trans)=#f59e0b, pink(Tonal)=#ef4444, lavender(Full)=#8b5cf6, peach(Layer)=#10b981
            { 0xff1a1610, 0xff261f16, 0xfff2ebdd, 0xff9c9078, 0xff332a1e,
              0xfff59e0b, 0xffef4444, 0xff8b5cf6, 0xff10b981, 0xffd0c090, 0xffe0d8a0, 0xffe0b090, 0xffd8c0a0 },
            // 9 Arctic (glacier blue / aurora pink / ice cyan / purple)
            // mint(Trans)=#38bdf8, pink(Tonal)=#f472b6, lavender(Full)=#a5f3fc, peach(Layer)=#c084fc
            { 0xff14181c, 0xff1e242a, 0xffe8f0f5, 0xff8496a0, 0xff28313a,
              0xff38bdf8, 0xfff472b6, 0xffa5f3fc, 0xffc084fc, 0xffb8dcf0, 0xffd8ecdc, 0xffc8dce0, 0xffcdd8ec }
        } };
        return t;
    }

    inline juce::StringArray getThemeNames()
    {
        return { "Midnight", "Sakura", "Ocean", "Forest", "Sunset",
                 "Mono", "Neon", "Vaporwave", "Amber", "Arctic" };
    }

    inline void setTheme(int idx) noexcept
    {
        const auto& t = themes()[(size_t)juce::jlimit(0, 9, idx)];
        bg        = juce::Colour(t.bg);
        panel     = juce::Colour(t.panel);
        text      = juce::Colour(t.text);
        textDim   = juce::Colour(t.textDim);
        knobTrack = juce::Colour(t.knobTrack);
        mint      = juce::Colour(t.mint);
        pink      = juce::Colour(t.pink);
        lavender  = juce::Colour(t.lavender);
        peach     = juce::Colour(t.peach);
        babyBlue  = juce::Colour(t.babyBlue);
        sage      = juce::Colour(t.sage);
        rose      = juce::Colour(t.rose);
        lilac     = juce::Colour(t.lilac);
        // panelLine/grid は text に連動（薄いオーバーレイ）
        panelLine = text.withAlpha(0.13f);
        grid      = text.withAlpha(0.08f);
        // アクセント再計算
        accentTransient = mint;
        accentTonal     = pink;
        accentFull      = lavender;
        accentWarning   = peach;
    }
}
