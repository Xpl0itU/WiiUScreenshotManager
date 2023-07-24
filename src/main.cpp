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

bool endsWith(const std::string &str, const std::string &suffix) {
    if (str.length() < suffix.length()) {
        return false;
    }

    return str.substr(str.length() - suffix.length()) == suffix;
}

std::vector<ImagesPair> scanImagePairsInSubfolders(const std::string &directoryPath, SDL_Renderer *renderer, int offsetX, int offsetY) {
    std::vector<ImagesPair> imagePairs;
    int pairIndex = 1;

    if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath)) {
        return imagePairs; // Return an empty vector if the directory doesn't exist or is not a directory
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(directoryPath)) {
        if (!entry.is_regular_file()) {
            continue; // Skip if not a regular file
        }

        std::string extension = entry.path().extension().string();
        if (extension == ".jpg" || extension == ".png" || extension == ".bmp") {
            std::string filename = entry.path().filename().string();
            if (filename.rfind("DRC.", filename.size() - 5) != std::string::npos) {
                std::string tvFilename = filename;
                tvFilename.replace(filename.size() - extension.size() - 3, 3, "TV");
                std::string tvPath = entry.path().parent_path().string() + "/" + tvFilename;

                if (std::filesystem::exists(tvPath) && std::filesystem::exists(entry.path().string())) {
                    SDL_Surface *surfaceTV = IMG_Load(tvPath.c_str());
                    SDL_Surface *surfaceDRC = IMG_Load(entry.path().c_str());

                    if (!surfaceTV || !surfaceDRC) {
                        SDL_FreeSurface(surfaceTV);
                        SDL_FreeSurface(surfaceDRC);
                        continue;
                    }

                    ImagesPair imgPair;
                    imgPair.textureTV = SDL_CreateTextureFromSurface(renderer, surfaceTV);
                    imgPair.textureDRC = SDL_CreateTextureFromSurface(renderer, surfaceDRC);
                    SDL_FreeSurface(surfaceTV);
                    SDL_FreeSurface(surfaceDRC);

                    imgPair.x = offsetX + (pairIndex - 1) % GRID_SIZE * (IMAGE_SIZE + SEPARATION);
                    imgPair.y = offsetY + (pairIndex - 1) / GRID_SIZE * (IMAGE_SIZE + SEPARATION);

                    imgPair.selected = false;
                    imgPair.pathTV = tvPath;
                    imgPair.pathDRC = entry.path().string();

                    imagePairs.push_back(imgPair);
                    ++pairIndex;
                } else {
                    if (std::filesystem::exists(tvPath)) {
                        SDL_Surface *surfaceTV = IMG_Load(tvPath.c_str());
                        if (surfaceTV) {
                            ImagesPair imgPair;
                            imgPair.textureTV = SDL_CreateTextureFromSurface(renderer, surfaceTV);
                            imgPair.textureDRC = nullptr;
                            SDL_FreeSurface(surfaceTV);

                            imgPair.x = offsetX + (pairIndex - 1) % GRID_SIZE * (IMAGE_SIZE + SEPARATION);
                            imgPair.y = offsetY + (pairIndex - 1) / GRID_SIZE * (IMAGE_SIZE + SEPARATION);

                            imgPair.selected = false;
                            imgPair.pathTV = tvPath;
                            imgPair.pathDRC = "";

                            imagePairs.push_back(imgPair);
                            ++pairIndex;
                        }
                    } else if (std::filesystem::exists(entry.path().string())) {
                        SDL_Surface *surfaceDRC = IMG_Load(entry.path().c_str());
                        if (surfaceDRC) {
                            ImagesPair imgPair;
                            imgPair.textureDRC = SDL_CreateTextureFromSurface(renderer, surfaceDRC);
                            imgPair.textureTV = nullptr;
                            SDL_FreeSurface(surfaceDRC);

                            imgPair.x = offsetX + (pairIndex - 1) % GRID_SIZE * (IMAGE_SIZE + SEPARATION);
                            imgPair.y = offsetY + (pairIndex - 1) / GRID_SIZE * (IMAGE_SIZE + SEPARATION);

                            imgPair.selected = false;
                            imgPair.pathDRC = entry.path().string();
                            imgPair.pathTV = "";

                            imagePairs.push_back(imgPair);
                            ++pairIndex;
                        }
                    }
                }
            }
        }
    }

    return imagePairs;
}

int main() {
    State::init();
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

    int totalWidth = GRID_SIZE * (IMAGE_SIZE + SEPARATION) - SEPARATION;
    int totalHeight = GRID_SIZE * (IMAGE_SIZE + SEPARATION) - SEPARATION + MARGIN_TOP + MARGIN_BOTTOM;

    int offsetX = (SCREEN_WIDTH - totalWidth) / 2 - IMAGE_SIZE / 3;
    int offsetY = (SCREEN_HEIGHT - totalHeight) / 2 + MARGIN_TOP;

    std::vector<ImagesPair> images = scanImagePairsInSubfolders(imagePath, renderer, offsetX, offsetY);

    int selectedImageIndex = 0;
    int scrollOffsetY = 0;

    Input input;

    while (State::AppRunning()) {
        input.read();
        if (input.get(TRIGGER, PAD_BUTTON_ANY)) {
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
                } else {
                    if (images[selectedImageIndex].textureTV) {
                        SDL_DestroyTexture(images[selectedImageIndex].textureTV);
                        images[selectedImageIndex].textureTV = nullptr;
                    }
                    if (images[selectedImageIndex].textureDRC) {
                        SDL_DestroyTexture(images[selectedImageIndex].textureDRC);
                        images[selectedImageIndex].textureDRC = nullptr;
                    }
                    if (images[selectedImageIndex].pathTV != "") {
                        remove(images[selectedImageIndex].pathTV.c_str());
                    }
                    if (images[selectedImageIndex].pathDRC != "") {
                        remove(images[selectedImageIndex].pathDRC.c_str());
                    }
                    images.erase(std::remove_if(images.begin(), images.end(), [&](const ImagesPair &img) {
                                     return (img.pathDRC == images[selectedImageIndex].pathDRC) || (img.pathTV == images[selectedImageIndex].pathTV);
                                 }),
                                 images.end());
                }

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
        if (img.textureTV) {
            SDL_DestroyTexture(img.textureTV);
        }
        if (img.textureDRC) {
            SDL_DestroyTexture(img.textureDRC);
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    FC_FreeFont(font);
    font = NULL;
    SDL_Quit();
    IMG_Quit();

    State::shutdown();

    return 0;
}
