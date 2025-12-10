# ofxTrueTypeFontLowRAM 開発ガイド

openFrameworks用の省メモリ日本語フォントアドオン。

## ビルド方法

```bash
cd example
xcodebuild -project example.xcodeproj -scheme "example Debug" -configuration Debug build
```

実行:
```bash
./bin/exampleDebug.app/Contents/MacOS/exampleDebug
# または
open bin/exampleDebug.app
```

## アーキテクチャ

### クラス構成

```
ofxTrueTypeFontLowRAM (ユーザー向けAPI)
    └── FontAtlasManager (フォント＋サイズごとに1つ、共有される)
            ├── FT_Face (FreeTypeフォント)
            ├── atlases[] (テクスチャアトラス、動的拡張)
            └── glyphs{} (ロード済みグリフのマップ)

SharedFontCache (シングルトン)
    └── cache{FontCacheKey -> FontAtlasManager}
```

### 主要ファイル

- `src/ofxTrueTypeFontLowRAM.h` - 全クラス定義
- `src/ofxTrueTypeFontLowRAM.cpp` - 実装
- `example/src/ofApp.cpp` - デモアプリ

## 重要な実装詳細

### グリフ位置計算 (lines 322-326)

FreeTypeのグリフ位置は `bitmap_left` と `bitmap_top` を使う:
```cpp
xmin = face->glyph->bitmap_left;    // bearingXではない！
ymin = -face->glyph->bitmap_top;    // 符号反転が必要
```

`metrics.horiBearingX/Y` は26.6固定小数点で別の意味なので使わないこと。

### drawStringのY座標

Y座標は**ベースライン**の位置。文字の左上ではない。
行間は `getLineHeight()` を使って計算する。

### static変数の破棄順序問題

`ftLibrary`（グローバルstatic）と`SharedFontCache`（シングルトン）の破棄順序が不定。
`FT_Done_Face`を呼ぶ前に`ftLibrary != nullptr`をチェックしている。

### テクスチャアトラス拡張

1. 初期サイズ: `fontSize * 4`（2の累乗に切り上げ、最小64）
2. グリフが入らなくなったら2倍に拡張
3. `GL_MAX_TEXTURE_SIZE`に達したら新しいアトラスを作成
4. 拡張時はCPU側ピクセルをコピーしてから再アップロード

## macOSシステムフォント

システムフォント名からパスを解決する`osxFontPathByName()`を実装済み:
- `"HiraMinProN-W3"` → `/System/Library/Fonts/ヒラギノ明朝 ProN.ttc`

CoreTextの`CTFontDescriptorCopyAttribute`を使用。

## 既知の制限

- `makeContours`（アウトライン取得）は未サポート
- Windows/Linuxのシステムフォント解決は未実装
- LRU eviction（古いグリフの破棄）は未実装

## デバッグ

`ofLogToConsole()`を`setup()`で呼ぶとターミナルにログが出る。
アドオン内のログは`[notice ] ofxTrueTypeFontLowRAM:`プレフィックス。

## テスト用キー操作（example）

- `1-5`: テキスト切り替え
- `S`: 統計表示切り替え
- `A`: アトラステクスチャ表示切り替え
