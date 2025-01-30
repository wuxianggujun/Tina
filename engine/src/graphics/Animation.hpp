//
// Created by wuxianggujun on 2025/1/30.
//

#pragma once

#include "base/String.hpp"
#include "math/Rect.hpp"
#include "math/Rect.hpp"
#include "graphics/Texture.hpp"
#include <vector>

namespace Tina
{
    struct AnimationFrame
    {
        Rectf textureRect;
        float duration;

        explicit AnimationFrame(const Rectf& rect, const float time): textureRect(rect), duration(time)
        {
        }
    };


    class Animation
    {
    public:
        Animation();
        explicit Animation(const Texture& texture);

        void addFrame(const Rectf& rect, float duration);
        void addFrame(const AnimationFrame& frame);

        void generateFrame(int frameCount, int frameWidth, int frameHeight, float frameDuration);

        void play();
        void pause();
        void stop();
        void reset();

        void update(float deltaTime);

        void setTexture(const Texture& texture);
        void setLooping(const bool looping) { m_isLooping = looping; }
        void setPlaybackSpeed(const float speed) { m_playbackSpeed = speed; }

        bool isPlaying() const { return m_isPlaying; }
        bool isLooping() const { return m_isLooping; }

        const Rectf& getCurrentFrame() const;
        const Texture& getTexture() const { return m_texture; }

        using FrameCallback = std::function<void(size_t)>;
        using CompleteCallback = std::function<void()>;

        void setOnFrameChange(const FrameCallback& callback) { m_onFrameChange = callback; }
        void setOnComplete(const CompleteCallback& callback) { m_onComplete = callback; }

    private:
        void updateFrame();

        Texture m_texture;
        std::vector<AnimationFrame> m_frames;
        size_t m_currentFrameIndex;
        float m_currentTime;
        float m_playbackSpeed;

        bool m_isPlaying;
        bool m_isLooping;

        FrameCallback m_onFrameChange;
        CompleteCallback m_onComplete;
    };
} // Tina
