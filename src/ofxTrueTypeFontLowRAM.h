#pragma once

#include "ofTrueTypeFont.h"
#include "ofFbo.h"
#include <unordered_map>
#include <memory>
using namespace std;

// 前方宣言
class FontAtlasManager;

// フォントキャッシュのキー（フォントパス + サイズ + アンチエイリアス）
struct FontCacheKey {
    string fontPath;
    int fontSize;
    bool antialiased;

    bool operator==(const FontCacheKey& other) const {
        return fontPath == other.fontPath &&
               fontSize == other.fontSize &&
               antialiased == other.antialiased;
    }
};

// FontCacheKey用のハッシュ関数
struct FontCacheKeyHash {
    size_t operator()(const FontCacheKey& key) const {
        size_t h1 = hash<string>()(key.fontPath);
        size_t h2 = hash<int>()(key.fontSize);
        size_t h3 = hash<bool>()(key.antialiased);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// グリフ情報（テクスチャ座標など）
struct LazyGlyphProps {
    size_t atlasIndex;      // どのアトラスに入っているか
    float t1, t2, v1, v2;   // テクスチャ座標
    float width, height;
    float bearingX, bearingY;
    float xmin, xmax, ymin, ymax;
    float advance;
    float tW, tH;           // テクスチャ上のサイズ
};

// フォントアトラス管理クラス
// 同じフォント＋サイズで共有される
class FontAtlasManager {
public:
    FontAtlasManager();
    ~FontAtlasManager();

    // 初期化
    bool setup(const of::filesystem::path& fontPath, int fontSize, bool antialiased, int dpi = 0);

    // グリフを取得（なければ遅延ロード）
    const LazyGlyphProps* getOrLoadGlyph(uint32_t codepoint);

    // グリフが既にロード済みか
    bool hasGlyph(uint32_t codepoint) const;

    // テクスチャを取得
    const ofTexture& getTexture(size_t atlasIndex = 0) const;
    size_t getAtlasCount() const { return atlases.size(); }

    // フォントメトリクス
    float getLineHeight() const { return lineHeight; }
    float getAscenderHeight() const { return ascenderHeight; }
    float getDescenderHeight() const { return descenderHeight; }
    float getSpaceAdvance() const { return spaceAdvance; }

    // メモリ使用量を取得（バイト単位）
    size_t getMemoryUsage() const;

    // カーニング取得
    double getKerning(uint32_t leftC, uint32_t rightC) const;

    // グリフ数
    size_t getLoadedGlyphCount() const { return glyphs.size(); }

private:
    // FreeTypeハンドル
    shared_ptr<struct FT_FaceRec_> face;

    // テクスチャアトラス（動的に増える可能性あり）
    vector<ofTexture> atlases;
    vector<ofPixels> atlasPixels;  // CPU側のピクセルデータ（リサイズ用）

    // 各アトラスの現在の書き込み位置
    struct AtlasState {
        int currentX = 0;
        int currentY = 0;
        int currentRowHeight = 0;
        int width = 0;
        int height = 0;
    };
    vector<AtlasState> atlasStates;

    // ロード済みグリフ
    unordered_map<uint32_t, LazyGlyphProps> glyphs;

    // フォント設定
    int fontSize = 0;
    bool antialiased = true;
    int dpi = 96;

    // フォントメトリクス
    float lineHeight = 0;
    float ascenderHeight = 0;
    float descenderHeight = 0;
    float spaceAdvance = 0;
    float fontUnitScale = 1.0f;

    // アトラスサイズ管理
    int minAtlasSize = 256;
    int maxAtlasSize = 4096;  // GL_MAX_TEXTURE_SIZEから取得
    int border = 1;           // グリフ間のボーダー

    // グリフをラスタライズしてアトラスに追加
    bool addGlyphToAtlas(uint32_t codepoint, LazyGlyphProps& outProps);

    // 現在のアトラスを2倍に拡張
    bool expandAtlas(size_t atlasIndex);

    // 新しいアトラスを作成
    size_t createNewAtlas();

    // グリフのピクセルデータを取得
    bool rasterizeGlyph(uint32_t codepoint, ofPixels& outPixels, LazyGlyphProps& outProps);

    // GL最大テクスチャサイズを取得
    static int getMaxTextureSize();
};

// 共有フォントキャッシュ（シングルトン）
class SharedFontCache {
public:
    static SharedFontCache& getInstance();

    // フォントアトラスを取得（なければ作成）
    shared_ptr<FontAtlasManager> getOrCreate(const FontCacheKey& key, int dpi = 0);

    // 特定のフォントを解放
    void release(const FontCacheKey& key);

    // 全て解放
    void clear();

    // 総メモリ使用量
    size_t getTotalMemoryUsage() const;

private:
    SharedFontCache() = default;
    unordered_map<FontCacheKey, shared_ptr<FontAtlasManager>, FontCacheKeyHash> cache;
};

// メインクラス：ofTrueTypeFontを継承して互換性を保つ
class ofxTrueTypeFontLowRAM : public ofTrueTypeFont {
public:
    ofxTrueTypeFontLowRAM();
    virtual ~ofxTrueTypeFontLowRAM();

    // コピー・ムーブ
    ofxTrueTypeFontLowRAM(const ofxTrueTypeFontLowRAM& other);
    ofxTrueTypeFontLowRAM& operator=(const ofxTrueTypeFontLowRAM& other);
    ofxTrueTypeFontLowRAM(ofxTrueTypeFontLowRAM&& other) noexcept;
    ofxTrueTypeFontLowRAM& operator=(ofxTrueTypeFontLowRAM&& other) noexcept;

    // フォントロード（親クラスのloadを隠蔽）
    bool load(const of::filesystem::path& filename,
              int fontsize,
              bool _bAntiAliased = true,
              bool _bFullCharacterSet = true,  // この引数は無視（遅延ロードなので）
              bool makeContours = false,       // 未サポート
              float simplifyAmt = 0.0f,
              int dpi = 0);

    bool load(const ofTrueTypeFontSettings& settings);

    // 描画（オーバーライドではなく隠蔽）
    void drawString(const string& s, float x, float y) const;

    // 文字列サイズ計算（隠蔽）
    float stringWidth(const string& s) const;
    float stringHeight(const string& s) const;
    ofRectangle getStringBoundingBox(const string& s, float x, float y, bool vflip = true) const;

    // メッシュ取得
    const ofMesh& getStringMesh(const string& s, float x, float y, bool vFlipped = true) const;

    // テクスチャ取得（最初のアトラス）
    const ofTexture& getFontTexture() const;

    // 全アトラス取得
    const vector<ofTexture>& getAllTextures() const;
    size_t getAtlasCount() const;

    // メモリ使用量（このフォントが使用している分）
    size_t getMemoryUsage() const;

    // 共有キャッシュ全体のメモリ使用量
    static size_t getTotalCacheMemoryUsage();

    // ロード済みグリフ数
    size_t getLoadedGlyphCount() const;

    // 有効なグリフかチェック（遅延ロードなので常にtrueを返す傾向）
    bool isValidGlyph(uint32_t glyph) const;

private:
    shared_ptr<FontAtlasManager> atlasManager;
    FontCacheKey cacheKey;

    // 描画用の一時メッシュ
    mutable ofMesh tempMesh;

    // 複数アトラス対応の描画用メッシュ
    mutable vector<ofMesh> meshesPerAtlas;

    // 内部描画ヘルパー
    void createStringMeshInternal(const string& s, float x, float y, bool vFlipped) const;
    void drawCharInternal(uint32_t c, float x, float y, bool vFlipped) const;

    // 文字列を反復処理
    void iterateStringInternal(const string& str, float x, float y, bool vFlipped,
                               function<void(uint32_t, glm::vec2)> f) const;
};
