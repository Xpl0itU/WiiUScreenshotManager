#include <InputUtils.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_FontCache.h>
#include <StateUtils.h>
#include <algorithm>
#include <coreinit/memory.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#define SCREEN_WIDTH       1920
#define SCREEN_HEIGHT      1080
#define GRID_SIZE          3
#define IMAGE_SIZE         SCREEN_WIDTH / GRID_SIZE / 2
#define SEPARATION         IMAGE_SIZE / 3
#define MARGIN_TOP         200
#define MARGIN_BOTTOM      50
#define FONT_SIZE          28
#define SCREEN_COLOR_WHITE ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF})
#define BUTTON_A           "\uE000"
#define BUTTON_B           "\uE001"
#define BUTTON_X           "\uE002"
#define BUTTON_DPAD        "\uE07D"

const std::string imagePath = "fs:/vol/external01/wiiu/screenshots/";
FC_Font *font;

struct ImagesPair {
    SDL_Texture *textureTV;
    SDL_Texture *textureDRC;
    int x, y;
    bool selected;
    std::string pathTV;
    std::string pathDRC;
};

enum class MenuState {
    ShowAllImages,
    SelectImagesDelete,
    ShowSingleImage,
};

bool fileEndsWith(const std::string &filename, const std::string &extension) {
    return filename.size() >= extension.size() && std::equal(extension.rbegin(), extension.rend(), filename.rbegin());
}

bool showConfirmationDialog(SDL_Renderer *renderer) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(renderer, nullptr);

    FC_DrawColor(font, renderer, SCREEN_WIDTH / 3, SCREEN_HEIGHT / 2 - 50, SCREEN_COLOR_WHITE, "Are you sure you want to delete the selected images?");
    FC_DrawColor(font, renderer, SCREEN_WIDTH / 3, SCREEN_HEIGHT / 2 + 50, SCREEN_COLOR_WHITE, BUTTON_A "Confirm"
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

    for (const auto &[baseFilename, paths] : baseFilenames) {
        std::string tvPath = paths.first;
        std::string drcPath = paths.second;

        if ((tvPath.empty() || std::filesystem::exists(tvPath)) || (drcPath.empty() || std::filesystem::exists(drcPath))) {
            SDL_Surface *surfaceTV = tvPath.empty() ? nullptr : IMG_Load(tvPath.c_str());
            SDL_Surface *surfaceDRC = drcPath.empty() ? nullptr : IMG_Load(drcPath.c_str());

            ImagesPair imgPair;
            imgPair.textureTV = surfaceTV ? SDL_CreateTextureFromSurface(renderer, surfaceTV) : nullptr;
            imgPair.textureDRC = surfaceDRC ? SDL_CreateTextureFromSurface(renderer, surfaceDRC) : nullptr;

            if (surfaceTV) SDL_FreeSurface(surfaceTV);
            if (surfaceDRC) SDL_FreeSurface(surfaceDRC);

            imgPair.x = offsetX + (pairIndex - 1) % GRID_SIZE * (IMAGE_SIZE + SEPARATION);
            imgPair.y = offsetY + (pairIndex - 1) / GRID_SIZE * (IMAGE_SIZE + SEPARATION);

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
    State::init();
    SDL_Init(SDL_INIT_VIDEO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP);

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

    int totalWidth = GRID_SIZE * (IMAGE_SIZE + SEPARATION) - SEPARATION;
    int totalHeight = GRID_SIZE * (IMAGE_SIZE + SEPARATION) - SEPARATION + MARGIN_TOP + MARGIN_BOTTOM;

    int offsetX = (SCREEN_WIDTH - totalWidth) / 2 - IMAGE_SIZE / 3;
    int offsetY = (SCREEN_HEIGHT - totalHeight) / 2 + MARGIN_TOP;

    std::vector<ImagesPair> images = scanImagePairsInSubfolders(imagePath, renderer, offsetX, offsetY);

    int selectedImageIndex = 0;
    int scrollOffsetY = 0;

    Input input;

    MenuState state = MenuState::ShowAllImages;
    std::string titleText = "All images";

    while (State::AppRunning()) {
        input.read();
        if (input.get(TRIGGER, PAD_BUTTON_ANY)) {
            if (input.get(TRIGGER, PAD_BUTTON_A)) {
                if (!images.empty()) {
                    images[selectedImageIndex].selected = !images[selectedImageIndex].selected;
                }
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
                if (state == MenuState::ShowAllImages) {
                    state = MenuState::SelectImagesDelete;
                } else if (state == MenuState::SelectImagesDelete) {
                    if (showConfirmationDialog(renderer)) {
                        if (std::any_of(images.begin(), images.end(), [](const ImagesPair &image) { return image.selected; })) {
                            for (auto &image : images) {
                                if (image.selected) {
                                    if (image.textureTV) {
                                        SDL_DestroyTexture(image.textureTV);
                                        image.textureTV = nullptr;
                                    }
                                    if (image.textureDRC) {
                                        SDL_DestroyTexture(image.textureDRC);
                                        image.textureDRC = nullptr;
                                    }
                                    if (image.pathTV != "") {
                                        remove(image.pathTV.c_str());
                                    }
                                    if (image.pathDRC != "") {
                                        remove(image.pathDRC.c_str());
                                    }
                                    images.erase(std::remove_if(images.begin(), images.end(), [&](const ImagesPair &img) {
                                                     return (img.pathDRC == image.pathDRC) || (img.pathTV == image.pathTV);
                                                 }),
                                                 images.end());
                                }
                            }
                            int totalImages = static_cast<int>(images.size());
                            for (int i = 0; i < totalImages; ++i) {
                                int row = i / GRID_SIZE;
                                int col = i % GRID_SIZE;
                                images[i].x = offsetX + col * (IMAGE_SIZE + SEPARATION);
                                images[i].y = offsetY + row * (IMAGE_SIZE + SEPARATION);
                            }
                        }
                        state = MenuState::ShowAllImages;
                    }
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_B)) {
                state = MenuState::ShowAllImages;
                selectedImageIndex = 0;
                if (std::any_of(images.begin(), images.end(), [](const ImagesPair &image) { return image.selected; })) {
                    for (auto &image : images) {
                        image.selected = false;
                    }
                }
            } else if (input.get(TRIGGER, PAD_BUTTON_A)) {
                if (state == MenuState::ShowAllImages) {
                    state = MenuState::ShowSingleImage;
                } else if (state == MenuState::SelectImagesDelete) {
                    images[selectedImageIndex].selected = !images[selectedImageIndex].selected;
                }
            }
        }

        SDL_RenderClear(renderer);
        if (state != MenuState::ShowSingleImage) {
            if (images.empty()) {
                FC_Draw(font, renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, "No images found");
            } else {
                for (size_t i = 0; i < images.size(); ++i) {
                    SDL_Rect destRect = {images[i].x, images[i].y + scrollOffsetY, IMAGE_SIZE, IMAGE_SIZE};
                    if (images[i].selected) {
                        SDL_SetTextureColorMod(images[i].textureTV, 0, 255, 0);
                    } else {
                        SDL_SetTextureColorMod(images[i].textureTV, 255, 255, 255);
                    }
                    SDL_SetTextureBlendMode(images[i].textureTV, SDL_BLENDMODE_BLEND);
                    SDL_RenderCopy(renderer, images[i].textureTV, nullptr, &destRect);
                }

                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
                SDL_Rect outlineRect = {images[selectedImageIndex].x, images[selectedImageIndex].y + scrollOffsetY, IMAGE_SIZE + 5, IMAGE_SIZE + 5};
                SDL_RenderDrawRect(renderer, &outlineRect);
            }
            if (state == MenuState::ShowAllImages) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 139, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 0x7F, 0x00, 0x00, 0xFF);
            }

            SDL_Rect hudRect = {SCREEN_WIDTH - IMAGE_SIZE - SEPARATION, 0, IMAGE_SIZE, SCREEN_HEIGHT};
            SDL_RenderFillRect(renderer, &hudRect);

            SDL_SetRenderDrawColor(renderer, 173, 216, 230, 255);
            // Draw hud text
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 5, "Album");
            std::string allImagesCount = "(" + std::to_string(images.size()) + ")";
            SDL_Rect separatorLine = {SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 40, IMAGE_SIZE, 1};
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
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 50, titleText.c_str());
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, 80, allImagesCount.c_str());
            // Draw controls at the bottom of the hud rect
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 130, BUTTON_DPAD ": Move");
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 100, BUTTON_A ": Select");
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 70, BUTTON_B ": Back");
            FC_Draw(font, renderer, SCREEN_WIDTH - IMAGE_SIZE - SEPARATION + 5, SCREEN_HEIGHT - 40, BUTTON_X ": Delete");

            SDL_RenderPresent(renderer);
        } else if (state == MenuState::ShowSingleImage && selectedImageIndex >= 0 && selectedImageIndex < static_cast<int>(images.size())) {
            SDL_Rect fullscreenRect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
            SDL_RenderCopy(renderer, images[selectedImageIndex].textureTV, nullptr, &fullscreenRect);
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

    FC_FreeFont(font);
    font = NULL;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    IMG_Quit();

    State::shutdown();

    return 0;
}
