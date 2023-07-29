#include <Button.h>
#include <ImagePairScreen.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL_FontCache.h>
#include <StateUtils.h>
#include <algorithm>
#include <chrono>
#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <filesystem>
#include <iostream>
#include <nn/erreula.h>
#include <romfs-wiiu.h>
#include <sndcore2/core.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <vpad/input.h>

#define GRID_SIZE           4
#define IMAGE_WIDTH         SCREEN_WIDTH / GRID_SIZE / 2
#define IMAGE_HEIGHT        SCREEN_HEIGHT / GRID_SIZE / 2
#define SEPARATION          IMAGE_WIDTH / 4
#define FONT_SIZE           36
#define TRAIL_LENGTH        20
#define SCREEN_COLOR_BLACK  ((SDL_Color){.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_WHITE  ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF})
#define SCREEN_COLOR_YELLOW ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_D_RED  ((SDL_Color){.r = 0x7F, .g = 0x00, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_GRAY   ((SDL_Color){.r = 0x6A, .g = 0x6A, .b = 0x6A, .a = 0xFF})
#define BUTTON_A            "\uE000"
#define BUTTON_B            "\uE001"
#define BUTTON_X            "\uE002"
#define BUTTON_DPAD         "\uE07D"

enum class MenuState {
    ShowAllImages,
    SelectImagesDelete,
    ShowSingleImage,
};

struct Texture {
    SDL_Texture *texture;
    SDL_Rect rect;
};

struct Particle {
    float x, y;
    float vx, vy;
    int lifetime, size;
    SDL_Rect rect;
};

const std::string imagePath = "fs:/vol/external01/wiiu/screenshots/";
FC_Font *font = nullptr;
SDL_Texture *orbTexture = nullptr;
SDL_Texture *particleTexture = nullptr;
SDL_Texture *ghostPointerTexture = nullptr;
SDL_Texture *cornerButtonTexture = nullptr;
SDL_Texture *largeCornerButtonTexture = nullptr;
SDL_Texture *arrowTexture = nullptr;
Texture blackTexture;
Texture headerTexture;
Texture backgroundTexture;
Texture backGraphicTexture;
Texture pointerTexture;

std::vector<Particle> particles;
std::vector<SDL_Point> pointerTrail;

bool fileEndsWith(const std::string &filename, const std::string &extension) {
    return filename.size() >= extension.size() && std::equal(extension.rbegin(), extension.rend(), filename.rbegin());
}

bool isPointInsideRect(int x, int y, const SDL_Rect &rect) {
    return (x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h);
}

bool isPointInsideRect(int x, int y, int w, int h, int rectX, int rectY) {
    return (x >= rectX && x <= rectX + w && y >= rectY && y <= rectY + h);
}

bool isFirstRow(int index) {
    return index < GRID_SIZE;
}

bool isLastRow(int index, int imageCount) {
    int lastRow = (imageCount - 1) / GRID_SIZE;
    return index >= (lastRow * GRID_SIZE);
}

Particle generateParticle(float x, float y) {
    Particle particle;
    particle.x = x;
    particle.y = y;
    particle.vx = static_cast<float>(rand() % 3 - 1);
    particle.vy = static_cast<float>(rand() % 3 - 1);
    particle.lifetime = rand() % 60 + 60;
    particle.size = (rand() % 20) + 10;

    return particle;
}

void initializeGhostPointerTexture(SDL_Renderer *renderer) {
    ghostPointerTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, pointerTexture.rect.w, pointerTexture.rect.h);

    SDL_SetRenderTarget(renderer, ghostPointerTexture);
    SDL_SetRenderDrawColor(renderer, 144, 238, 144, 100);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
}

void renderGhostPointers(SDL_Renderer *renderer, const std::vector<SDL_Point> &trail) {
    if (!ghostPointerTexture) return;

    SDL_SetTextureBlendMode(ghostPointerTexture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(ghostPointerTexture, 100);

    for (const auto &point : trail) {
        SDL_Rect destRect = {point.x, point.y, pointerTexture.rect.w, pointerTexture.rect.h};
        SDL_RenderCopy(renderer, ghostPointerTexture, nullptr, &destRect);
    }
}

bool isImageVisible(const ImagesPair &image, int scrollOffsetY) {
    int imageTop = headerTexture.rect.h + image.y + scrollOffsetY;
    int imageBottom = imageTop + IMAGE_HEIGHT + IMAGE_HEIGHT / 2;

    int screenTop = headerTexture.rect.h;
    int screenBottom = SCREEN_HEIGHT;

    return (imageBottom >= screenTop) && (imageTop <= screenBottom);
}

SDL_Texture *loadTexture(SDL_Renderer *renderer, const std::string path) {
    SDL_Texture *texture = IMG_LoadTexture(renderer, path.c_str());
    if (texture) {
        return texture;
    }
    return blackTexture.texture;
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
    FSClient *fsClient = (FSClient *) MEMAllocFromDefaultHeap(sizeof(FSClient));
    FSAddClient(fsClient, FS_ERROR_FLAG_NONE);

    nn::erreula::CreateArg createArg;
    createArg.region = nn::erreula::RegionType::USA;
    createArg.language = nn::erreula::LangType::English;
    createArg.workMemory = MEMAllocFromDefaultHeap(nn::erreula::GetWorkMemorySize());
    createArg.fsClient = fsClient;
    if (!nn::erreula::Create(createArg)) {
        return false;
    }

    nn::erreula::AppearArg appearArg;
    appearArg.errorArg.errorType = nn::erreula::ErrorType::Message2Button;
    appearArg.errorArg.renderTarget = nn::erreula::RenderTarget::Both;
    appearArg.errorArg.controllerType = nn::erreula::ControllerType::DrcGamepad;
    appearArg.errorArg.errorMessage = u"Are you sure you want to delete the selected images?";
    appearArg.errorArg.button1Label = u"Cancel";
    appearArg.errorArg.button2Label = u"Confirm";
    nn::erreula::AppearErrorViewer(appearArg);
    while (State::AppRunning()) {
        VPADStatus vpadStatus;
        VPADRead(VPAD_CHAN_0, &vpadStatus, 1, nullptr);
        VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpadStatus.tpNormal, &vpadStatus.tpNormal);

        nn::erreula::ControllerInfo controllerInfo;
        controllerInfo.vpad = &vpadStatus;
        controllerInfo.kpad[0] = nullptr;
        controllerInfo.kpad[1] = nullptr;
        controllerInfo.kpad[2] = nullptr;
        controllerInfo.kpad[3] = nullptr;
        nn::erreula::Calc(controllerInfo);

        if (nn::erreula::IsDecideSelectButtonError()) {
            nn::erreula::DisappearErrorViewer();
            break;
        }
        nn::erreula::DrawTV();
        nn::erreula::DrawDRC();
        SDL_RenderPresent(renderer);
    }
    nn::erreula::Destroy();
    MEMFreeToDefaultHeap(createArg.workMemory);

    FSDelClient(fsClient, FS_ERROR_FLAG_NONE);
    MEMFreeToDefaultHeap(fsClient);

    int32_t resultCode = nn::erreula::GetResultCode();

    return static_cast<bool>(resultCode);
}

std::vector<ImagesPair> scanImagePairsInSubfolders(const std::string &directoryPath, int offsetX, int offsetY) {
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
        std::string baseFilename = filename.substr(0, filename.find_last_of('_'));
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
            ImagesPair imgPair;

            imgPair.textureTV = nullptr;
            imgPair.textureDRC = nullptr;

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
    FSInit();
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
        SDL_Quit();
        return 1;
    }
    SDL_RWops *rw = SDL_RWFromConstMem(ttf, size);
    if (!FC_LoadFont_RW(font, renderer, rw, 1, FONT_SIZE, SCREEN_COLOR_WHITE, TTF_STYLE_NORMAL)) {
        SDL_Quit();
        return 1;
    }

    int totalWidth = GRID_SIZE * (IMAGE_WIDTH + SEPARATION) - SEPARATION;
    int totalHeight = GRID_SIZE * (IMAGE_WIDTH + SEPARATION) - SEPARATION;

    int offsetX = (SCREEN_WIDTH - totalWidth) / 2;
    int offsetY = (SCREEN_HEIGHT - totalHeight) / 2;

    blackTexture.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 800, 600);
    SDL_SetRenderTarget(renderer, blackTexture.texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    std::vector<ImagesPair> images = scanImagePairsInSubfolders(imagePath, offsetX, offsetY);

    int selectedImageIndex = 0;
    int scrollOffsetY = 0;

    MenuState state = MenuState::ShowAllImages;

    arrowTexture = IMG_LoadTexture(renderer, "romfs:/arrow_image.png");
    if (!arrowTexture) {
        SDL_Quit();
        return 1;
    }
    backgroundTexture.texture = IMG_LoadTexture(renderer, "romfs:/backdrop.png");
    if (!backgroundTexture.texture) {
        SDL_Quit();
        return 1;
    }
    cornerButtonTexture = IMG_LoadTexture(renderer, "romfs:/corner-button.png");
    if (!cornerButtonTexture) {
        SDL_Quit();
        return 1;
    }
    largeCornerButtonTexture = IMG_LoadTexture(renderer, "romfs:/large-corner-button.png");
    if (!largeCornerButtonTexture) {
        SDL_Quit();
        return 1;
    }
    backGraphicTexture.texture = IMG_LoadTexture(renderer, "romfs:/back_graphic.png");
    if (!backGraphicTexture.texture) {
        SDL_Quit();
        return 1;
    }
    headerTexture.texture = IMG_LoadTexture(renderer, "romfs:/header.png");
    if (!headerTexture.texture) {
        SDL_Quit();
        return 1;
    }
    orbTexture = IMG_LoadTexture(renderer, "romfs:/orb.png");
    if (!orbTexture) {
        SDL_Quit();
        return 1;
    }
    particleTexture = IMG_LoadTexture(renderer, "romfs:/orb.png");
    if (!particleTexture) {
        SDL_Quit();
        return 1;
    }
    pointerTexture.texture = IMG_LoadTexture(renderer, "romfs:/orb.png");
    if (!pointerTexture.texture) {
        SDL_Quit();
        return 1;
    }

    backgroundTexture.rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    backGraphicTexture.rect = {0, SCREEN_HEIGHT - 128, 128, 128};
    headerTexture.rect = {0, 0, SCREEN_WIDTH, 256};
    pointerTexture.rect = {0, 0, 30, 30};

    bool pressedBack = false;
    bool deleteImagesSelected = false;

    Button cornerButton(0, SCREEN_HEIGHT - 256, 256, 256, "", cornerButtonTexture, font, SDL_CONTROLLER_BUTTON_B, SCREEN_COLOR_WHITE);
    cornerButton.setOnClick([&]() {
        pressedBack = true;
    });

    Button largeCornerButton(SCREEN_WIDTH - 512, 0, 512, 256, BUTTON_X " Select", largeCornerButtonTexture, font, SDL_CONTROLLER_BUTTON_X, SCREEN_COLOR_BLACK);
    largeCornerButton.setOnClick([&]() {
        if (images.empty()) {
            return;
        }
        if (state == MenuState::ShowAllImages) {
            state = MenuState::SelectImagesDelete;
        } else if (state == MenuState::SelectImagesDelete) {
            deleteImagesSelected = true;
        }
    });
    largeCornerButton.setFlip((SDL_RendererFlip) (SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));

    SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_JoystickOpen(i) == nullptr) {
            SDL_Quit();
            return 1;
        }
    }

    bool isCameraScrolling = false;
    bool selectedImage = false;
    int initialTouchY = -1;
    int initialSelectedImageIndex;
    SDL_Event event;
    SDL_Joystick *j;
    ImagePairScreen imagePairScreen(nullptr, arrowTexture, renderer);
    initializeGhostPointerTexture(renderer);
    while (State::AppRunning()) {
        deleteImagesSelected = false;
        pressedBack = false;
        while (SDL_PollEvent(&event)) {
            int x, y;
            cornerButton.handleEvent(event);
            largeCornerButton.handleEvent(event);
            if (state == MenuState::ShowSingleImage) {
                imagePairScreen.handleEvent(event);
            }
            switch (event.type) {
                case SDL_JOYDEVICEADDED:
                    j = SDL_JoystickOpen(event.jdevice.which);
                    break;
                case SDL_JOYDEVICEREMOVED:
                    j = SDL_JoystickFromInstanceID(event.jdevice.which);
                    if (j && SDL_JoystickGetAttached(j)) {
                        SDL_JoystickClose(j);
                    }
                    break;
                case SDL_JOYBUTTONDOWN:
                    switch (event.jbutton.button) {
                        case SDL_CONTROLLER_BUTTON_A:
                            if (!images.empty()) {
                                if (state == MenuState::SelectImagesDelete) {
                                    images[selectedImageIndex].selected = !images[selectedImageIndex].selected;
                                } else if (state == MenuState::ShowAllImages) {
                                    state = MenuState::ShowSingleImage;
                                    imagePairScreen.setImagePair(&images[selectedImageIndex]);
                                }
                            }
                            break;
                        case 0xd: // SDL_CONTROLLER_BUTTON_DPAD_UP
                            if (state != MenuState::ShowSingleImage) {
                                if (selectedImageIndex >= GRID_SIZE) {
                                    selectedImageIndex -= GRID_SIZE;
                                    if (isFirstRow(selectedImageIndex) || !isImageVisible(images[selectedImageIndex], scrollOffsetY)) {
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
                            break;
                        case 0xf: // SDL_CONTROLLER_BUTTON_DPAD_DOWN
                            if (state != MenuState::ShowSingleImage) {
                                if (selectedImageIndex < static_cast<int>(images.size()) - GRID_SIZE) {
                                    selectedImageIndex += GRID_SIZE;
                                    if (isLastRow(selectedImageIndex, images.size()) || !isImageVisible(images[selectedImageIndex], scrollOffsetY)) {
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
                            break;
                        case 0xc: // SDL_CONTROLLER_BUTTON_DPAD_LEFT
                            if (state != MenuState::ShowSingleImage) {
                                if (selectedImageIndex % GRID_SIZE != 0) {
                                    selectedImageIndex--;
                                }
                            }
                            break;
                        case 0xe: // SDL_CONTROLLER_BUTTON_DPAD_RIGHT
                            if (state != MenuState::ShowSingleImage) {
                                if (selectedImageIndex % GRID_SIZE != GRID_SIZE - 1 && selectedImageIndex < static_cast<int>(images.size()) - 1) {
                                    selectedImageIndex++;
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    break;
                case SDL_FINGERDOWN:
                    x = event.tfinger.x * SCREEN_WIDTH;
                    y = event.tfinger.y * SCREEN_HEIGHT;
                    pointerTexture.rect.x = x - pointerTexture.rect.w / 2;
                    pointerTexture.rect.y = y - pointerTexture.rect.h / 2;
                    initialTouchY = y;
                    initialSelectedImageIndex = selectedImageIndex;
                    pointerTrail.clear();
                    if (state == MenuState::ShowAllImages) {
                        for (auto image : images) {
                            if (isPointInsideRect(x, y, IMAGE_WIDTH, IMAGE_HEIGHT, image.x, headerTexture.rect.h + image.y + scrollOffsetY)) {
                                selectedImageIndex = static_cast<int>(std::distance(images.begin(), std::find(images.begin(), images.end(), image)));
                                initialSelectedImageIndex = selectedImageIndex;
                                break;
                            }
                        }
                        if (!selectedImage) {
                            isCameraScrolling = true;
                        }
                    } else if (state == MenuState::SelectImagesDelete) {
                        for (auto image : images) {
                            if (isPointInsideRect(x, y, IMAGE_WIDTH, IMAGE_HEIGHT, image.x, headerTexture.rect.h + image.y + scrollOffsetY)) {
                                selectedImageIndex = static_cast<int>(std::distance(images.begin(), std::find(images.begin(), images.end(), image)));
                                images[selectedImageIndex].selected = !images[selectedImageIndex].selected;
                                break;
                            }
                        }
                        isCameraScrolling = true;
                    }
                    break;
                case SDL_FINGERUP:
                    x = event.tfinger.x * SCREEN_WIDTH;
                    y = event.tfinger.y * SCREEN_HEIGHT;
                    initialTouchY = -1;
                    isCameraScrolling = false;
                    selectedImage = false;
                    if (state == MenuState::ShowAllImages) {
                        if (isPointInsideRect(x, y, IMAGE_WIDTH, IMAGE_HEIGHT, images[selectedImageIndex].x, headerTexture.rect.h + images[selectedImageIndex].y + scrollOffsetY)) {
                            state = MenuState::ShowSingleImage;
                            imagePairScreen.setImagePair(&images[selectedImageIndex]);
                            selectedImage = true;
                        }
                    }
                    break;
                case SDL_FINGERMOTION:
                    x = event.tfinger.x * SCREEN_WIDTH;
                    y = event.tfinger.y * SCREEN_HEIGHT;
                    pointerTexture.rect.x = x - pointerTexture.rect.w / 2;
                    pointerTexture.rect.y = y - pointerTexture.rect.h / 2;
                    pointerTrail.push_back({pointerTexture.rect.x, pointerTexture.rect.y});

                    if (pointerTrail.size() > TRAIL_LENGTH) {
                        pointerTrail.erase(pointerTrail.begin());
                    }
                    if (initialTouchY != -1 && isCameraScrolling) {
                        int touchDeltaY = y - initialTouchY;
                        scrollOffsetY += touchDeltaY;
                        int maxScrollOffsetY = (GRID_SIZE - 1) * (IMAGE_HEIGHT + SEPARATION);
                        if (scrollOffsetY > 0) {
                            scrollOffsetY = 0;
                        } else if (scrollOffsetY < -maxScrollOffsetY) {
                            scrollOffsetY = -maxScrollOffsetY;
                        }

                        int rowsScrolled = std::abs(scrollOffsetY) / (IMAGE_HEIGHT + SEPARATION);
                        int newRow = initialSelectedImageIndex / GRID_SIZE;
                        if (scrollOffsetY > 0) {
                            newRow -= rowsScrolled;
                        } else if (scrollOffsetY < 0) {
                            newRow += rowsScrolled - 1;
                        }

                        newRow = std::max(0, std::min(newRow, (int) ((images.size() - 1) / GRID_SIZE)));

                        selectedImageIndex = newRow * GRID_SIZE + (initialSelectedImageIndex % GRID_SIZE);

                        initialTouchY = y;
                    }
                    break;
                default:
                    break;
            }
        }

        if (deleteImagesSelected) {
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

        if (pressedBack) {
            state = MenuState::ShowAllImages;
            if (std::any_of(images.begin(), images.end(), [](const ImagesPair &image) { return image.selected; })) {
                for (auto &image : images) {
                    image.selected = false;
                }
            }
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, backgroundTexture.texture, nullptr, &backgroundTexture.rect);
        if (rand() % 10 == 0) {
            float x = static_cast<float>(rand() % SCREEN_WIDTH);
            float y = static_cast<float>(rand() % SCREEN_HEIGHT);
            particles.push_back(generateParticle(x, y));
        }
        for (auto it = particles.begin(); it != particles.end();) {
            Particle &particle = *it;
            particle.x += particle.vx;
            particle.y += particle.vy;
            particle.lifetime--;
            particle.rect = {static_cast<int>(particle.x), static_cast<int>(particle.y), particle.size, particle.size};

            if (particle.lifetime <= 0) {
                it = particles.erase(it);
            } else {
                ++it;
            }
        }
        for (const Particle &particle : particles) {
            SDL_RenderCopy(renderer, particleTexture, nullptr, &particle.rect);
        }
        if (state != MenuState::ShowSingleImage) {
            if (images.empty()) {
                FC_Draw(font, renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, "No images found");
            } else {
                SDL_SetTextureColorMod(headerTexture.texture, 0, 0, 147);
                SDL_SetTextureBlendMode(headerTexture.texture, SDL_BLENDMODE_BLEND);
                SDL_RenderCopy(renderer, headerTexture.texture, nullptr, &headerTexture.rect);
                FC_DrawColor(font, renderer, headerTexture.rect.x + (headerTexture.rect.w / 2), (headerTexture.rect.y + (headerTexture.rect.h / 2)) - 100, SCREEN_COLOR_WHITE, "Album");
                for (size_t i = 0; i < images.size(); ++i) {
                    if (isImageVisible(images[i], scrollOffsetY)) {
                        if ((images[i].textureTV == nullptr)) {
                            images[i].textureTV = loadTexture(renderer, images[i].pathTV);
                        }
                        if ((images[i].textureDRC == nullptr)) {
                            images[i].textureDRC = loadTexture(renderer, images[i].pathDRC);
                        }
                        SDL_Rect destRectTV = {images[i].x, headerTexture.rect.h + images[i].y + scrollOffsetY, IMAGE_WIDTH, IMAGE_HEIGHT};
                        SDL_Rect destRectDRC = {images[i].x + IMAGE_WIDTH / 2, headerTexture.rect.h + images[i].y + scrollOffsetY + IMAGE_WIDTH / 2, IMAGE_WIDTH / 2, IMAGE_HEIGHT / 2};
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
                            drawOrb(renderer, images[i].x - 10, headerTexture.rect.h + images[i].y + scrollOffsetY - 10, 60, images[i].selected);
                        }
                    } else {
                        if (images[i].textureTV && images[i].textureTV != blackTexture.texture) {
                            SDL_DestroyTexture(images[i].textureTV);
                            images[i].textureTV = nullptr;
                        }
                        if (images[i].textureDRC && images[i].textureDRC != blackTexture.texture) {
                            SDL_DestroyTexture(images[i].textureDRC);
                            images[i].textureDRC = nullptr;
                        }
                    }
                }

                SDL_SetTextureBlendMode(largeCornerButtonTexture, SDL_BLENDMODE_BLEND);
                if (state == MenuState::SelectImagesDelete) {
                    SDL_SetTextureColorMod(largeCornerButtonTexture, 255, 0, 0);
                    SDL_SetTextureBlendMode(backGraphicTexture.texture, SDL_BLENDMODE_BLEND);
                    cornerButton.render(renderer);
                    SDL_RenderCopy(renderer, backGraphicTexture.texture, nullptr, &backGraphicTexture.rect);
                    largeCornerButton.setTextColor(SCREEN_COLOR_WHITE);
                    largeCornerButton.setText(BUTTON_X " Delete");
                } else {
                    SDL_SetTextureColorMod(largeCornerButtonTexture, 255, 255, 255);
                    largeCornerButton.setTextColor(SCREEN_COLOR_BLACK);
                    largeCornerButton.setText(BUTTON_X " Select");
                }
                largeCornerButton.render(renderer);
                drawRect(renderer, images[selectedImageIndex].x, headerTexture.rect.h + images[selectedImageIndex].y + scrollOffsetY, IMAGE_WIDTH, IMAGE_HEIGHT * 1.5, 7, SCREEN_COLOR_YELLOW);
            }
            if (isCameraScrolling) {
                renderGhostPointers(renderer, pointerTrail);
                SDL_SetTextureBlendMode(pointerTexture.texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureColorMod(pointerTexture.texture, 144, 238, 144);
                SDL_RenderCopy(renderer, pointerTexture.texture, nullptr, &pointerTexture.rect);
            }
            SDL_RenderPresent(renderer);
        } else if (state == MenuState::ShowSingleImage && selectedImageIndex >= 0 && selectedImageIndex < static_cast<int>(images.size())) {
            imagePairScreen.render();
            SDL_SetTextureBlendMode(backGraphicTexture.texture, SDL_BLENDMODE_BLEND);
            cornerButton.render(renderer);
            SDL_RenderCopy(renderer, backGraphicTexture.texture, nullptr, &backGraphicTexture.rect);
            if (isCameraScrolling) {
                renderGhostPointers(renderer, pointerTrail);
                SDL_SetTextureBlendMode(pointerTexture.texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureColorMod(pointerTexture.texture, 144, 238, 144);
                SDL_RenderCopy(renderer, pointerTexture.texture, nullptr, &pointerTexture.rect);
            }
            SDL_RenderPresent(renderer);
        }
        cornerButton.update();
        largeCornerButton.update();
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
    if (arrowTexture) {
        SDL_DestroyTexture(arrowTexture);
    }
    if (backgroundTexture.texture) {
        SDL_DestroyTexture(backgroundTexture.texture);
    }
    if (cornerButtonTexture) {
        SDL_DestroyTexture(cornerButtonTexture);
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
    if (particleTexture) {
        SDL_DestroyTexture(particleTexture);
    }
    if (largeCornerButtonTexture) {
        SDL_DestroyTexture(largeCornerButtonTexture);
    }
    if (pointerTexture.texture) {
        SDL_DestroyTexture(pointerTexture.texture);
    }
    if (ghostPointerTexture) {
        SDL_DestroyTexture(ghostPointerTexture);
    }

    FC_FreeFont(font);
    font = nullptr;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    IMG_Quit();
    FSShutdown();

    romfsExit();

    State::shutdown();

    return 0;
}
