#include <InputUtils.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <iostream>
#include <romfs-wiiu.h>
#include <string>
#include <vector>

#define SCREEN_WIDTH  1280
#define SCREEN_HEIGHT 720
#define GRID_SIZE     3
#define IMAGE_SIZE    SCREEN_WIDTH / GRID_SIZE / 2
#define SEPARATION    IMAGE_SIZE / 3
#define MARGIN_TOP    200
#define MARGIN_BOTTOM 50

const std::string imagePath = "romfs:/";

struct Image {
    SDL_Texture *texture;
    int x, y;
};

int main() {
    romfsInit();
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);

    SDL_Window *window = SDL_CreateWindow(nullptr, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    std::vector<Image> images;

    int imageIndex = 1;
    bool foundImage = true;

    int totalWidth = GRID_SIZE * (IMAGE_SIZE + SEPARATION) - SEPARATION;
    int totalHeight = GRID_SIZE * (IMAGE_SIZE + SEPARATION) - SEPARATION + MARGIN_TOP + MARGIN_BOTTOM;

    int offsetX = (SCREEN_WIDTH - totalWidth) / 2 - IMAGE_SIZE / 3;
    int offsetY = (SCREEN_HEIGHT - totalHeight) / 2 + MARGIN_TOP;

    while (foundImage) {
        std::string filename = imagePath + "image" + std::to_string(imageIndex) + ".png";
        SDL_Surface *surface = IMG_Load(filename.c_str());

        if (!surface) {
            foundImage = false;
        } else {
            Image img;
            img.texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);

            img.x = offsetX + (imageIndex - 1) % GRID_SIZE * (IMAGE_SIZE + SEPARATION);
            img.y = offsetY + (imageIndex - 1) / GRID_SIZE * (IMAGE_SIZE + SEPARATION);

            images.push_back(img);
        }

        imageIndex++;
    }

    int selectedImageIndex = 0;
    int scrollOffsetY = 0;

    bool quit = false;
    SDL_Event event;

    Input input;

    while (!quit) {
        input.read();
        SDL_PollEvent(&event);
        if (event.type == SDL_QUIT) {
            quit = true;
        } else if (input.get(TRIGGER, PAD_BUTTON_ANY)) {
            if (input.get(TRIGGER, PAD_BUTTON_UP)) {
                if (selectedImageIndex >= GRID_SIZE) {
                    selectedImageIndex -= GRID_SIZE;
                    scrollOffsetY += IMAGE_SIZE + SEPARATION;
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_DOWN)) {
                if (selectedImageIndex < static_cast<int>(images.size()) - GRID_SIZE) {
                    selectedImageIndex += GRID_SIZE;
                    scrollOffsetY -= IMAGE_SIZE + SEPARATION;
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_LEFT)) {
                if (selectedImageIndex % GRID_SIZE != 0) {
                    selectedImageIndex--;
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_RIGHT)) {
                if (selectedImageIndex % GRID_SIZE != GRID_SIZE - 1 && selectedImageIndex < static_cast<int>(images.size()) - 1) {
                    selectedImageIndex++;
                }
            }
        }

        SDL_RenderClear(renderer);

        for (size_t i = 0; i < images.size(); ++i) {
            SDL_Rect destRect = {images[i].x, images[i].y + scrollOffsetY, IMAGE_SIZE, IMAGE_SIZE};
            SDL_RenderCopy(renderer, images[i].texture, nullptr, &destRect);
        }

        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_Rect outlineRect = {images[selectedImageIndex].x, images[selectedImageIndex].y + scrollOffsetY, IMAGE_SIZE + 5, IMAGE_SIZE + 5};
        SDL_RenderDrawRect(renderer, &outlineRect);

        SDL_SetRenderDrawColor(renderer, 0, 0, 139, 255);

        SDL_Rect hudRect = {SCREEN_WIDTH - IMAGE_SIZE - SEPARATION, 0, IMAGE_SIZE, SCREEN_HEIGHT};
        SDL_RenderFillRect(renderer, &hudRect);

        SDL_SetRenderDrawColor(renderer, 173, 216, 230, 255);

        SDL_RenderPresent(renderer);
    }

    for (const auto &img : images) {
        SDL_DestroyTexture(img.texture);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    IMG_Quit();
    romfsExit();

    return 0;
}
