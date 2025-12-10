#include "ofApp.h"

void ofApp::setup() {
    ofLogToConsole();
    ofSetFrameRate(60);
    ofBackground(30);

    // テスト用の文字列
    testStrings[0] = "Hello, World! 1234567890";
    testStrings[1] = "こんにちは、世界！日本語テスト";
    testStrings[2] = "The quick brown fox jumps over the lazy dog.";
    testStrings[3] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz";
    testStrings[4] = "漢字ひらがなカタカナ混在テスト 123 ABC";

    // 遅延ロードフォント - 異なるサイズ
    // macOSではシステムフォント名を直接指定
#ifdef TARGET_OS_MAC
    string fontPath = "HiraMinProN-W3";  // macOS日本語フォント（ヒラギノ明朝）
#elif defined WIN32
    string fontPath = "Meiryo.ttf";
#else
    string fontPath = OF_TTF_SANS;
#endif

    ofLogNotice("ofApp") << "フォントをロード: " << fontPath;

    // 異なるサイズでロード
    fontSmall.load(fontPath, 16, true);
    fontMedium.load(fontPath, 32, true);
    fontLarge.load(fontPath, 64, true);

    // 同じフォント・サイズで2つのインスタンス（共有テスト）
    fontShared1.load(fontPath, 32, true);
    fontShared2.load(fontPath, 32, true);

    // 比較用の通常フォント（小さい文字セットのみ）
    // 注意: 日本語を含む全文字セットをロードするとメモリを大量消費
    fontNormal.load(fontPath, 32, true, false);  // fullCharSet = false

    ofLogNotice("ofApp") << "セットアップ完了";
}

void ofApp::update() {
}

void ofApp::draw() {
    float y = 50;
    string currentStr = testStrings[currentStringIndex];

    // タイトル
    ofSetColor(255, 200, 100);
    fontSmall.drawString("ofxTrueTypeFontLowRAM Example", 20, y);
    y += 40;

    // 操作説明
    ofSetColor(150);
    fontSmall.drawString("[1-5] テキスト切替  [S] 統計表示  [A] アトラス表示", 20, y);
    y += 50;

    // 現在のテスト文字列を表示
    ofSetColor(255);
	y += fontSmall.getLineHeight();
    fontSmall.drawString("Small (16px):", 20, y);
    ofSetColor(255);
	y += fontSmall.getLineHeight();
    fontSmall.drawString(currentStr, 40, y);

    ofSetColor(255);
	y += fontSmall.getLineHeight();
    fontSmall.drawString("Medium (32px):", 20, y);
    ofSetColor(255);
	y += fontMedium.getLineHeight();
    fontMedium.drawString(currentStr, 40, y);

    ofSetColor(255);
	y += fontSmall.getLineHeight();
    fontSmall.drawString("Large (64px):", 20, y);
    ofSetColor(255);
	y += fontLarge.getLineHeight();  // Largeのベースライン分を確保
    fontLarge.drawString(currentStr, 40, y);

    // 共有テスト
    ofSetColor(200, 255, 200);
	y += fontSmall.getLineHeight();
    fontSmall.drawString("共有テスト (同じフォント・サイズの2インスタンス):", 20, y);
    ofSetColor(255);
	y += fontMedium.getLineHeight() + 5;
    fontShared1.drawString("Instance 1: " + currentStr.substr(0, 10), 40, y);
	y += fontMedium.getLineHeight() + 5;
    fontShared2.drawString("Instance 2: " + currentStr.substr(0, 10), 40, y);

    // 統計表示
    if (showStats) {
        ofSetColor(100, 200, 255);
        y += 20;
        fontSmall.drawString("--- Statistics ---", 20, y);
        y += 25;

        // 各フォントの情報
        stringstream ss;
        ss << "fontSmall:  " << fontSmall.getLoadedGlyphCount() << " glyphs, "
           << fontSmall.getAtlasCount() << " atlas(es), "
           << (fontSmall.getMemoryUsage() / 1024) << " KB";
        fontSmall.drawString(ss.str(), 20, y);
        y += 22;

        ss.str("");
        ss << "fontMedium: " << fontMedium.getLoadedGlyphCount() << " glyphs, "
           << fontMedium.getAtlasCount() << " atlas(es), "
           << (fontMedium.getMemoryUsage() / 1024) << " KB";
        fontSmall.drawString(ss.str(), 20, y);
        y += 22;

        ss.str("");
        ss << "fontLarge:  " << fontLarge.getLoadedGlyphCount() << " glyphs, "
           << fontLarge.getAtlasCount() << " atlas(es), "
           << (fontLarge.getMemoryUsage() / 1024) << " KB";
        fontSmall.drawString(ss.str(), 20, y);
        y += 22;

        ss.str("");
        ss << "fontShared1 & 2: " << fontShared1.getLoadedGlyphCount() << " glyphs (shared), "
           << (fontShared1.getMemoryUsage() / 1024) << " KB";
        fontSmall.drawString(ss.str(), 20, y);
        y += 30;

        // 総メモリ使用量
        ofSetColor(255, 200, 100);
        ss.str("");
        ss << "Total Cache Memory: " << (ofxTrueTypeFontLowRAM::getTotalCacheMemoryUsage() / 1024) << " KB";
        fontSmall.drawString(ss.str(), 20, y);
        y += 25;

        ofSetColor(150);
        ss.str("");
        ss << "FPS: " << ofToString(ofGetFrameRate(), 1);
        fontSmall.drawString(ss.str(), 20, y);
    }

    // アトラステクスチャ表示
    if (showAtlas) {
        ofSetColor(255);

        // 右側にアトラスを表示
        float atlasX = ofGetWidth() - 280;
        float atlasY = 50;

        fontSmall.drawString("Atlas Textures:", atlasX, atlasY);
        atlasY += 30;

        // fontSmallのアトラス
        const ofTexture& texSmall = fontSmall.getFontTexture();
        if (texSmall.isAllocated()) {
            float scale = 128.0f / max(texSmall.getWidth(), texSmall.getHeight());
            ofSetColor(255);
            texSmall.draw(atlasX, atlasY, texSmall.getWidth() * scale, texSmall.getHeight() * scale);

            ofSetColor(100);
            stringstream ss;
            ss << "Small: " << texSmall.getWidth() << "x" << texSmall.getHeight();
            fontSmall.drawString(ss.str(), atlasX, atlasY + texSmall.getHeight() * scale + 15);
            atlasY += texSmall.getHeight() * scale + 35;
        }

        // fontMediumのアトラス
        const ofTexture& texMedium = fontMedium.getFontTexture();
        if (texMedium.isAllocated()) {
            float scale = 128.0f / max(texMedium.getWidth(), texMedium.getHeight());
            ofSetColor(255);
            texMedium.draw(atlasX, atlasY, texMedium.getWidth() * scale, texMedium.getHeight() * scale);

            ofSetColor(100);
            stringstream ss;
            ss << "Medium: " << texMedium.getWidth() << "x" << texMedium.getHeight();
            fontSmall.drawString(ss.str(), atlasX, atlasY + texMedium.getHeight() * scale + 15);
            atlasY += texMedium.getHeight() * scale + 35;
        }

        // fontLargeのアトラス
        const ofTexture& texLarge = fontLarge.getFontTexture();
        if (texLarge.isAllocated()) {
            float scale = 128.0f / max(texLarge.getWidth(), texLarge.getHeight());
            ofSetColor(255);
            texLarge.draw(atlasX, atlasY, texLarge.getWidth() * scale, texLarge.getHeight() * scale);

            ofSetColor(100);
            stringstream ss;
            ss << "Large: " << texLarge.getWidth() << "x" << texLarge.getHeight();
            fontSmall.drawString(ss.str(), atlasX, atlasY + texLarge.getHeight() * scale + 15);
        }
    }
}

void ofApp::keyPressed(int key) {
    if (key >= '1' && key <= '5') {
        currentStringIndex = key - '1';
        ofLogNotice("ofApp") << "テキスト切替: " << currentStringIndex;
    } else if (key == 's' || key == 'S') {
        showStats = !showStats;
    } else if (key == 'a' || key == 'A') {
        showAtlas = !showAtlas;
    }
}
