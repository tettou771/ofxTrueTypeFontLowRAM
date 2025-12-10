# ofxTrueTypeFontLowRAM

openFrameworks用の省メモリTrueTypeFontアドオン。日本語など大規模文字セットを扱う際のメモリ問題を解決する。

## 特徴

- **遅延ロード**: 描画時に必要なグリフのみラスタライズ
- **共有テクスチャキャッシュ**: 同じフォント＋サイズの複数インスタンスでアトラスを共有
- **動的アトラス拡張**: 小さく始めて必要に応じて自動拡張
- **ofTrueTypeFont互換API**: 既存コードからの移行が容易

## 動機

標準の`ofTrueTypeFont`は`load()`時に指定されたUnicode範囲の全グリフをラスタライズする。日本語（JIS第1・第2水準で約7000字）を含めると、フォントサイズによっては数百MBのメモリを消費する。

このアドオンは実際に描画する文字だけをオンデマンドでロードすることで、メモリ使用量を大幅に削減する。

## インストール

`addons/`フォルダにクローンまたはコピー:

```bash
cd openframeworks/addons
git clone https://github.com/tettou771/ofxTrueTypeFontLowRAM.git
```

## 使い方

```cpp
#include "ofxTrueTypeFontLowRAM.h"

class ofApp : public ofBaseApp {
    ofxTrueTypeFontLowRAM font;

    void setup() {
        // システムフォント名またはパスで指定
        font.load("HiraMinProN-W3", 24);

        // または data/ フォルダ内のフォントファイル
        // font.load("myfont.ttf", 24);
    }

    void draw() {
        ofSetColor(255);
        font.drawString("こんにちは世界", 100, 100);
    }
};
```

## API

### ofxTrueTypeFontLowRAM

```cpp
// フォントロード
bool load(const string& fontPath, int fontSize, bool antialiased = true);

// 描画
void drawString(const string& s, float x, float y) const;

// メトリクス（ofTrueTypeFontと同じ）
float getLineHeight() const;
float stringWidth(const string& s) const;
ofRectangle getStringBoundingBox(const string& s, float x, float y) const;

// メモリ情報
size_t getMemoryUsage() const;           // このインスタンスが使用するメモリ
static size_t getTotalCacheMemoryUsage(); // 共有キャッシュ全体のメモリ
size_t getLoadedGlyphCount() const;       // ロード済みグリフ数
```

### テクスチャアトラスへのアクセス

```cpp
const ofTexture& getFontTexture() const;       // 最初のアトラス
const vector<ofTexture>& getAllTextures() const; // 全アトラス
size_t getAtlasCount() const;
```

## 共有キャッシュ

同じフォント＋サイズ＋アンチエイリアス設定のインスタンスはテクスチャアトラスを共有する:

```cpp
ofxTrueTypeFontLowRAM font1, font2;
font1.load("HiraMinProN-W3", 24);
font2.load("HiraMinProN-W3", 24);  // font1とアトラスを共有

font1.drawString("あいう", 100, 100);
font2.drawString("えお", 100, 150);  // 「あいう」は既にロード済み
```

## メモリ比較（目安）

| 条件 | ofTrueTypeFont | ofxTrueTypeFontLowRAM |
|------|----------------|----------------------|
| 日本語フォント32px、100文字使用 | ~50MB | ~1MB |
| 日本語フォント64px、100文字使用 | ~200MB | ~4MB |

※実際の値はフォントと使用する文字により異なる

## ofTrueTypeFontとの違い

### Unicode範囲指定は無視される

`ofTrueTypeFontSettings::addRange()` や `ranges` の設定は無視される。遅延ロードでは事前に範囲を指定する必要がないため。

```cpp
ofTrueTypeFontSettings settings("myfont.ttf", 24);
settings.addRange(ofUnicode::Latin);  // 無視される（警告ログが出る）
font.load(settings);

font.drawString("日本語", 100, 100);  // 問題なく描画される
```

## 互換性

- openFrameworks 0.12.x
- macOS（システムフォント解決対応）
- Windows/Linux（ファイルパス指定のみ）

## ライセンス

MIT License
