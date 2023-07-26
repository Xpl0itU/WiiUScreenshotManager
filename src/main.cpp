#include <InputUtils.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_FontCache.h>
#include <StateUtils.h>
#include <algorithm>
#include <coreinit/memory.h>
#include <filesystem>
#include <iostream>
#include <romfs-wiiu.h>
#include <sndcore2/core.h>
#include <string>
#include <unordered_map>
#include <vector>

#define SCREEN_WIDTH        1920
#define SCREEN_HEIGHT       1080
#define GRID_SIZE           4
#define IMAGE_WIDTH         SCREEN_WIDTH / GRID_SIZE / 2
#define IMAGE_HEIGHT        SCREEN_HEIGHT / GRID_SIZE / 2
#define SEPARATION          IMAGE_WIDTH / 4
#define FONT_SIZE           36
#define SCREEN_COLOR_BLACK  ((SDL_Color){.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_WHITE  ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF})
#define SCREEN_COLOR_YELLOW ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_D_RED  ((SDL_Color){.r = 0x7F, .g = 0x00, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_GRAY   ((SDL_Color){.r = 0x6A, .g = 0x6A, .b = 0x6A, .a = 0xFF})
#define BUTTON_A            "\uE000"
#define BUTTON_B            "\uE001"
#define BUTTON_X            "\uE002"
#define BUTTON_DPAD         "\uE07D"

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

enum class MenuState {
    ShowAllImages,
    SelectImagesDelete,
    ShowSingleImage,
};

enum class SingleImageState {
    TV,
    DRC,
};

struct Texture {
    SDL_Texture *texture;
    SDL_Rect rect;
    SDL_Rect originalRect;
};

const std::string imagePath = "fs:/vol/external01/wiiu/screenshots/";
FC_Font *font = nullptr;
SDL_Texture *orbTexture = nullptr;
Texture blackTexture;
Texture headerTexture;
Texture backgroundTexture;
Texture cornerButtonTexture;
Texture largeCornerButtonTexture;
Texture backGraphicTexture;
Texture arrowTexture;

bool fileEndsWith(const std::string &filename, const std::string &extension) {
    return filename.size() >= extension.size() && std::equal(extension.rbegin(), extension.rend(), filename.rbegin());
}

bool isFirstRow(int index) {
    return index < GRID_SIZE;
}

bool isLastRow(int index, int imageCount) {
    int lastRow = (imageCount - 1) / GRID_SIZE;
    return index >= (lastRow * GRID_SIZE);
}

bool isImageVisible(const ImagesPair &image, int scrollOffsetY) {
    int imageTop = headerTexture.originalRect.h + image.y + scrollOffsetY;
    int imageBottom = imageTop + IMAGE_HEIGHT + IMAGE_HEIGHT / 2;

    int screenTop = headerTexture.originalRect.h;
    int screenBottom = SCREEN_HEIGHT;

    return (imageBottom >= screenTop) && (imageTop <= screenBottom);
}

void drawRectFilled(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color color) {
    SDL_Color prevColor = {0, 0, 0, 0};
    SDL_GetRenderDrawColor(renderer, &prevColor.r, &prevColor.g, &prevColor.b, &prevColor.a);
    SDL_Rect rect{x, y, w, h};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, prevColor.r, prevColor.g, prevColor.b, prevColor.a);
}

void drawRect(SDL_Renderer *renderer, int x, int y, int w, int h, int borderSize, SDL_Color color) {
    drawRectFilled(renderer, x, y, w, borderSize, color);
    drawRectFilled(renderer, x, y + h - borderSize, w, borderSize, color);
    drawRectFilled(renderer, x, y, borderSize, h, color);
    drawRectFilled(renderer, x + w - borderSize, y, borderSize, h, color);
}

void drawOrb(SDL_Renderer *renderer, int x, int y, int size, bool selected) {
    if (x < 0 || x + size >= SCREEN_WIDTH || y < 0 || y + size >= SCREEN_HEIGHT) {
        return;
    }

    SDL_Color orbColor = selected ? SCREEN_COLOR_D_RED : SCREEN_COLOR_WHITE;
    int orbRadius = size / 2;
    int orbCenterX = x + orbRadius;
    int orbCenterY = y + orbRadius;

    SDL_SetTextureColorMod(orbTexture, orbColor.r, orbColor.g, orbColor.b);

    SDL_Rect orbGraphicRect{x, y, size, size};
    SDL_RenderCopy(renderer, orbTexture, nullptr, &orbGraphicRect);

    if (selected) {
        int crossSize = size / 3;
        int x1 = orbCenterX - crossSize / 2;
        int y1 = orbCenterY - crossSize / 2;
        int x2 = orbCenterX + crossSize / 2;
        int y2 = orbCenterY + crossSize / 2;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
        SDL_RenderDrawLine(renderer, x1, y2, x2, y1);
    }
}

bool showConfirmationDialog(SDL_Renderer *renderer) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(renderer, nullptr);

    int rectWidth = FC_GetWidth(font, "Are you sure you want to delete the selected images?") + 40;
    int buttonsHeight = FC_GetHeight(font, BUTTON_A "Confirm") + FC_GetHeight(font, BUTTON_B "Cancel");
    int messageHeight = FC_GetHeight(font, "Are you sure you want to delete the selected images?");
    int rectHeight = buttonsHeight + messageHeight;

    drawRectFilled(renderer, SCREEN_WIDTH / 2 - rectWidth / 2, SCREEN_HEIGHT / 2 - rectHeight / 2, rectWidth, rectHeight, SCREEN_COLOR_D_RED);

    FC_DrawColor(font, renderer, SCREEN_WIDTH / 2 - rectWidth / 2, (SCREEN_HEIGHT / 2 - rectHeight / 2), SCREEN_COLOR_WHITE, "Are you sure you want to delete the selected images?");
    FC_DrawColor(font, renderer, SCREEN_WIDTH / 2 - rectWidth / 2, (SCREEN_HEIGHT / 2 - rectHeight / 2) + messageHeight * 2, SCREEN_COLOR_WHITE, BUTTON_A "Confirm"
                                                                                                                                                          " " BUTTON_B "Cancel");

    SDL_RenderPresent(renderer);

    Input input;
    while (State::AppRunning()) {
        input.read();
        if (input.get(TRIGGER, PAD_BUTTON_A)) {
            return true;
        } else if (input.get(TRIGGER, PAD_BUTTON_B)) {
            return false;
        }
    }

    return false;
}

std::vector<ImagesPair> scanImagePairsInSubfolders(const std::string &directoryPath, SDL_Renderer *renderer, int offsetX, int offsetY) {
    std::vector<ImagesPair> imagePairs;
    int pairIndex = 1;

    if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath)) {
        return imagePairs;
    }

    std::unordered_map<std::string, std::pair<std::string, std::string>> baseFilenames;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(directoryPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        std::string filename = entry.path().filename().string();
        std::string baseFilename = filename.substr(0, filename.find_last_of('.'));
        if (fileEndsWith(filename, "_TV.jpg") || fileEndsWith(filename, "_TV.png") || fileEndsWith(filename, "_TV.bmp")) {
            baseFilenames[baseFilename].first = entry.path().string();
        } else if (fileEndsWith(filename, "_DRC.jpg") || fileEndsWith(filename, "_DRC.png") || fileEndsWith(filename, "_DRC.bmp")) {
            baseFilenames[baseFilename].second = entry.path().string();
        }
    }

    blackTexture.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 800, 600);
    SDL_SetRenderTarget(renderer, blackTexture.texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, NULL);

    for (const auto &[baseFilename, paths] : baseFilenames) {
        std::string tvPath = paths.first;
        std::string drcPath = paths.second;

        if ((tvPath.empty() || std::filesystem::exists(tvPath)) || (drcPath.empty() || std::filesystem::exists(drcPath))) {
            SDL_Surface *surfaceTV = tvPath.empty() ? nullptr : IMG_Load(tvPath.c_str());
            SDL_Surface *surfaceDRC = drcPath.empty() ? nullptr : IMG_Load(drcPath.c_str());

            ImagesPair imgPair;
            imgPair.textureTV = surfaceTV ? SDL_CreateTextureFromSurface(renderer, surfaceTV) : blackTexture.texture;
            imgPair.textureDRC = surfaceDRC ? SDL_CreateTextureFromSurface(renderer, surfaceDRC) : blackTexture.texture;

            if (surfaceTV) SDL_FreeSurface(surfaceTV);
            if (surfaceDRC) SDL_FreeSurface(surfaceDRC);

            imgPair.x = offsetX + (pairIndex - 1) % GRID_SIZE * (IMAGE_WIDTH + SEPARATION);
            imgPair.y = offsetY + (pairIndex - 1) / GRID_SIZE * (IMAGE_WIDTH + SEPARATION);

            imgPair.selected = false;
            imgPair.pathTV = tvPath;
            imgPair.pathDRC = drcPath;

            imagePairs.push_back(imgPair);
            ++pairIndex;
        }
    }

    return imagePairs;
}

int main() {
    AXInit();
    AXQuit();
    State::init();
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP);

    romfsInit();

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

    int totalWidth = GRID_SIZE * (IMAGE_WIDTH + SEPARATION) - SEPARATION;
    int totalHeight = GRID_SIZE * (IMAGE_WIDTH + SEPARATION) - SEPARATION;

    int offsetX = (SCREEN_WIDTH - totalWidth) / 2;
    int offsetY = (SCREEN_HEIGHT - totalHeight) / 2;

    std::vector<ImagesPair> images = scanImagePairsInSubfolders(imagePath, renderer, offsetX, offsetY);

    int selectedImageIndex = 0;
    int scrollOffsetY = 0;

    Input input;

    MenuState state = MenuState::ShowAllImages;
    SingleImageState singleImageState = SingleImageState::TV;

    arrowTexture.texture = IMG_LoadTexture(renderer, "romfs:/arrow_image.png");
    if (!arrowTexture.texture) {
        return 1;
    }

    backgroundTexture.texture = IMG_LoadTexture(renderer, "romfs:/backdrop.png");
    if (!backgroundTexture.texture) {
        return 1;
    }
    cornerButtonTexture.texture = IMG_LoadTexture(renderer, "romfs:/corner-button.png");
    if (!cornerButtonTexture.texture) {
        return 1;
    }
    largeCornerButtonTexture.texture = IMG_LoadTexture(renderer, "romfs:/large-corner-button.png");
    if (!largeCornerButtonTexture.texture) {
        return 1;
    }
    backGraphicTexture.texture = IMG_LoadTexture(renderer, "romfs:/back_graphic.png");
    if (!backGraphicTexture.texture) {
        return 1;
    }
    headerTexture.texture = IMG_LoadTexture(renderer, "romfs:/header.png");
    if (!headerTexture.texture) {
        return 1;
    }
    orbTexture = IMG_LoadTexture(renderer, "romfs:/orb.png");
    if (!orbTexture) {
        return 1;
    }
    backgroundTexture.originalRect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    cornerButtonTexture.originalRect = {0, SCREEN_HEIGHT - 256, 256, 256};
    largeCornerButtonTexture.originalRect = {SCREEN_WIDTH - 512, 0, 512, 256};
    backGraphicTexture.originalRect = {0, SCREEN_HEIGHT - 128, 128, 128};
    headerTexture.originalRect = {0, 0, SCREEN_WIDTH, 256};
    arrowTexture.originalRect = {0, (SCREEN_HEIGHT / 2) - 145, 290, 290};

    backgroundTexture.rect = backgroundTexture.originalRect;
    cornerButtonTexture.rect = cornerButtonTexture.originalRect;
    largeCornerButtonTexture.rect = largeCornerButtonTexture.originalRect;
    backGraphicTexture.rect = backGraphicTexture.originalRect;
    headerTexture.rect = headerTexture.originalRect;
    arrowTexture.rect = arrowTexture.originalRect;

    while (State::AppRunning()) {
        input.read();
        if (input.get(TRIGGER, PAD_BUTTON_ANY)) {
            if (input.get(TRIGGER, PAD_BUTTON_A)) {
                if (!images.empty()) {
                    if (state == MenuState::SelectImagesDelete) {
                        images[selectedImageIndex].selected = !images[selectedImageIndex].selected;
                    } else if (state == MenuState::ShowAllImages) {
                        state = MenuState::ShowSingleImage;
                    }
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_UP)) {
                if (state != MenuState::ShowSingleImage) {
                    if (selectedImageIndex >= GRID_SIZE) {
                        selectedImageIndex -= GRID_SIZE;
                        if (isFirstRow(selectedImageIndex)) {
                            if (scrollOffsetY >= 0) {
                                scrollOffsetY = 0;
                            } else {
                                scrollOffsetY += (GRID_SIZE - 1) * (IMAGE_HEIGHT + SEPARATION);
                                if (scrollOffsetY > 0) {
                                    scrollOffsetY = 0;
                                }
                            }
                        }
                    }
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_DOWN)) {
                if (state != MenuState::ShowSingleImage) {
                    if (selectedImageIndex < static_cast<int>(images.size()) - GRID_SIZE) {
                        selectedImageIndex += GRID_SIZE;
                        if (isLastRow(selectedImageIndex, images.size())) {
                            scrollOffsetY -= IMAGE_WIDTH + SEPARATION;
                            int lastRow = (images.size() - 1) / GRID_SIZE;
                            int lastVisibleRow = (lastRow * GRID_SIZE - 1) / GRID_SIZE;
                            int lastRowTop = -lastVisibleRow * (IMAGE_HEIGHT + SEPARATION);

                            if (scrollOffsetY <= lastRowTop) {
                                scrollOffsetY = lastRowTop;
                            } else {
                                scrollOffsetY -= (GRID_SIZE - 1) * (IMAGE_HEIGHT + SEPARATION);
                                if (scrollOffsetY < lastRowTop) {
                                    scrollOffsetY = lastRowTop;
                                }
                            }
                        }
                    }
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_LEFT)) {
                if (state != MenuState::ShowSingleImage) {
                    if (selectedImageIndex % GRID_SIZE != 0) {
                        selectedImageIndex--;
                    }
                }
                if ((state == MenuState::ShowSingleImage) && (singleImageState == SingleImageState::DRC)) {
                    singleImageState = SingleImageState::TV;
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_RIGHT)) {
                if (state != MenuState::ShowSingleImage) {
                    if (selectedImageIndex % GRID_SIZE != GRID_SIZE - 1 && selectedImageIndex < static_cast<int>(images.size()) - 1) {
                        selectedImageIndex++;
                    }
                }
                if ((state == MenuState::ShowSingleImage) && (singleImageState == SingleImageState::TV)) {
                    singleImageState = SingleImageState::DRC;
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_X)) {
                if (state == MenuState::ShowAllImages) {
                    state = MenuState::SelectImagesDelete;
                } else if (state == MenuState::SelectImagesDelete) {
                    if (showConfirmationDialog(renderer)) {
                        if (std::any_of(images.begin(), images.end(), [](const ImagesPair &image) { return image.selected; })) {
                            std::vector<ImagesPair> removedImages;
                            for (auto &image : images) {
                                if (image.selected) {
                                    removedImages.push_back(image);
                                    if (!image.pathTV.empty()) {
                                        std::filesystem::remove(image.pathTV);
                                    }
                                    if (!image.pathDRC.empty()) {
                                        std::filesystem::remove(image.pathDRC);
                                    }
                                    if (image.textureTV && image.textureTV != blackTexture.texture) {
                                        SDL_DestroyTexture(image.textureTV);
                                        image.textureTV = nullptr;
                                    }
                                    if (image.textureDRC && image.textureDRC != blackTexture.texture) {
                                        SDL_DestroyTexture(image.textureDRC);
                                        image.textureDRC = nullptr;
                                    }
                                }
                            }
                            for (auto &image : removedImages) {
                                auto it = std::find(images.begin(), images.end(), image);
                                if (it != images.end()) {
                                    images.erase(it);
                                }
                            }
                            removedImages.clear();
                            removedImages.shrink_to_fit();
                            selectedImageIndex = 0;
                            int totalImages = static_cast<int>(images.size());
                            for (int i = 0; i < totalImages; ++i) {
                                int row = i / GRID_SIZE;
                                int col = i % GRID_SIZE;
                                images[i].x = offsetX + col * (IMAGE_WIDTH + SEPARATION);
                                images[i].y = offsetY + row * (IMAGE_WIDTH + SEPARATION);
                            }
                        }
                        state = MenuState::ShowAllImages;
                    }
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_B)) {
                state = MenuState::ShowAllImages;
                singleImageState = SingleImageState::TV;
                if (std::any_of(images.begin(), images.end(), [](const ImagesPair &image) { return image.selected; })) {
                    for (auto &image : images) {
                        image.selected = false;
                    }
                }
            }
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, backgroundTexture.texture, nullptr, &backgroundTexture.originalRect);
        if (state != MenuState::ShowSingleImage) {
            if (images.empty()) {
                FC_Draw(font, renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, "No images found");
            } else {
                SDL_SetTextureColorMod(headerTexture.texture, 0, 0, 147);
                SDL_RenderCopy(renderer, headerTexture.texture, nullptr, &headerTexture.originalRect);
                SDL_SetTextureBlendMode(headerTexture.texture, SDL_BLENDMODE_BLEND);
                FC_DrawColor(font, renderer, headerTexture.originalRect.x + (headerTexture.originalRect.w / 2), (headerTexture.originalRect.y + (headerTexture.originalRect.h / 2)) - 100, SCREEN_COLOR_WHITE, "Album");
                for (size_t i = 0; i < images.size(); ++i) {
                    if (isImageVisible(images[i], scrollOffsetY)) {
                        SDL_Rect destRectTV = {images[i].x, headerTexture.originalRect.h + images[i].y + scrollOffsetY, IMAGE_WIDTH, IMAGE_HEIGHT};
                        SDL_Rect destRectDRC = {images[i].x + IMAGE_WIDTH / 2, headerTexture.originalRect.h + images[i].y + scrollOffsetY + IMAGE_WIDTH / 2, IMAGE_WIDTH / 2, IMAGE_HEIGHT / 2};
                        if (images[i].selected) {
                            SDL_SetTextureColorMod(images[i].textureTV, 0, 255, 0);
                            SDL_SetTextureColorMod(images[i].textureDRC, 0, 255, 0);
                        } else {
                            SDL_SetTextureColorMod(images[i].textureTV, 255, 255, 255);
                            SDL_SetTextureColorMod(images[i].textureDRC, 255, 255, 255);
                        }
                        SDL_SetTextureBlendMode(images[i].textureTV, SDL_BLENDMODE_BLEND);
                        SDL_SetTextureBlendMode(images[i].textureDRC, SDL_BLENDMODE_BLEND);
                        SDL_RenderCopy(renderer, images[i].textureTV, nullptr, &destRectTV);
                        SDL_RenderCopy(renderer, images[i].textureDRC, nullptr, &destRectDRC);
                        if (state == MenuState::SelectImagesDelete) {
                            drawOrb(renderer, images[i].x - 10, headerTexture.originalRect.h + images[i].y + scrollOffsetY - 10, 60, images[i].selected);
                        }
                    }
                }

                SDL_SetTextureBlendMode(largeCornerButtonTexture.texture, SDL_BLENDMODE_BLEND);
                if (state == MenuState::SelectImagesDelete) {
                    SDL_SetTextureColorMod(largeCornerButtonTexture.texture, 255, 0, 0);
                    SDL_RenderCopyEx(renderer, largeCornerButtonTexture.texture, nullptr, &largeCornerButtonTexture.originalRect, 0.0, nullptr, (SDL_RendererFlip) (SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
                    FC_DrawColor(font, renderer, largeCornerButtonTexture.originalRect.x + (largeCornerButtonTexture.originalRect.w / 2), (largeCornerButtonTexture.originalRect.y + (largeCornerButtonTexture.originalRect.h / 2)) - 100, SCREEN_COLOR_WHITE, BUTTON_X " Delete");
                    SDL_SetTextureBlendMode(cornerButtonTexture.texture, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureBlendMode(backGraphicTexture.texture, SDL_BLENDMODE_BLEND);
                    SDL_RenderCopy(renderer, cornerButtonTexture.texture, nullptr, &cornerButtonTexture.originalRect);
                    SDL_RenderCopy(renderer, backGraphicTexture.texture, nullptr, &backGraphicTexture.originalRect);
                } else {
                    SDL_SetTextureColorMod(largeCornerButtonTexture.texture, 255, 255, 255);
                    SDL_RenderCopyEx(renderer, largeCornerButtonTexture.texture, nullptr, &largeCornerButtonTexture.originalRect, 0.0, nullptr, (SDL_RendererFlip) (SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
                    FC_DrawColor(font, renderer, (largeCornerButtonTexture.originalRect.x + (largeCornerButtonTexture.originalRect.w / 2)) - 5, (largeCornerButtonTexture.originalRect.y + (largeCornerButtonTexture.originalRect.h / 2)) - 100, SCREEN_COLOR_BLACK, BUTTON_X " Select Items");
                }
                drawRect(renderer, images[selectedImageIndex].x, headerTexture.originalRect.h + images[selectedImageIndex].y + scrollOffsetY, IMAGE_WIDTH, IMAGE_HEIGHT * 1.5, 7, SCREEN_COLOR_YELLOW);
            }

            SDL_RenderPresent(renderer);
        } else if (state == MenuState::ShowSingleImage && selectedImageIndex >= 0 && selectedImageIndex < static_cast<int>(images.size())) {
            switch (singleImageState) {
                case SingleImageState::TV:
                    arrowTexture.originalRect.x = SCREEN_WIDTH - (SCREEN_WIDTH / 3 / 2);
                    SDL_RenderCopy(renderer, images[selectedImageIndex].textureTV, nullptr, &backgroundTexture.originalRect);
                    SDL_RenderCopy(renderer, arrowTexture.texture, nullptr, &arrowTexture.originalRect);
                    break;
                case SingleImageState::DRC:
                    arrowTexture.originalRect.x = 0;
                    SDL_RenderCopy(renderer, images[selectedImageIndex].textureDRC, nullptr, &backgroundTexture.originalRect);
                    SDL_RenderCopyEx(renderer, arrowTexture.texture, nullptr, &arrowTexture.originalRect, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
                    break;
                default:
                    arrowTexture.originalRect.x = SCREEN_WIDTH - (SCREEN_WIDTH / 3 / 2);
                    SDL_RenderCopy(renderer, images[selectedImageIndex].textureTV, nullptr, &backgroundTexture.originalRect);
                    SDL_RenderCopy(renderer, arrowTexture.texture, nullptr, &arrowTexture.originalRect);
                    break;
            }
            SDL_SetTextureBlendMode(cornerButtonTexture.texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureBlendMode(backGraphicTexture.texture, SDL_BLENDMODE_BLEND);
            SDL_RenderCopy(renderer, cornerButtonTexture.texture, nullptr, &cornerButtonTexture.originalRect);
            SDL_RenderCopy(renderer, backGraphicTexture.texture, nullptr, &backGraphicTexture.originalRect);
            SDL_RenderPresent(renderer);
        }
    }

    for (const auto &img : images) {
        if (img.textureTV) {
            SDL_DestroyTexture(img.textureTV);
        }
        if (img.textureDRC) {
            SDL_DestroyTexture(img.textureDRC);
        }
    }
    if (blackTexture.texture) {
        SDL_DestroyTexture(blackTexture.texture);
    }
    if (arrowTexture.texture) {
        SDL_DestroyTexture(arrowTexture.texture);
    }
    if (backgroundTexture.texture) {
        SDL_DestroyTexture(backgroundTexture.texture);
    }
    if (cornerButtonTexture.texture) {
        SDL_DestroyTexture(cornerButtonTexture.texture);
    }
    if (backGraphicTexture.texture) {
        SDL_DestroyTexture(backGraphicTexture.texture);
    }
    if (headerTexture.texture) {
        SDL_DestroyTexture(headerTexture.texture);
    }
    if (orbTexture) {
        SDL_DestroyTexture(orbTexture);
    }
    if (largeCornerButtonTexture.texture) {
        SDL_DestroyTexture(largeCornerButtonTexture.texture);
    }

    FC_FreeFont(font);
    font = nullptr;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    IMG_Quit();

    romfsExit();

    State::shutdown();

    return 0;
}
