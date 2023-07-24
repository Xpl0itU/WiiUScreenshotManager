#include <InputUtils.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_FontCache.h>
#include <coreinit/memory.h>
#include <iostream>
#include <romfs-wiiu.h>
#include <string>
#include <vector>

#define SCREEN_WIDTH       1280
#define SCREEN_HEIGHT      720
#define GRID_SIZE          3
#define IMAGE_SIZE         SCREEN_WIDTH / GRID_SIZE / 2
#define SEPARATION         IMAGE_SIZE / 3
#define MARGIN_TOP         200
#define MARGIN_BOTTOM      50
#define FONT_SIZE          28
#define SCREEN_COLOR_WHITE ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF})

const std::string imagePath = "romfs:/";
FC_Font *font;

struct Image {
    SDL_Texture *texture;
    int x, y;
    bool selected;
};

int main() {
    romfsInit();
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_PNG);

    SDL_Window *window = SDL_CreateWindow(nullptr, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    void *ttf;
    size_t size;
    OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &ttf, &size);
    font = FC_CreateFont();
    if (font == nullptr) {
        return 1;
    }
    SDL_RWops *rw = SDL_RWFromConstMem(ttf, size);
    if (!FC_LoadFont_RW(font, renderer, rw, 1, FONT_SIZE, SCREEN_COLOR_WHITE, TTF_STYLE_NORMAL)) {
        return 1;
    }

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

            img.selected = false;

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
            if (input.get(TRIGGER, PAD_BUTTON_A)) {
                images[selectedImageIndex].selected = !images[selectedImageIndex].selected;
            } else if (input.get(TRIGGER, PAD_BUTTON_UP)) {
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
            } else if (input.get(TRIGGER, PAD_BUTTON_X)) {
                SDL_DestroyTexture(images[selectedImageIndex].texture);
                images[selectedImageIndex].texture = nullptr;
                images.erase(images.begin() + selectedImageIndex);

                int totalImages = static_cast<int>(images.size());
                for (int i = 0; i < totalImages; ++i) {
                    int row = i / GRID_SIZE;
                    int col = i % GRID_SIZE;
                    images[i].x = offsetX + col * (IMAGE_SIZE + SEPARATION);
                    images[i].y = offsetY + row * (IMAGE_SIZE + SEPARATION);
                }
            }
        }

        SDL_RenderClear(renderer);

        for (size_t i = 0; i < images.size(); ++i) {
            SDL_Rect destRect = {images[i].x, images[i].y + scrollOffsetY, IMAGE_SIZE, IMAGE_SIZE};
            if (images[i].selected) {
                SDL_SetTextureColorMod(images[i].texture, 0, 255, 0);
            } else {
                SDL_SetTextureColorMod(images[i].texture, 255, 255, 255);
            }
            SDL_SetTextureBlendMode(images[i].texture, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(renderer, images[i].texture, nullptr, &destRect);
        }

        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_Rect outlineRect = {images[selectedImageIndex].x, images[selectedImageIndex].y + scrollOffsetY, IMAGE_SIZE + 5, IMAGE_SIZE + 5};
        SDL_RenderDrawRect(renderer, &outlineRect);

        SDL_SetRenderDrawColor(renderer, 0, 0, 139, 255);

        SDL_Rect hudRect = {SCREEN_WIDTH - IMAGE_SIZE - SEPARATION, 0, IMAGE_SIZE, SCREEN_HEIGHT};
        SDL_RenderFillRect(renderer, &hudRect);

        SDL_SetRenderDrawColor(renderer, 173, 216, 230, 255);

        // Draw hud text
        FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 5, "Album");
        std::string allImagesCount = "(" + std::to_string(images.size()) + ")";
        SDL_Rect separatorLine = {SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 40, IMAGE_SIZE, 1};
        SDL_RenderFillRect(renderer, &separatorLine);
        FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 50, "All Images");
        FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 80, allImagesCount.c_str());
        // Draw controls at the bottom of the hud rect
        FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 130, "DPAD: Move");
        FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 100, "A: Select");
        FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 70, "B: Back");
        FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 40, "X: Delete");

        SDL_RenderPresent(renderer);
    }

    for (const auto &img : images) {
        SDL_DestroyTexture(img.texture);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    FC_FreeFont(font);
    font = NULL;
    SDL_Quit();
    IMG_Quit();
    romfsExit();

    return 0;
}
