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
#define GRID_SIZE           3
#define IMAGE_WIDTH         SCREEN_WIDTH / GRID_SIZE / 2
#define IMAGE_HEIGHT        SCREEN_HEIGHT / GRID_SIZE / 2
#define SEPARATION          IMAGE_WIDTH / 3
#define MARGIN_TOP          200
#define MARGIN_BOTTOM       50
#define FONT_SIZE           36
#define SCREEN_COLOR_WHITE  ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF})
#define SCREEN_COLOR_YELLOW ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_D_RED  ((SDL_Color){.r = 0x7F, .g = 0x00, .b = 0x00, .a = 0xFF})
#define BUTTON_A            "\uE000"
#define BUTTON_B            "\uE001"
#define BUTTON_X            "\uE002"
#define BUTTON_DPAD         "\uE07D"

const std::string imagePath = "fs:/vol/external01/wiiu/screenshots/";
FC_Font *font;
SDL_Texture *blackTexture;

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

bool fileEndsWith(const std::string &filename, const std::string &extension) {
    return filename.size() >= extension.size() && std::equal(extension.rbegin(), extension.rend(), filename.rbegin());
}

void drawRectFilled(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color color) {
    SDL_Rect rect{x, y, w, h};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void drawRect(SDL_Renderer *renderer, int x, int y, int w, int h, int borderSize, SDL_Color color) {
    drawRectFilled(renderer, x, y, w, borderSize, color);
    drawRectFilled(renderer, x, y + h - borderSize, w, borderSize, color);
    drawRectFilled(renderer, x, y, borderSize, h, color);
    drawRectFilled(renderer, x + w - borderSize, y, borderSize, h, color);
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

    blackTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 800, 600);
    SDL_SetRenderTarget(renderer, blackTexture);
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
            imgPair.textureTV = surfaceTV ? SDL_CreateTextureFromSurface(renderer, surfaceTV) : blackTexture;
            imgPair.textureDRC = surfaceDRC ? SDL_CreateTextureFromSurface(renderer, surfaceDRC) : blackTexture;

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
    int totalHeight = GRID_SIZE * (IMAGE_WIDTH + SEPARATION) - SEPARATION + MARGIN_TOP + MARGIN_BOTTOM;

    int offsetX = (SCREEN_WIDTH - totalWidth) / 2 - IMAGE_WIDTH / 3;
    int offsetY = (SCREEN_HEIGHT - totalHeight) / 2 + MARGIN_TOP;

    std::vector<ImagesPair> images = scanImagePairsInSubfolders(imagePath, renderer, offsetX, offsetY);

    int selectedImageIndex = 0;
    int scrollOffsetY = 0;

    Input input;

    MenuState state = MenuState::ShowAllImages;
    SingleImageState singleImageState = SingleImageState::TV;
    std::string titleText = "All images";

    SDL_Texture *arrowTexture = IMG_LoadTexture(renderer, "romfs:/arrow_image.png");
    SDL_Rect arrowRect = {0, (SCREEN_HEIGHT / 2) - 145, 290, 290};

    SDL_Rect fullscreenRect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

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
                        scrollOffsetY += IMAGE_WIDTH + SEPARATION;
                    }
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_DOWN)) {
                if (state != MenuState::ShowSingleImage) {
                    if (selectedImageIndex < static_cast<int>(images.size()) - GRID_SIZE) {
                        selectedImageIndex += GRID_SIZE;
                        scrollOffsetY -= IMAGE_WIDTH + SEPARATION;
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
                                    if (image.textureTV && image.textureTV != blackTexture) {
                                        SDL_DestroyTexture(image.textureTV);
                                        image.textureTV = nullptr;
                                    }
                                    if (image.textureDRC && image.textureDRC != blackTexture) {
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
        if (state != MenuState::ShowSingleImage) {
            if (images.empty()) {
                FC_Draw(font, renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, "No images found");
            } else {
                for (size_t i = 0; i < images.size(); ++i) {
                    SDL_Rect destRectTV = {images[i].x, images[i].y + scrollOffsetY, IMAGE_WIDTH, IMAGE_HEIGHT};
                    SDL_Rect destRectDRC = {images[i].x + IMAGE_WIDTH / 2, images[i].y + scrollOffsetY + IMAGE_WIDTH / 2, IMAGE_WIDTH / 2, IMAGE_HEIGHT / 2};
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
                }

                drawRect(renderer, images[selectedImageIndex].x, images[selectedImageIndex].y + scrollOffsetY, IMAGE_WIDTH, IMAGE_HEIGHT * 1.5, 7, SCREEN_COLOR_YELLOW);
            }
            if (state == MenuState::ShowAllImages) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 139, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 0x7F, 0x00, 0x00, 0xFF);
            }

            SDL_Rect hudRect = {SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION, 0, IMAGE_WIDTH, SCREEN_HEIGHT};
            SDL_RenderFillRect(renderer, &hudRect);

            SDL_SetRenderDrawColor(renderer, 173, 216, 230, 255);
            // Draw hud text
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, 5, "Album");
            std::string allImagesCount = "(" + std::to_string(images.size()) + ")";
            SDL_Rect separatorLine = {SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, 50, IMAGE_WIDTH, 1};
            SDL_RenderFillRect(renderer, &separatorLine);
            switch (state) {
                case MenuState::ShowAllImages:
                    titleText = "All images";
                    break;
                case MenuState::ShowSingleImage:
                    titleText = "Single image";
                    break;
                case MenuState::SelectImagesDelete:
                    titleText = "Select images";
                    break;
                default:
                    titleText = "Unknown";
                    break;
            }
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, 50, titleText.c_str());
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, 90, allImagesCount.c_str());
            // Draw controls at the bottom of the hud rect
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, SCREEN_HEIGHT - 170, BUTTON_DPAD ": Move");
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, SCREEN_HEIGHT - 130, BUTTON_A ": Select");
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, SCREEN_HEIGHT - 90, BUTTON_B ": Back");
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_WIDTH - SEPARATION + 5, SCREEN_HEIGHT - 50, BUTTON_X ": Delete");

            SDL_RenderPresent(renderer);
        } else if (state == MenuState::ShowSingleImage && selectedImageIndex >= 0 && selectedImageIndex < static_cast<int>(images.size())) {
            switch (singleImageState) {
                case SingleImageState::TV:
                    arrowRect.x = SCREEN_WIDTH - IMAGE_WIDTH;
                    SDL_RenderCopy(renderer, images[selectedImageIndex].textureTV, nullptr, &fullscreenRect);
                    SDL_RenderCopy(renderer, arrowTexture, nullptr, &arrowRect);
                    break;
                case SingleImageState::DRC:
                    arrowRect.x = 0;
                    SDL_RenderCopy(renderer, images[selectedImageIndex].textureDRC, nullptr, &fullscreenRect);
                    SDL_RenderCopyEx(renderer, arrowTexture, nullptr, &arrowRect, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
                    break;
                default:
                    arrowRect.x = SCREEN_WIDTH - IMAGE_WIDTH;
                    SDL_RenderCopy(renderer, images[selectedImageIndex].textureTV, nullptr, &fullscreenRect);
                    SDL_RenderCopy(renderer, arrowTexture, nullptr, &arrowRect);
                    break;
            }
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
    if (blackTexture) {
        SDL_DestroyTexture(blackTexture);
    }
    if (arrowTexture) {
        SDL_DestroyTexture(arrowTexture);
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
