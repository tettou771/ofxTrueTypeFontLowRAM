#include "ofxTrueTypeFontLowRAM.h"
#include "ofGraphics.h"
#include "ofAppRunner.h"
#include "ofUtils.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

#ifdef TARGET_OSX
#include <CoreText/CoreText.h>
#endif

// FreeTypeライブラリ（グローバル）
static FT_Library ftLibrary = nullptr;
static int ftLibraryRefCount = 0;

// FreeTypeライブラリ初期化
static bool initFreeType() {
    if (ftLibrary == nullptr) {
        if (FT_Init_FreeType(&ftLibrary)) {
            ofLogError("ofxTrueTypeFontLowRAM") << "FreeTypeライブラリの初期化に失敗";
            return false;
        }
    }
    ftLibraryRefCount++;
    return true;
}

// FreeTypeライブラリ解放
static void releaseFreeType() {
    ftLibraryRefCount--;
    if (ftLibraryRefCount <= 0 && ftLibrary != nullptr) {
        FT_Done_FreeType(ftLibrary);
        ftLibrary = nullptr;
        ftLibraryRefCount = 0;
    }
}

// 26.6固定小数点から浮動小数点への変換
static double int26p6_to_dbl(long p) {
    return double(p) / 64.0;
}

#ifdef TARGET_OSX
// macOSでフォント名からファイルパスを取得
static of::filesystem::path osxFontPathByName(const string& fontName) {
    CFStringRef targetName = CFStringCreateWithCString(nullptr, fontName.c_str(), kCFStringEncodingUTF8);
    CTFontDescriptorRef targetDescriptor = CTFontDescriptorCreateWithNameAndSize(targetName, 0.0);
    CFURLRef targetURL = (CFURLRef)CTFontDescriptorCopyAttribute(targetDescriptor, kCTFontURLAttribute);
    string fontDir = "";

    if (targetURL) {
        UInt8 buffer[PATH_MAX];
        CFURLGetFileSystemRepresentation(targetURL, true, buffer, PATH_MAX);
        fontDir = string((char*)buffer);
        CFRelease(targetURL);
    }

    CFRelease(targetName);
    CFRelease(targetDescriptor);
    return of::filesystem::path(fontDir);
}
#endif

// フォントパスを解決する（システムフォントも対応）
static of::filesystem::path resolveFontPath(const of::filesystem::path& fontPath) {
    // まずdataフォルダ内を探す
    of::filesystem::path resolvedPath = ofToDataPath(fontPath, true);
    if (of::filesystem::exists(resolvedPath)) {
        return resolvedPath;
    }

    // 絶対パスとして存在するか
    if (of::filesystem::exists(fontPath)) {
        return fontPath;
    }

#ifdef TARGET_OSX
    // macOSの場合、システムフォント名として解決を試みる
    string fontName = fontPath.string();

    // OF_TTF_SANS等の変換
    if (fontName == OF_TTF_SANS) {
        fontName = "Helvetica Neue";
    } else if (fontName == OF_TTF_SERIF) {
        fontName = "Times New Roman";
    } else if (fontName == OF_TTF_MONO) {
        fontName = "Menlo Regular";
    }

    of::filesystem::path systemPath = osxFontPathByName(fontName);
    if (!systemPath.empty() && of::filesystem::exists(systemPath)) {
        ofLogNotice("ofxTrueTypeFontLowRAM") << "システムフォントを使用: " << systemPath;
        return systemPath;
    }
#endif

    // 見つからない場合は空を返す
    return of::filesystem::path();
}

// ===========================================================================
// FontAtlasManager 実装
// ===========================================================================

FontAtlasManager::FontAtlasManager() {
}

FontAtlasManager::~FontAtlasManager() {
    // 終了時のstatic変数破棄順序の問題を避けるため、
    // FT_Done_Faceは呼ばない（OSがプロセス終了時にクリーンアップする）
    // face.reset()を呼ぶとshared_ptrのデリータが動いてFT_Done_Faceが呼ばれてしまう
    // ので、ここでは何もしない
    //
    // 注意: 動的にフォントをロード/アンロードする場合は別途対応が必要
}

int FontAtlasManager::getMaxTextureSize() {
    static int maxSize = 0;
    if (maxSize == 0) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
        if (maxSize <= 0) {
            maxSize = 4096;  // フォールバック
        }
    }
    return maxSize;
}

bool FontAtlasManager::setup(const of::filesystem::path& fontPath, int size, bool antialias, int dpiValue) {
    if (!initFreeType()) {
        return false;
    }

    fontSize = size;
    antialiased = antialias;
    dpi = (dpiValue > 0) ? dpiValue : 96;

    // 最大テクスチャサイズを取得
    maxAtlasSize = getMaxTextureSize();
    ofLogNotice("ofxTrueTypeFontLowRAM") << "最大テクスチャサイズ: " << maxAtlasSize;

    // 最小サイズは fontSize * 4
    minAtlasSize = max(64, fontSize * 4);
    // 2の累乗に切り上げ
    int s = 64;
    while (s < minAtlasSize) s *= 2;
    minAtlasSize = s;

    ofLogNotice("ofxTrueTypeFontLowRAM") << "初期アトラスサイズ: " << minAtlasSize;

    // フォントファイルを探す（システムフォント対応）
    of::filesystem::path resolvedPath = resolveFontPath(fontPath);
    if (resolvedPath.empty()) {
        ofLogError("ofxTrueTypeFontLowRAM") << "フォントが見つからない: " << fontPath;
        releaseFreeType();
        return false;
    }

    // FT_Faceをロード
    FT_Face rawFace;
    FT_Error err = FT_New_Face(ftLibrary, resolvedPath.string().c_str(), 0, &rawFace);
    if (err) {
        ofLogError("ofxTrueTypeFontLowRAM") << "フォントのロードに失敗: " << fontPath;
        releaseFreeType();
        return false;
    }

    // shared_ptrで管理
    // 注意: プログラム終了時、static変数の破棄順序が不定のため
    // ftLibraryがnullptrの場合はFT_Done_Faceを呼ばない
    face = shared_ptr<FT_FaceRec_>(rawFace, [](FT_Face f) {
        if (ftLibrary != nullptr) {
            FT_Done_Face(f);
        }
    });

    // フォントサイズ設定
    FT_Set_Char_Size(face.get(), fontSize << 6, fontSize << 6, dpi, dpi);

    // メトリクス取得
    fontUnitScale = float(face->size->metrics.y_ppem) / float(face->units_per_EM);
    lineHeight = int26p6_to_dbl(face->size->metrics.height);
    ascenderHeight = int26p6_to_dbl(face->size->metrics.ascender);
    descenderHeight = int26p6_to_dbl(face->size->metrics.descender);

    // スペースのadvanceを取得
    FT_UInt spaceIndex = FT_Get_Char_Index(face.get(), ' ');
    if (FT_Load_Glyph(face.get(), spaceIndex, FT_LOAD_NO_HINTING) == 0) {
        spaceAdvance = int26p6_to_dbl(face->glyph->metrics.horiAdvance);
    } else {
        spaceAdvance = fontSize * 0.5f;  // フォールバック
    }

    // 最初のアトラスを作成
    createNewAtlas();

    return true;
}

size_t FontAtlasManager::createNewAtlas() {
    int size = minAtlasSize;
    if (!atlases.empty()) {
        // 既存のアトラスがあれば、それと同じサイズで作成
        size = atlasStates.back().width;
    }

    AtlasState state;
    state.width = size;
    state.height = size;
    state.currentX = border;
    state.currentY = border;
    state.currentRowHeight = 0;
    atlasStates.push_back(state);

    // CPU側ピクセルバッファ
    ofPixels pixels;
    pixels.allocate(size, size, OF_PIXELS_GRAY_ALPHA);
    pixels.set(0, 255);  // ルミナンス = 白
    pixels.set(1, 0);    // アルファ = 透明
    atlasPixels.push_back(pixels);

    // GPU側テクスチャ
    ofTexture tex;
    tex.allocate(pixels, false);
    tex.setRGToRGBASwizzles(true);
    if (antialiased && fontSize > 20) {
        tex.setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
    } else {
        tex.setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
    }
    tex.loadData(pixels);
    atlases.push_back(std::move(tex));

    return atlases.size() - 1;
}

bool FontAtlasManager::expandAtlas(size_t atlasIndex) {
    if (atlasIndex >= atlases.size()) return false;

    AtlasState& state = atlasStates[atlasIndex];
    int newSize = state.width * 2;

    if (newSize > maxAtlasSize) {
        ofLogWarning("ofxTrueTypeFontLowRAM") << "アトラスが最大サイズに達した、新しいアトラスを作成";
        return false;  // 拡張不可、新しいアトラスが必要
    }

    ofLogNotice("ofxTrueTypeFontLowRAM") << "アトラスを拡張: " << state.width << " -> " << newSize;

    // 新しいピクセルバッファを作成
    ofPixels newPixels;
    newPixels.allocate(newSize, newSize, OF_PIXELS_GRAY_ALPHA);
    newPixels.set(0, 255);
    newPixels.set(1, 0);

    // 既存のピクセルをコピー
    ofPixels& oldPixels = atlasPixels[atlasIndex];
    oldPixels.pasteInto(newPixels, 0, 0);

    // 状態更新
    state.width = newSize;
    state.height = newSize;
    atlasPixels[atlasIndex] = std::move(newPixels);

    // GPUテクスチャを再作成
    atlases[atlasIndex].clear();
    atlases[atlasIndex].allocate(atlasPixels[atlasIndex], false);
    atlases[atlasIndex].setRGToRGBASwizzles(true);
    if (antialiased && fontSize > 20) {
        atlases[atlasIndex].setTextureMinMagFilter(GL_LINEAR, GL_LINEAR);
    } else {
        atlases[atlasIndex].setTextureMinMagFilter(GL_NEAREST, GL_NEAREST);
    }
    atlases[atlasIndex].loadData(atlasPixels[atlasIndex]);

    // 既存グリフのテクスチャ座標を再計算
    for (auto& [codepoint, props] : glyphs) {
        if (props.atlasIndex == atlasIndex) {
            // テクスチャ座標を新しいサイズに合わせて再計算
            // 古いサイズでのピクセル位置を逆算
            float oldW = state.width / 2.0f;
            float oldH = state.height / 2.0f;
            float newW = float(state.width);
            float newH = float(state.height);

            props.t1 = (props.t1 * oldW) / newW;
            props.t2 = (props.t2 * oldW) / newW;
            props.v1 = (props.v1 * oldH) / newH;
            props.v2 = (props.v2 * oldH) / newH;
        }
    }

    return true;
}

bool FontAtlasManager::rasterizeGlyph(uint32_t codepoint, ofPixels& outPixels, LazyGlyphProps& outProps) {
    if (!face) return false;

    FT_UInt glyphIndex = FT_Get_Char_Index(face.get(), codepoint);
    if (glyphIndex == 0) {
        // 存在しないグリフ
        return false;
    }

    FT_Error err = FT_Load_Glyph(face.get(), glyphIndex, FT_LOAD_NO_HINTING);
    if (err) {
        ofLogWarning("ofxTrueTypeFontLowRAM") << "グリフのロードに失敗: " << codepoint;
        return false;
    }

    // ラスタライズ
    if (antialiased) {
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
    } else {
        FT_Render_Glyph(face->glyph, FT_RENDER_MODE_MONO);
    }

    FT_Bitmap& bitmap = face->glyph->bitmap;
    int width = bitmap.width;
    int height = bitmap.rows;

    // プロパティ設定（ofTrueTypeFontと同じ計算方法）
    outProps.width = int26p6_to_dbl(face->glyph->metrics.width);
    outProps.height = int26p6_to_dbl(face->glyph->metrics.height);
    outProps.bearingX = int26p6_to_dbl(face->glyph->metrics.horiBearingX);
    outProps.bearingY = int26p6_to_dbl(face->glyph->metrics.horiBearingY);
    outProps.advance = int26p6_to_dbl(face->glyph->metrics.horiAdvance);
    // xmin/ymin/xmax/ymaxはbitmap_left/bitmap_topから計算（重要！）
    outProps.xmin = face->glyph->bitmap_left;
    outProps.xmax = outProps.xmin + outProps.width;
    outProps.ymin = -face->glyph->bitmap_top;  // マイナスが重要
    outProps.ymax = outProps.ymin + outProps.height;
    outProps.tW = width;
    outProps.tH = height;

    if (width == 0 || height == 0) {
        // スペースなど描画不要な文字
        outPixels.clear();
        return true;
    }

    // ピクセルデータを作成
    outPixels.allocate(width, height, OF_PIXELS_GRAY_ALPHA);
    outPixels.set(0, 255);  // ルミナンス = 白
    outPixels.set(1, 0);    // アルファ = 透明

    if (antialiased) {
        // グレースケール
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                unsigned char alpha = bitmap.buffer[y * bitmap.pitch + x];
                outPixels.setColor(x, y, ofColor(255, alpha));
            }
        }
    } else {
        // モノクロ（1ビット）
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int byteIndex = x / 8;
                int bitIndex = 7 - (x % 8);
                unsigned char byte = bitmap.buffer[y * bitmap.pitch + byteIndex];
                unsigned char alpha = (byte & (1 << bitIndex)) ? 255 : 0;
                outPixels.setColor(x, y, ofColor(255, alpha));
            }
        }
    }

    return true;
}

bool FontAtlasManager::addGlyphToAtlas(uint32_t codepoint, LazyGlyphProps& outProps) {
    ofPixels glyphPixels;
    if (!rasterizeGlyph(codepoint, glyphPixels, outProps)) {
        return false;
    }

    int glyphW = glyphPixels.getWidth();
    int glyphH = glyphPixels.getHeight();

    if (glyphW == 0 || glyphH == 0) {
        // スペースなど（テクスチャ不要）
        outProps.atlasIndex = 0;
        outProps.t1 = outProps.t2 = outProps.v1 = outProps.v2 = 0;
        return true;
    }

    // 最後のアトラスに追加を試みる
    size_t atlasIndex = atlases.size() - 1;
    AtlasState& state = atlasStates[atlasIndex];

    // 現在の行に収まるかチェック
    if (state.currentX + glyphW + border > state.width) {
        // 次の行へ
        state.currentX = border;
        state.currentY += state.currentRowHeight + border;
        state.currentRowHeight = 0;
    }

    // 高さが足りるかチェック
    if (state.currentY + glyphH + border > state.height) {
        // アトラスを拡張
        if (!expandAtlas(atlasIndex)) {
            // 拡張できなかった → 新しいアトラスを作成
            atlasIndex = createNewAtlas();
            state = atlasStates[atlasIndex];
        }
    }

    // 再度チェック（拡張後）
    AtlasState& currentState = atlasStates[atlasIndex];
    if (currentState.currentX + glyphW + border > currentState.width) {
        currentState.currentX = border;
        currentState.currentY += currentState.currentRowHeight + border;
        currentState.currentRowHeight = 0;
    }

    if (currentState.currentY + glyphH + border > currentState.height) {
        // それでも入らない → さらに拡張 or 新アトラス
        if (!expandAtlas(atlasIndex)) {
            atlasIndex = createNewAtlas();
            currentState = atlasStates[atlasIndex];
        }
    }

    // グリフをアトラスにペースト
    int x = currentState.currentX;
    int y = currentState.currentY;

    glyphPixels.pasteInto(atlasPixels[atlasIndex], x, y);

    // テクスチャ座標を計算
    float atlasW = float(currentState.width);
    float atlasH = float(currentState.height);
    outProps.atlasIndex = atlasIndex;
    outProps.t1 = float(x) / atlasW;
    outProps.v1 = float(y) / atlasH;
    outProps.t2 = float(x + glyphW) / atlasW;
    outProps.v2 = float(y + glyphH) / atlasH;

    // 書き込み位置を更新
    currentState.currentX += glyphW + border;
    currentState.currentRowHeight = max(currentState.currentRowHeight, glyphH);

    // GPUにアップロード
    atlases[atlasIndex].loadData(atlasPixels[atlasIndex]);

    return true;
}

const LazyGlyphProps* FontAtlasManager::getOrLoadGlyph(uint32_t codepoint) {
    auto it = glyphs.find(codepoint);
    if (it != glyphs.end()) {
        return &it->second;
    }

    // 遅延ロード
    LazyGlyphProps props;
    if (!addGlyphToAtlas(codepoint, props)) {
        return nullptr;
    }

    auto result = glyphs.emplace(codepoint, props);
    return &result.first->second;
}

bool FontAtlasManager::hasGlyph(uint32_t codepoint) const {
    return glyphs.find(codepoint) != glyphs.end();
}

const ofTexture& FontAtlasManager::getTexture(size_t atlasIndex) const {
    static ofTexture emptyTex;
    if (atlasIndex < atlases.size()) {
        return atlases[atlasIndex];
    }
    return emptyTex;
}

size_t FontAtlasManager::getMemoryUsage() const {
    size_t total = 0;

    // テクスチャメモリ（GPU + CPUコピー）
    for (size_t i = 0; i < atlasStates.size(); i++) {
        // GRAY_ALPHA = 2バイト/ピクセル
        size_t texSize = atlasStates[i].width * atlasStates[i].height * 2;
        total += texSize * 2;  // GPU + CPU
    }

    // グリフ情報
    total += glyphs.size() * sizeof(LazyGlyphProps);

    return total;
}

double FontAtlasManager::getKerning(uint32_t leftC, uint32_t rightC) const {
    if (!face) return 0.0;

    if (FT_HAS_KERNING(face.get())) {
        FT_Vector kerning;
        FT_Get_Kerning(face.get(),
                       FT_Get_Char_Index(face.get(), leftC),
                       FT_Get_Char_Index(face.get(), rightC),
                       FT_KERNING_UNFITTED, &kerning);
        return int26p6_to_dbl(kerning.x);
    }
    return 0.0;
}

// ===========================================================================
// SharedFontCache 実装
// ===========================================================================

SharedFontCache& SharedFontCache::getInstance() {
    static SharedFontCache instance;
    return instance;
}

shared_ptr<FontAtlasManager> SharedFontCache::getOrCreate(const FontCacheKey& key, int dpi) {
    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    auto manager = make_shared<FontAtlasManager>();
    if (!manager->setup(key.fontPath, key.fontSize, key.antialiased, dpi)) {
        return nullptr;
    }

    cache[key] = manager;
    return manager;
}

void SharedFontCache::release(const FontCacheKey& key) {
    cache.erase(key);
}

void SharedFontCache::clear() {
    cache.clear();
}

size_t SharedFontCache::getTotalMemoryUsage() const {
    size_t total = 0;
    for (const auto& [key, manager] : cache) {
        total += manager->getMemoryUsage();
    }
    return total;
}

// ===========================================================================
// ofxTrueTypeFontLowRAM 実装
// ===========================================================================

ofxTrueTypeFontLowRAM::ofxTrueTypeFontLowRAM() {
}

ofxTrueTypeFontLowRAM::~ofxTrueTypeFontLowRAM() {
}

ofxTrueTypeFontLowRAM::ofxTrueTypeFontLowRAM(const ofxTrueTypeFontLowRAM& other) {
    atlasManager = other.atlasManager;
    cacheKey = other.cacheKey;
    bLoadedOk = other.bLoadedOk;
    lineHeight = other.lineHeight;
    ascenderHeight = other.ascenderHeight;
    descenderHeight = other.descenderHeight;
    letterSpacing = other.letterSpacing;
    spaceSize = other.spaceSize;
}

ofxTrueTypeFontLowRAM& ofxTrueTypeFontLowRAM::operator=(const ofxTrueTypeFontLowRAM& other) {
    if (this != &other) {
        atlasManager = other.atlasManager;
        cacheKey = other.cacheKey;
        bLoadedOk = other.bLoadedOk;
        lineHeight = other.lineHeight;
        ascenderHeight = other.ascenderHeight;
        descenderHeight = other.descenderHeight;
        letterSpacing = other.letterSpacing;
        spaceSize = other.spaceSize;
    }
    return *this;
}

ofxTrueTypeFontLowRAM::ofxTrueTypeFontLowRAM(ofxTrueTypeFontLowRAM&& other) noexcept {
    atlasManager = std::move(other.atlasManager);
    cacheKey = std::move(other.cacheKey);
    bLoadedOk = other.bLoadedOk;
    lineHeight = other.lineHeight;
    ascenderHeight = other.ascenderHeight;
    descenderHeight = other.descenderHeight;
    letterSpacing = other.letterSpacing;
    spaceSize = other.spaceSize;
    other.bLoadedOk = false;
}

ofxTrueTypeFontLowRAM& ofxTrueTypeFontLowRAM::operator=(ofxTrueTypeFontLowRAM&& other) noexcept {
    if (this != &other) {
        atlasManager = std::move(other.atlasManager);
        cacheKey = std::move(other.cacheKey);
        bLoadedOk = other.bLoadedOk;
        lineHeight = other.lineHeight;
        ascenderHeight = other.ascenderHeight;
        descenderHeight = other.descenderHeight;
        letterSpacing = other.letterSpacing;
        spaceSize = other.spaceSize;
        other.bLoadedOk = false;
    }
    return *this;
}

bool ofxTrueTypeFontLowRAM::load(const of::filesystem::path& filename,
                                  int fontsize,
                                  bool _bAntiAliased,
                                  bool _bFullCharacterSet,
                                  bool makeContours,
                                  float simplifyAmt,
                                  int dpi) {
    if (makeContours) {
        ofLogWarning("ofxTrueTypeFontLowRAM") << "makeContoursは未サポート";
    }

    // キャッシュキー作成
    cacheKey.fontPath = filename.string();
    cacheKey.fontSize = fontsize;
    cacheKey.antialiased = _bAntiAliased;

    // 共有キャッシュから取得
    atlasManager = SharedFontCache::getInstance().getOrCreate(cacheKey, dpi);
    if (!atlasManager) {
        bLoadedOk = false;
        return false;
    }

    // 親クラスのメンバーを設定
    bLoadedOk = true;
    lineHeight = atlasManager->getLineHeight();
    ascenderHeight = atlasManager->getAscenderHeight();
    descenderHeight = atlasManager->getDescenderHeight();
    letterSpacing = 1.0f;
    spaceSize = 1.0f;

    // 設定を保存
    settings.fontName = filename;
    settings.fontSize = fontsize;
    settings.antialiased = _bAntiAliased;
    settings.dpi = dpi;

    return true;
}

bool ofxTrueTypeFontLowRAM::load(const ofTrueTypeFontSettings& s) {
    if (!s.ranges.empty()) {
        ofLogWarning("ofxTrueTypeFontLowRAM") << "Unicode ranges are ignored. This addon uses lazy loading, so all glyphs are loaded on demand regardless of range settings.";
    }
    return load(s.fontName, s.fontSize, s.antialiased, true, s.contours, s.simplifyAmt, s.dpi);
}

void ofxTrueTypeFontLowRAM::iterateStringInternal(const string& str, float x, float y, bool vFlipped,
                                                   function<void(uint32_t, glm::vec2)> f) const {
    if (!atlasManager) return;

    glm::vec2 pos(x, y);
    float newLineDirection = vFlipped ? 1 : -1;
    float directionX = (settings.direction == OF_TTF_LEFT_TO_RIGHT) ? 1 : -1;
    uint32_t prevC = 0;

    for (auto c : ofUTF8Iterator(str)) {
        try {
            if (c == '\n') {
                pos.y += lineHeight * newLineDirection;
                pos.x = x;
                prevC = 0;
            } else if (c == '\t') {
                f(c, pos);
                pos.x += atlasManager->getSpaceAdvance() * spaceSize * 4 * directionX;
                prevC = c;
            } else if (c == ' ') {
                pos.x += atlasManager->getSpaceAdvance() * spaceSize * directionX;
                f(c, pos);
                prevC = c;
            } else {
                // グリフを取得（遅延ロード）
                const LazyGlyphProps* props = atlasManager->getOrLoadGlyph(c);
                if (props) {
                    if (prevC > 0) {
                        if (settings.direction == OF_TTF_LEFT_TO_RIGHT) {
                            pos.x += atlasManager->getKerning(prevC, c);
                        } else {
                            pos.x += atlasManager->getKerning(c, prevC);
                        }
                    }
                    if (settings.direction == OF_TTF_LEFT_TO_RIGHT) {
                        f(c, pos);
                        pos.x += props->advance * directionX;
                        pos.x += atlasManager->getSpaceAdvance() * (letterSpacing - 1.f) * directionX;
                    } else {
                        pos.x += props->advance * directionX;
                        pos.x += atlasManager->getSpaceAdvance() * (letterSpacing - 1.f) * directionX;
                        f(c, pos);
                    }
                    prevC = c;
                }
            }
        } catch (...) {
            break;
        }
    }
}

void ofxTrueTypeFontLowRAM::drawCharInternal(uint32_t c, float x, float y, bool vFlipped) const {
    if (!atlasManager) return;

    const LazyGlyphProps* props = atlasManager->getOrLoadGlyph(c);
    if (!props) return;
    if (props->tW == 0 || props->tH == 0) return;  // スペースなど

    float xmin = props->xmin + x;
    float ymin = props->ymin;
    float xmax = props->xmax + x;
    float ymax = props->ymax;

    if (!vFlipped) {
        ymin *= -1.0f;
        ymax *= -1.0f;
    }
    ymin += y;
    ymax += y;

    // アトラスごとにメッシュを分ける
    size_t atlasIdx = props->atlasIndex;
    while (meshesPerAtlas.size() <= atlasIdx) {
        meshesPerAtlas.push_back(ofMesh());
        meshesPerAtlas.back().setMode(OF_PRIMITIVE_TRIANGLES);
    }

    ofMesh& mesh = meshesPerAtlas[atlasIdx];
    ofIndexType firstIndex = mesh.getVertices().size();

    mesh.addVertex(glm::vec3(xmin, ymin, 0.f));
    mesh.addVertex(glm::vec3(xmax, ymin, 0.f));
    mesh.addVertex(glm::vec3(xmax, ymax, 0.f));
    mesh.addVertex(glm::vec3(xmin, ymax, 0.f));

    mesh.addTexCoord(glm::vec2(props->t1, props->v1));
    mesh.addTexCoord(glm::vec2(props->t2, props->v1));
    mesh.addTexCoord(glm::vec2(props->t2, props->v2));
    mesh.addTexCoord(glm::vec2(props->t1, props->v2));

    mesh.addIndex(firstIndex);
    mesh.addIndex(firstIndex + 1);
    mesh.addIndex(firstIndex + 2);
    mesh.addIndex(firstIndex + 2);
    mesh.addIndex(firstIndex + 3);
    mesh.addIndex(firstIndex);
}

void ofxTrueTypeFontLowRAM::createStringMeshInternal(const string& s, float x, float y, bool vFlipped) const {
    // メッシュをクリア
    for (auto& mesh : meshesPerAtlas) {
        mesh.clear();
    }

    iterateStringInternal(s, x, y, vFlipped, [this](uint32_t c, glm::vec2 pos) {
        drawCharInternal(c, pos.x, pos.y, ofIsVFlipped());
    });
}

void ofxTrueTypeFontLowRAM::drawString(const string& s, float x, float y) const {
    if (!bLoadedOk || !atlasManager) {
        ofLogError("ofxTrueTypeFontLowRAM") << "drawString(): フォントがロードされていない";
        return;
    }

    createStringMeshInternal(s, x, y, ofIsVFlipped());

    // ブレンド設定を保存
    bool blendEnabled = glIsEnabled(GL_BLEND);
    GLint blendSrc, blendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDst);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 各アトラスのメッシュを描画
    for (size_t i = 0; i < meshesPerAtlas.size(); i++) {
        if (meshesPerAtlas[i].getNumVertices() > 0) {
            atlasManager->getTexture(i).bind();
            meshesPerAtlas[i].draw();
            atlasManager->getTexture(i).unbind();
        }
    }

    // ブレンド設定を復元
    if (!blendEnabled) {
        glDisable(GL_BLEND);
    }
    glBlendFunc(blendSrc, blendDst);
}

float ofxTrueTypeFontLowRAM::stringWidth(const string& s) const {
    if (!bLoadedOk || !atlasManager) return 0;

    float w = 0;
    iterateStringInternal(s, 0, 0, false, [&](uint32_t c, glm::vec2 pos) {
        float cWidth = 0;
        if (settings.direction == OF_TTF_LEFT_TO_RIGHT) {
            if (c == '\t') {
                cWidth = atlasManager->getSpaceAdvance() * spaceSize * 4;  // TAB_WIDTH = 4
            } else {
                const LazyGlyphProps* props = atlasManager->getOrLoadGlyph(c);
                if (props) {
                    cWidth = props->advance;
                }
            }
        }
        w = max(w, abs(pos.x + cWidth));
    });
    return w;
}

float ofxTrueTypeFontLowRAM::stringHeight(const string& s) const {
    return getStringBoundingBox(s, 0, 0).height;
}

ofRectangle ofxTrueTypeFontLowRAM::getStringBoundingBox(const string& s, float x, float y, bool vflip) const {
    if (!bLoadedOk || !atlasManager || s.empty()) {
        return ofRectangle(x, y, 0, 0);
    }

    float minX = x;
    float minY = y;
    float maxY = y;
    float w = 0;

    iterateStringInternal(s, x, y, vflip, [&](uint32_t c, glm::vec2 pos) {
        const LazyGlyphProps* props = atlasManager->getOrLoadGlyph(c);
        if (!props) return;

        float cWidth = 0;
        if (settings.direction == OF_TTF_LEFT_TO_RIGHT) {
            if (c == '\t') {
                cWidth = atlasManager->getSpaceAdvance() * spaceSize * 4;
            } else {
                cWidth = props->advance;
            }
        }

        w = max(w, abs(pos.x - x) + cWidth);
        minX = min(minX, pos.x);

        if (vflip) {
            minY = min(minY, pos.y - (props->ymax - props->ymin));
            maxY = max(maxY, pos.y - (props->bearingY - props->height));
        } else {
            minY = min(minY, pos.y - props->ymax);
            maxY = max(maxY, pos.y - props->ymin);
        }
    });

    float height = maxY - minY;
    return ofRectangle(minX, minY, w, height);
}

const ofMesh& ofxTrueTypeFontLowRAM::getStringMesh(const string& s, float x, float y, bool vFlipped) const {
    tempMesh.clear();
    tempMesh.setMode(OF_PRIMITIVE_TRIANGLES);

    if (!atlasManager) return tempMesh;

    // シンプルな実装（1つ目のアトラスのみ）
    // 複数アトラスの場合は getAllMeshes() などを別途実装すべき
    createStringMeshInternal(s, x, y, vFlipped);

    if (!meshesPerAtlas.empty()) {
        tempMesh = meshesPerAtlas[0];
    }

    return tempMesh;
}

const ofTexture& ofxTrueTypeFontLowRAM::getFontTexture() const {
    if (atlasManager) {
        return atlasManager->getTexture(0);
    }
    static ofTexture emptyTex;
    return emptyTex;
}

const vector<ofTexture>& ofxTrueTypeFontLowRAM::getAllTextures() const {
    static vector<ofTexture> empty;
    if (!atlasManager) return empty;

    // 注意: これは効率的ではない（コピーが発生）
    // 必要なら別の方法を検討
    static vector<ofTexture> textures;
    textures.clear();
    for (size_t i = 0; i < atlasManager->getAtlasCount(); i++) {
        textures.push_back(atlasManager->getTexture(i));
    }
    return textures;
}

size_t ofxTrueTypeFontLowRAM::getAtlasCount() const {
    return atlasManager ? atlasManager->getAtlasCount() : 0;
}

size_t ofxTrueTypeFontLowRAM::getMemoryUsage() const {
    return atlasManager ? atlasManager->getMemoryUsage() : 0;
}

size_t ofxTrueTypeFontLowRAM::getTotalCacheMemoryUsage() {
    return SharedFontCache::getInstance().getTotalMemoryUsage();
}

size_t ofxTrueTypeFontLowRAM::getLoadedGlyphCount() const {
    return atlasManager ? atlasManager->getLoadedGlyphCount() : 0;
}

bool ofxTrueTypeFontLowRAM::isValidGlyph(uint32_t glyph) const {
    // 遅延ロードなので、基本的にはFreeTypeで描画可能なら有効
    // ここでは常にtrueを返すか、実際にロードしてチェックするか選択
    // パフォーマンスのため、ロード済みならチェック、そうでなければtrue
    if (atlasManager && atlasManager->hasGlyph(glyph)) {
        return true;
    }
    return true;  // 遅延ロードするので基本的にtrue
}
