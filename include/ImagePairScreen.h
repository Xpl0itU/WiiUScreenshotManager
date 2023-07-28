#pragma once

#include <Button.h>
#include <SDL2/SDL.h>
#include <SDL_FontCache.h>
#include <string>

#define SCREEN_WIDTH  1920
#define SCREEN_HEIGHT 1080

struct ImagesPair {
    SDL_Texture *textureTV;
    SDL_Texture *textureDRC;
    int x, y;
    bool selected;
    std::string pathTV;
    std::string pathDRC;

    bool operator==(const ImagesPair &other) const {
        return pathTV == other.pathTV && pathDRC == other.pathDRC;
    }
};

enum class SingleImageState {
    TV,
    DRC,
};

class ImagePairScreen {
public:
    ImagePairScreen(ImagesPair *imagesPair, SDL_Texture *arrowTexture, SDL_Renderer *renderer)
        : imagesPair(imagesPair), arrowTexture(arrowTexture), renderer(renderer), animationSteps(50),
          animationStep(0), arrowVisible(true), arrowButton(0, (SCREEN_HEIGHT / 2) - 145, 290, 290, "", arrowTexture, nullptr, 0xe, SDL_Color({0, 0, 0, 0})) {
        fullscreenTVRect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        fullscreenDRCRect = {SCREEN_WIDTH, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        arrowButton.setTexture(arrowTexture);
        arrowButton.setRect(arrowRect);
        arrowButton.setButton(0xe);
        arrowButton.setOnClick([this] {
            if (imageState == SingleImageState::TV) {
                imageState = SingleImageState::DRC;
            } else {
                imageState = SingleImageState::TV;
            }
            animationStep = 1;
            arrowVisible = false;
        });
    }

    void handleEvent(SDL_Event &event);

    void render();

    void setImagePair(ImagesPair *imagesPair);

private:
    ImagesPair *imagesPair;
    SDL_Texture *arrowTexture;
    Button arrowButton;
    SDL_Renderer *renderer;
    int animationSteps;
    int animationStep;
    bool arrowVisible;

    SingleImageState imageState = SingleImageState::TV;

    SDL_Rect arrowRect = {0, (SCREEN_HEIGHT / 2) - 145, 290, 290};
    SDL_Rect fullscreenTVRect;
    SDL_Rect fullscreenDRCRect;
};
