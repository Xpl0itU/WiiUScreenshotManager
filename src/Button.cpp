#include <Button.h>
#include <chrono>
#include <iostream>
#include <thread>

void Button::handleEvent(const SDL_Event &event) {
    switch (event.type) {
        case SDL_FINGERDOWN: {
            int touchX = static_cast<int>(event.tfinger.x * screenWidth);
            int touchY = static_cast<int>(event.tfinger.y * screenHeight);

            if (touchX >= x && touchX <= x + width && touchY >= y && touchY <= y + height) {
                pressed = true;
                animationQueue.push({AnimationType::EXPAND, 0});
            }
            break;
        }
        case SDL_FINGERUP: {
            if (pressed) {
                pressed = false;
                animationQueue.push({AnimationType::CONTRACT, 0});
                onClick();
            }
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN: {
            if (event.cbutton.button == button) {
                pressed = true;
                animationQueue.push({AnimationType::EXPAND, 0});
            }
            break;
        }
        case SDL_CONTROLLERBUTTONUP: {
            if (event.cbutton.button == button) {
                if (pressed) {
                    pressed = false;
                    animationQueue.push({AnimationType::CONTRACT, 0});
                    onClick();
                }
            }
            break;
        }
    }
}

void Button::setOnClick(OnClickFunction callback) {
    onClickFunction = std::move(callback);
}

void Button::onClick() {
    if (onClickFunction) {
        onClickFunction();
    }
}

void Button::render(SDL_Renderer *renderer) const {
    SDL_Rect buttonRect = {x, y, width, height};

    if (texture) {
        SDL_RenderCopyEx(renderer, texture, nullptr, &buttonRect, 0.0, nullptr, flip);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderFillRect(renderer, &buttonRect);

        if (pressed) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &buttonRect);
        }
    }

    if (flip == (SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL)) {
        int textWidth = FC_GetWidth(font, text.c_str());
        int textHeight = FC_GetHeight(font, text.c_str());

        int textX = (x + (width - textWidth) / 3) - 50;
        int textY = (y + (height - textHeight)) - 40;

        textX = x + width - (textX - x) - textWidth;
        textY = y + height - (textY - y) - textHeight;

        FC_DrawColor(font, renderer, textX, textY, textColor, text.c_str());
    } else {
        FC_DrawColor(font, renderer, x + (width - FC_GetWidth(font, text.c_str())) / 2, y + (height - FC_GetHeight(font, text.c_str())) / 2, textColor, text.c_str());
    }
}

void Button::update() {
    if (!animationQueue.empty()) {
        auto &animation = animationQueue.front();

        switch (animation.type) {
            case AnimationType::EXPAND:
                if (animation.counter < maxExpansion) {
                    if (x > 0) {
                        width += 8;
                        x -= 8;
                    }

                    if (y > 0) {
                        height += 8;
                        y -= 8;
                    }

                    if (x + width < screenWidth) {
                        width += 8;
                    }

                    if (y + height < screenHeight) {
                        height += 8;
                    }

                    animation.counter++;
                } else {
                    animationQueue.pop();
                }
                break;

            case AnimationType::CONTRACT:
                if (animation.counter > 0) {
                    int contractionStep = 2;

                    if (x < originalX) {
                        x++;
                        width -= contractionStep;
                    } else if (x > originalX) {
                        x--;
                        width -= contractionStep;
                    }

                    if (y < originalY) {
                        y++;
                        height -= contractionStep;
                    } else if (y > originalY) {
                        y--;
                        height -= contractionStep;
                    }

                    if (x + width > originalX + originalWidth) {
                        width -= contractionStep;
                    }

                    if (y + height > originalY + originalHeight) {
                        height -= contractionStep;
                    }

                    animation.counter--;
                } else {
                    x = originalX;
                    y = originalY;
                    width = originalWidth;
                    height = originalHeight;
                    animationQueue.pop();
                }
                break;
        }
    }
}

void Button::setText(const std::string text) {
    this->text = text;
}

void Button::setFlip(SDL_RendererFlip flip) {
    this->flip = flip;
}

void Button::setTextColor(SDL_Color color) {
    this->textColor = color;
}

void Button::setTexture(SDL_Texture *texture) {
    this->texture = texture;
}

void Button::setRect(SDL_Rect rect) {
    x = rect.x;
    y = rect.y;
    width = rect.w;
    height = rect.h;
    originalX = x;
    originalY = y;
    originalWidth = width;
    originalHeight = height;
}

void Button::setButton(int button) {
    this->button = button;
}

bool Button::isAnimationInProgress() const {
    return !animationQueue.empty();
}
