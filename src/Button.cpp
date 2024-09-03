#include <Button.h>
#include <chrono>
#include <iostream>
#include <thread>

Button::Button(int x, int y, int width, int height, SDL_Texture* texture, FC_Font* font, const std::string& text, SDL_Color textColor)
    : x(x), y(y), width(width), height(height), texture(texture), font(font), text(text), textColor(textColor),
      pressed(false), flip(SDL_FLIP_NONE), button(0),
      originalX(x), originalY(y), originalWidth(width), originalHeight(height),
      scale(1.0f), scalePressed(1.1f), touching(false), touchDown(false), inflated(false),
      controllerButton(SDL_CONTROLLER_BUTTON_INVALID) {
}

void Button::handleEvent(const SDL_Event& event) {
    int touchX, touchY;
    bool isTouched = false;

    switch (event.type) {
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            touchX = static_cast<int>(event.tfinger.x * screenWidth);
            touchY = static_cast<int>(event.tfinger.y * screenHeight);
            isTouched = (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            if (event.cbutton.button == controllerButton) {
                touchX = x + width / 2;
                touchY = y + height / 2;
                isTouched = (event.type == SDL_CONTROLLERBUTTONDOWN);
            } else {
                return;
            }
            break;
        default:
            return;
    }

    updateButton(touchX, touchY, isTouched);
}

void Button::updateButton(int touchX, int touchY, bool isTouched) {
    float interpolateScale = (scalePressed - 1.0f) / 3.0f;  // Increased for faster animation

    if (isPointInside(touchX, touchY) && !touching && isTouched) {
        scale = 1.0f;
        touching = true;
        inflated = false;
        onStart();
    }

    if (touching) {
        if (!touchDown) {
            scale += interpolateScale;
        } else {
            scale -= interpolateScale;
        }

        if (scale >= scalePressed) {
            scale = scalePressed;
            if (!inflated) {
                onInflate();
                inflated = true;
            }

            if (!isPointInside(touchX, touchY) && isTouched) {
                touchDown = true;
                inflated = false;
            } else if (!isTouched) {
                onInflateRelease();
                touchDown = true;
                inflated = false;
            }
        } else if (scale <= 1.0f) {
            scale = 1.0f;
            touching = false;
            touchDown = false;
            onDeflate();
        }
    }

    pressed = touching && !touchDown;
}

void Button::render(SDL_Renderer* renderer) const {
    int scaledWidth = static_cast<int>(width * scale);
    int scaledHeight = static_cast<int>(height * scale);
    int scaledX = x + (width - scaledWidth) / 2;
    int scaledY = y + (height - scaledHeight) / 2;

    SDL_Rect buttonRect = {scaledX, scaledY, scaledWidth, scaledHeight};

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

    if (font) {
        FC_DrawColor(font, renderer, 
                     scaledX + (scaledWidth - FC_GetWidth(font, text.c_str())) / 2, 
                     scaledY + (scaledHeight - FC_GetHeight(font, text.c_str())) / 2, 
                     textColor, text.c_str());
    }
}

void Button::update() {
    // This method is now empty as the animation is handled in updateButton
}

bool Button::isPointInside(int x, int y) const {
    return x >= this->x && x < this->x + width && y >= this->y && y < this->y + height;
}

void Button::onStart() {
    // Placeholder for start event
}

void Button::onInflate() {
    // Placeholder for inflate event
}

void Button::onInflateRelease() {
    if (onClickFunction) {
        onClickFunction();
    }
}

void Button::onDeflate() {
    // Placeholder for deflate event
}

// Existing setter methods remain unchanged
void Button::setText(const std::string text) {
    this->text = text;
}

void Button::setFlip(SDL_RendererFlip flip) {
    this->flip = flip;
}

void Button::setTextColor(SDL_Color color) {
    this->textColor = color;
}

void Button::setTexture(SDL_Texture* texture) {
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

void Button::setControllerButton(SDL_GameControllerButton button) {
    this->controllerButton = button;
}

void Button::setOnClick(OnClickFunction callback) {
    onClickFunction = std::move(callback);
}

bool Button::isAnimationInProgress() const {
    return touching;
}