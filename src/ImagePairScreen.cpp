#include <ImagePairScreen.h>

void ImagePairScreen::handleEvent(const SDL_Event &event) {
    arrowButton.handleEvent(event);
}

void ImagePairScreen::render() {
    int slideStepAmount = SCREEN_WIDTH / animationSteps;

    if (animationStep > 0 && animationStep <= animationSteps) {
        if (imageState == SingleImageState::TV) {
            fullscreenTVRect.x += slideStepAmount;
            fullscreenDRCRect.x += slideStepAmount;
        } else {
            fullscreenTVRect.x -= slideStepAmount;
            fullscreenDRCRect.x -= slideStepAmount;
        }

        animationStep++;

        if (animationStep > animationSteps) {
            if (imageState == SingleImageState::TV) {
                fullscreenTVRect.x = 0;
                fullscreenDRCRect.x = SCREEN_WIDTH;
            } else {
                fullscreenTVRect.x = -SCREEN_WIDTH;
                fullscreenDRCRect.x = 0;
            }
            animationStep = 0;
            arrowVisible = true;
        }
    }

    SDL_RenderCopy(renderer, imagesPair->textureTV, nullptr, &fullscreenTVRect);
    SDL_RenderCopy(renderer, imagesPair->textureDRC, nullptr, &fullscreenDRCRect);

    if (arrowButton.isAnimationInProgress()) {
        arrowButton.update();
        arrowButton.render(renderer);
        return;
    }

    if (arrowVisible) {
        if (imageState == SingleImageState::TV) {
            arrowRect.x = SCREEN_WIDTH - (SCREEN_WIDTH / 3 / 2);
            arrowButton.setRect(arrowRect);
            arrowButton.setFlip(SDL_FLIP_NONE);
            arrowButton.setButton(0xe);
        } else {
            arrowRect.x = 0;
            arrowButton.setRect(arrowRect);
            arrowButton.setFlip(SDL_FLIP_HORIZONTAL);
            arrowButton.setButton(0xc);
        }
        arrowButton.render(renderer);
    }
}

void ImagePairScreen::setImagePair(ImagesPair *imagesPair) {
    this->imagesPair = imagesPair;
    // Reset all variables
    this->imageState = SingleImageState::TV;
    this->arrowRect = {0, (SCREEN_HEIGHT / 2) - 145, 290, 290};
    this->fullscreenTVRect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    this->fullscreenDRCRect = {SCREEN_WIDTH, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    this->animationStep = 0;
    this->arrowVisible = true;
    this->arrowButton.setRect(arrowRect);
    this->arrowButton.setButton(0xe);
}
