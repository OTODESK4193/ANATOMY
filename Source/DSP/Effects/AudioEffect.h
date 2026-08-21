#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

enum class TargetRoute
{
    Transient,
    Tonal,
    FullMix,
    Layer
};

/**
 * AudioEffect
 * 縦幅半分のUIスリム化、および出現・格納トポロジーを完全支援する共通抽象基底規格。
 */
class AudioEffect
{
public:
    virtual ~AudioEffect() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() noexcept = 0;
    virtual void process(juce::AudioBuffer<float>& buffer) noexcept = 0;

    virtual juce::String getName() const = 0;
    virtual TargetRoute getTargetRoute() const noexcept = 0;
    virtual void setTargetRoute(TargetRoute newRoute) noexcept = 0;

    // 💥【新設】縦幅半分へのスリム化に伴うバイパス（On/Off）制御規格
    virtual bool isActive() const noexcept = 0;
    virtual void setActive(bool shouldBeActive) noexcept = 0;

    // 💥【新設】全エフェクト共通 Dry/Wet 制御規格
    virtual void setMix(float newMix) noexcept = 0;
    virtual float getMix() const noexcept = 0;

    // 💥【新設】下段ParameterDockPanelと汎用バインドするためのインデックス式パラメータ設定
    virtual void setIndexedParameter(int index, float value) noexcept = 0;
    virtual float getIndexedParameter(int index) const noexcept = 0;
};