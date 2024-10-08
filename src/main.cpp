#include <Button.h>
#include <ImagePairScreen.h>
#include <MutexWrapper.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL_FontCache.h>
#include <algorithm>
#include <chrono>
#include <coreinit/filesystem.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <filesystem>
#include <future>
#include <iostream>
#include <nn/erreula.h>
#include <romfs-wiiu.h>
#include <sndcore2/core.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <vector>
#include <vpad/input.h>

#define GRID_SIZE            4
#define IMAGE_WIDTH          SCREEN_WIDTH / GRID_SIZE / 2
#define IMAGE_HEIGHT         SCREEN_HEIGHT / GRID_SIZE / 2
#define SEPARATION           IMAGE_WIDTH / 4
#define FONT_SIZE            36
#define TRAIL_LENGTH         20
#define SCREEN_COLOR_BLACK   ((SDL_Color){.r = 0x00, .g = 0x00, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_WHITE   ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0xFF, .a = 0xFF})
#define SCREEN_COLOR_YELLOW  ((SDL_Color){.r = 0xFF, .g = 0xFF, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_D_RED   ((SDL_Color){.r = 0x7F, .g = 0x00, .b = 0x00, .a = 0xFF})
#define SCREEN_COLOR_GRAY    ((SDL_Color){.r = 0x6A, .g = 0x6A, .b = 0x6A, .a = 0xFF})
#define BUTTON_A             "\uE000"
#define BUTTON_B             "\uE001"
#define BUTTON_X             "\uE002"
#define BUTTON_DPAD          "\uE07D"
#define THREAD_PRIORITY_HIGH 13
#ifdef EMU
#define SCREENSHOT_PATH "romfs:/screenshots/"
#else
#define SCREENSHOT_PATH "fs:/vol/external01/wiiu/screenshots/"
#endif

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

const std::string imagePath = SCREENSHOT_PATH;
FC_Font *font = nullptr;
SDL_Texture *orbTexture = nullptr;
SDL_Texture *particleTexture = nullptr;
SDL_Texture *ghostPointerTexture = nullptr;
SDL_Texture *cornerButtonTexture = nullptr;
SDL_Texture *largeCornerButtonTexture = nullptr;
SDL_Texture *arrowTexture = nullptr;
SDL_Texture *blackTexture = nullptr;
SDL_Texture *placeholderTexture = nullptr;
Texture headerTexture;
Texture backgroundTexture;
Texture backGraphicTexture;
Texture pointerTexture;

static uint8_t *bgmBuffer = nullptr;
static Mix_Music *backgroundMusic = nullptr;

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

void renderBackgroundParticles(SDL_Renderer *renderer, std::vector<Particle> &particles, SDL_Texture *particleTexture) {
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

bool isImageVisible(const ImagesPair &image, int scrollOffsetY, bool fullyVisible) {
    int imageTop = headerTexture.rect.h + image.y + scrollOffsetY;
    int imageBottom = imageTop + IMAGE_HEIGHT + IMAGE_HEIGHT / 2;

    int screenTop = headerTexture.rect.h / 2;
    if (fullyVisible) {
        screenTop *= 2;
    }
    int screenBottom = SCREEN_HEIGHT;

    return (imageBottom >= screenTop) && (imageTop <= screenBottom);
}

SDL_Texture *loadTexture(SDL_Renderer *renderer, const std::string path) {
    SDL_Texture *texture = IMG_LoadTexture(renderer, path.c_str());
    if (texture) {
        return texture;
    }
    return blackTexture;
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

bool showConfirmationDialog(SDL_Renderer *renderer, bool *quit) {
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
    SDL_Event event;
    while (!*quit) {
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

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                *quit = true;
                nn::erreula::DisappearErrorViewer();
                break;
            }
        }
        if (*quit) {
            break;
        }
        if (nn::erreula::IsDecideSelectButtonError()) {
            nn::erreula::DisappearErrorViewer();
            break;
        }
        SDL_RenderPresent(renderer);
        nn::erreula::DrawTV();
        nn::erreula::DrawDRC();
    }
    bool selectedOk = nn::erreula::IsDecideSelectRightButtonError();
    nn::erreula::Destroy();
    MEMFreeToDefaultHeap(createArg.workMemory);

    FSDelClient(fsClient, FS_ERROR_FLAG_NONE);
    MEMFreeToDefaultHeap(fsClient);

    return selectedOk;
}

std::vector<ImagesPair> scanImagePairsInSubfolders(SDL_Renderer *renderer, const std::string &directoryPath, int offsetX, int offsetY, int *totalImages, MutexWrapper totalImagesMutex) {
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

            imgPair.textureTV = loadTexture(renderer, tvPath);
            imgPair.textureDRC = loadTexture(renderer, drcPath);

            imgPair.x = offsetX + (pairIndex - 1) % GRID_SIZE * (IMAGE_WIDTH + SEPARATION);
            imgPair.y = offsetY + (pairIndex - 1) / GRID_SIZE * (IMAGE_WIDTH + SEPARATION);

            imgPair.selected = false;
            imgPair.pathTV = tvPath;
            imgPair.pathDRC = drcPath;

            imagePairs.push_back(imgPair);
            ++pairIndex;
            totalImagesMutex.lock();
            ++(*totalImages);
            totalImagesMutex.unlock();
        }
    }

    return imagePairs;
}

SDL_GameController *findController() {
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            return SDL_GameControllerOpen(i);
        }
    }

    return nullptr;
}

void renderHeader(SDL_Renderer *renderer, FC_Font *font, const Texture &headerTexture) {
    SDL_SetTextureColorMod(headerTexture.texture, 0, 0, 147);
    SDL_SetTextureBlendMode(headerTexture.texture, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, headerTexture.texture, nullptr, &headerTexture.rect);
    FC_DrawColor(font, renderer, headerTexture.rect.x + (headerTexture.rect.w / 2), (headerTexture.rect.y + (headerTexture.rect.h / 2)) - 100, SCREEN_COLOR_WHITE, "Album");
}

void renderImage(SDL_Renderer *renderer, const ImagesPair &image, int scrollOffsetY, MenuState state) {
    SDL_Rect destRectTV = {image.x, headerTexture.rect.h + image.y + scrollOffsetY, IMAGE_WIDTH, IMAGE_HEIGHT};
    SDL_Rect destRectDRC = {image.x + IMAGE_WIDTH / 2, headerTexture.rect.h + image.y + scrollOffsetY + IMAGE_WIDTH / 2, IMAGE_WIDTH / 2, IMAGE_HEIGHT / 2};
    if (image.selected) {
        SDL_SetTextureColorMod(image.textureTV, 0, 255, 0);
        SDL_SetTextureColorMod(image.textureDRC, 0, 255, 0);
    } else {
        SDL_SetTextureColorMod(image.textureTV, 255, 255, 255);
        SDL_SetTextureColorMod(image.textureDRC, 255, 255, 255);
    }
    SDL_SetTextureBlendMode(image.textureTV, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(image.textureDRC, SDL_BLENDMODE_BLEND);
    SDL_RenderCopy(renderer, image.textureTV, nullptr, &destRectTV);
    SDL_RenderCopy(renderer, image.textureDRC, nullptr, &destRectDRC);
    if (state == MenuState::SelectImagesDelete) {
        drawOrb(renderer, image.x - 10, headerTexture.rect.h + image.y + scrollOffsetY - 10, 60, image.selected);
    }
}

int32_t loadFile(const char *fPath, uint8_t **buf) {
    int ret = 0;
    FILE *file = fopen(fPath, "rb");
    if (file != nullptr) {
        struct stat st {};
        stat(fPath, &st);
        int size = st.st_size;

        *buf = (uint8_t *) malloc(size);
        if (*buf != nullptr) {
            if (fread(*buf, size, 1, file) == 1)
                ret = size;
            else
                free(*buf);
        }
        fclose(file);
    }
    return ret;
}

int main() {
    FSInit();
    //AXInit();
    //AXQuit();
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP);
    Mix_Init(MIX_INIT_MP3);

    romfsInit();

    SDL_Window *window = SDL_CreateWindow(nullptr, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    int bgMusicFileSize = loadFile("romfs:/bg_music.mp3", &bgmBuffer);
    if (Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, MIX_DEFAULT_CHANNELS, 4096) == 0) {
        SDL_RWops *rw = SDL_RWFromMem(bgmBuffer, bgMusicFileSize);
        backgroundMusic = Mix_LoadMUS_RW(rw, true);
        if (backgroundMusic != NULL) {
            Mix_VolumeMusic(SDL_MIX_MAXVOLUME * 0.15);
            Mix_PlayMusic(backgroundMusic, -1);
            if (Mix_PlayMusic(backgroundMusic, -1) != 0) {
                Mix_FreeMusic(backgroundMusic);
                bgmBuffer = nullptr;
                Mix_CloseAudio();
            }
        }
    }

    OSSetThreadPriority(OSGetCurrentThread(), THREAD_PRIORITY_HIGH);

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

    blackTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 800, 600);
    SDL_SetRenderTarget(renderer, blackTexture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, nullptr);

    placeholderTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, IMAGE_WIDTH, IMAGE_HEIGHT);
    SDL_SetRenderTarget(renderer, placeholderTexture);
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255); // gray
    SDL_RenderClear(renderer);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_SetRenderTarget(renderer, nullptr);

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

    bool deleteImagesSelected = false;

    int totalImages = 0;
    MutexWrapper totalImagesMutex = MutexWrapper();
    totalImagesMutex.init("totalImages");

    std::vector<ImagesPair> placeholderImages;
    std::future<std::vector<ImagesPair>> futureImages = std::async(std::launch::async, scanImagePairsInSubfolders, renderer, imagePath, offsetX, offsetY, &totalImages, totalImagesMutex);
    std::vector<ImagesPair> images;

    Button cornerButton(0, SCREEN_HEIGHT - 137, 185, 137, cornerButtonTexture, font, "", SCREEN_COLOR_WHITE);
    cornerButton.setOnClick([&]() {
        state = MenuState::ShowAllImages;
        if (std::any_of(images.begin(), images.end(), [](const ImagesPair &image) { return image.selected; })) {
            for (auto &image : images) {
                image.selected = false;
            }
        }
    });
    cornerButton.setControllerButton(SDL_CONTROLLER_BUTTON_B);

    ImagesPair placeholderImgPair;
    placeholderImgPair.textureTV = placeholderTexture;
    placeholderImgPair.textureDRC = placeholderTexture;
    placeholderImgPair.selected = false;
    placeholderImgPair.pathTV = "";
    placeholderImgPair.pathDRC = "";

    while (futureImages.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        totalImagesMutex.lock();
        int newImagesFound = totalImages - placeholderImages.size();
        totalImagesMutex.unlock();
        if (newImagesFound > 0) {
            for (int i = 0; i < newImagesFound; i++) {
                placeholderImgPair.x = offsetX + (placeholderImages.size() % GRID_SIZE) * (IMAGE_WIDTH + SEPARATION);
                placeholderImgPair.y = offsetY + (placeholderImages.size() / GRID_SIZE) * (IMAGE_WIDTH + SEPARATION);
                placeholderImages.push_back(placeholderImgPair);
            }
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, backgroundTexture.texture, nullptr, &backgroundTexture.rect);
        renderBackgroundParticles(renderer, particles, particleTexture);
        for (const auto &image : placeholderImages) {
            renderImage(renderer, image, scrollOffsetY, state);
        }
        renderHeader(renderer, font, headerTexture);
        SDL_RenderPresent(renderer);
    }

    images = futureImages.get();
    placeholderImages.clear();
    placeholderImages.shrink_to_fit();

    Button largeCornerButton(SCREEN_WIDTH - 470, 0, 470, 160, largeCornerButtonTexture, font, BUTTON_X " Select", SCREEN_COLOR_BLACK);
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
    largeCornerButton.setControllerButton(SDL_CONTROLLER_BUTTON_X);

    SDL_GameController *controller = findController();

    bool quit = false;
    bool isCameraScrolling = false;
    bool selectedImage = false;
    bool renderHover = true;
    int initialTouchY = -1;
    int initialSelectedImageIndex;
    SDL_Event event;
    ImagePairScreen imagePairScreen(nullptr, arrowTexture, renderer);
    initializeGhostPointerTexture(renderer);
    while (!quit) {
        deleteImagesSelected = false;
        int x, y;
        while (SDL_PollEvent(&event)) {
            cornerButton.handleEvent(event);
            largeCornerButton.handleEvent(event);
            if (state == MenuState::ShowSingleImage) {
                imagePairScreen.handleEvent(event);
            }
            switch (event.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_CONTROLLERDEVICEADDED:
                    if (!controller) {
                        controller = SDL_GameControllerOpen(event.cdevice.which);
                    }
                    break;
                case SDL_CONTROLLERDEVICEREMOVED:
                    if (controller && event.cdevice.which == SDL_JoystickInstanceID(
                                                                     SDL_GameControllerGetJoystick(controller))) {
                        SDL_GameControllerClose(controller);
                        controller = findController();
                    }
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    renderHover = true;
                    switch (event.cbutton.button) {
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
                        case SDL_CONTROLLER_BUTTON_DPAD_UP:
                            if (state != MenuState::ShowSingleImage) {
                                if (selectedImageIndex >= GRID_SIZE) {
                                    selectedImageIndex -= GRID_SIZE;
                                    if (isFirstRow(selectedImageIndex) || !isImageVisible(images[selectedImageIndex], scrollOffsetY, true)) {
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
                        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                            if (state != MenuState::ShowSingleImage) {
                                if (selectedImageIndex < static_cast<int>(images.size()) - GRID_SIZE) {
                                    selectedImageIndex += GRID_SIZE;
                                    if (isLastRow(selectedImageIndex, images.size()) || !isImageVisible(images[selectedImageIndex], scrollOffsetY, true)) {
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
                        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                            if (state != MenuState::ShowSingleImage) {
                                if (selectedImageIndex % GRID_SIZE != 0) {
                                    selectedImageIndex--;
                                }
                            }
                            break;
                        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
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
                        for (const auto &image : images) {
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
                        for (const auto &image : images) {
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
                        renderHover = false;
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

                        selectedImageIndex = std::clamp(newRow * GRID_SIZE + (initialSelectedImageIndex % GRID_SIZE), 0, (int) images.size() - 1);

                        initialTouchY = y;
                    }
                    break;
                default:
                    break;
            }
        }

        if (deleteImagesSelected) {
            if (showConfirmationDialog(renderer, &quit)) {
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
                    for (const auto &image : removedImages) {
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

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, backgroundTexture.texture, nullptr, &backgroundTexture.rect);
        renderBackgroundParticles(renderer, particles, particleTexture);
        if (state != MenuState::ShowSingleImage) {
            if (images.empty()) {
                FC_Draw(font, renderer, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, "No images found");
                SDL_RenderPresent(renderer);
            } else {
                for (const auto &image : images) {
                    if (isImageVisible(image, scrollOffsetY, false)) {
                        renderImage(renderer, image, scrollOffsetY, state);
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
                renderHeader(renderer, font, headerTexture);
                largeCornerButton.render(renderer);
                if (renderHover) {
                    drawRect(renderer, images[selectedImageIndex].x - IMAGE_WIDTH * 0.05, headerTexture.rect.h + images[selectedImageIndex].y + scrollOffsetY - IMAGE_HEIGHT * 0.05, IMAGE_WIDTH * 1.1, IMAGE_HEIGHT * 1.5, 7, SCREEN_COLOR_YELLOW);
                }
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
        cornerButton.updateButton(x, y, event.type == SDL_FINGERUP);
        largeCornerButton.updateButton(x, y, event.type == SDL_FINGERUP);
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
    Mix_FreeMusic(backgroundMusic);
    bgmBuffer = nullptr;
    Mix_CloseAudio();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    IMG_Quit();
    FSShutdown();

    romfsExit();

    return 0;
}
