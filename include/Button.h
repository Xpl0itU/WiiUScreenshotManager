#pragma once

#include <SDL2/SDL.h>
#include <SDL_FontCache.h>
#include <functional>
#include <queue>
#include <string>

class Button {
public:
    Button(int x, int y, int width, int height, const std::string &text, SDL_Texture *texture, FC_Font *font, int button, SDL_Color textColor)
        : x(x), y(y), width(width), height(height), text(text), texture(texture), pressed(false), animationQueue(), maxExpansion(10),
          originalX(x), originalY(y), originalWidth(width), originalHeight(height), font(font), button(button), textColor(textColor) {}

    void handleEvent(const SDL_Event &event);

    using OnClickFunction = std::function<void()>;

    void setOnClick(OnClickFunction callback);

    void onClick();

    void render(SDL_Renderer *renderer) const;

    void update();

    void setText(std::string text);

    void setFlip(SDL_RendererFlip flip);

    void setTextColor(SDL_Color color);

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
    const int maxExpansion;

    OnClickFunction onClickFunction;

    int screenWidth = 1920;
    int screenHeight = 1080;
    FC_Font *font;

    int button;
    SDL_RendererFlip flip;
    SDL_Color textColor;
};
