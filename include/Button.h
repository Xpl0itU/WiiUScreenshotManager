#pragma once

#include <SDL2/SDL.h>
#include <SDL_FontCache.h>
#include <functional>
#include <queue>
#include <string>

class Button {
public:
    Button(int x, int y, int width, int height, SDL_Texture *texture, FC_Font *font, const std::string &text, SDL_Color textColor);

    void handleEvent(const SDL_Event &event);

    void updateButton(int touchX, int touchY, bool isTouched);

    using OnClickFunction = std::function<void()>;

    void setOnClick(OnClickFunction callback);

    void render(SDL_Renderer *renderer) const;

    void update();

    bool isPointInside(int x, int y) const;

    void onStart();

    void onInflate();

    void onInflateRelease();

    void onDeflate();

    void setText(std::string text);

    void setFlip(SDL_RendererFlip flip);

    void setTextColor(SDL_Color color);

    void setTexture(SDL_Texture *texture);

    void setRect(SDL_Rect rect);

    void setControllerButton(SDL_GameControllerButton button);

    bool isAnimationInProgress() const;

private:
    int x, y, width, height;
    int originalX, originalY, originalWidth, originalHeight;
    std::string text;
    SDL_Texture *texture;
    bool pressed;

    enum class AnimationType {
        EXPAND,
        CONTRACT
    };

    struct Animation {
        AnimationType type;
        int counter;
    };

    std::queue<Animation> animationQueue;
    int maxExpansion;

    OnClickFunction onClickFunction;

    int screenWidth = 1920;
    int screenHeight = 1080;
    FC_Font *font;

    int button;
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    SDL_Color textColor;

    float scale;
    float scalePressed;
    float touching;
    float touchDown;
    float inflated;

    SDL_GameControllerButton controllerButton;
};
