#pragma once

#include "ofMain.h"
#include "ofxTrueTypeFontLowRAM.h"

class ofApp : public ofBaseApp {
public:
    void setup();
    void update();
    void draw();
    void keyPressed(int key);

private:
    // 遅延ロードフォント
    ofxTrueTypeFontLowRAM fontSmall;
    ofxTrueTypeFontLowRAM fontMedium;
    ofxTrueTypeFontLowRAM fontLarge;

    // 同じフォント・サイズで複数インスタンス（共有テスト）
    ofxTrueTypeFontLowRAM fontShared1;
    ofxTrueTypeFontLowRAM fontShared2;

    // 通常のofTrueTypeFont（比較用）
    ofTrueTypeFont fontNormal;

    // 描画する文字列
    string testStrings[5];
    int currentStringIndex = 0;

    // 統計表示
    bool showStats = true;

    // アトラステクスチャ表示
    bool showAtlas = false;
};
