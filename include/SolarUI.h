/*
    SolarUI
    Lightweight UI framework for FreeGLUT/OpenGL

    Copyright (c) 2026 Ellie-Mae Dawson

    Licensed under the MIT License
*/
#ifndef SOLAR_UI_H
#define SOLAR_UI_H

#include <GL/freeglut.h>
#include <string>

namespace SolarUI
{
    // **********************************************
    // *              GLOBAL INPUT STATE            *
    // **********************************************
    inline float MouseX = 0.0f;
    inline float MouseY = 0.0f;

    inline bool MouseDown = false;
    inline bool MousePressed = false;

    inline int WindowWidth = 800;
    inline int WindowHeight = 600;
    inline float LogicalWidth = 800.0f;
    inline float LogicalHeight = 600.0f;
    inline float ViewScale = 1.0f;
    inline float ViewOffsetX = 0.0f;
    inline float ViewOffsetY = 0.0f;


    // **********************************************
    // *              GLOBAL FONT SYSTEM            *
    // **********************************************

    inline void* CurrentFont = GLUT_BITMAP_HELVETICA_18;

    inline void SetFont(void* font)
    {
        CurrentFont = font;
    }


    // **********************************************
    // *              CORE FUNCTIONS               *
    // **********************************************
    int Init();
    int Update();
    int DrawFrame();
    void SetLogicalSize(float width, float height);
    void UpdateViewport(int width, int height);
    void ScreenToLogical(int screenX, int screenY, float& outX, float& outY);


    // **********************************************
    // *                  ELEMENTS                  *
    // **********************************************
    struct Label
    {
        const char* Text;
        float X, Y;

        Label(const char* text = "", float x = 0, float y = 0)
            : Text(text), X(x), Y(y) {}

        void Draw();
    };

    struct Button
    {
        float X, Y, W, H;
        Label Title;

        Button(float x, float y, float w, float h, const char* text)
            : X(x), Y(y), W(w), H(h), Title(text, x, y) {}

        void Draw();
        bool Contains(int mx, int my);
        bool IsClicked();
    };

    struct Slider
    {
        float X, Y, W, H;
        float Value;

        Label Title;

        Slider(float x, float y, float w, float h, const char* text)
            : X(x), Y(y), W(w), H(h),
              Value(0.5f),
              Title(text, x, y) {}

        void Draw();
        void Update();
        bool Contains(int mx, int my);
    };

    struct Checkbox
    {
        float X, Y, W, H;
        bool Checked;

        Label Title;

        Checkbox(float x, float y, float w, float h, const char* text)
            : X(x), Y(y), W(w), H(h),
              Checked(false),
              Title(text, x, y) {}

        void Draw();
        void Update();
        bool Contains(int mx, int my);
    };

    struct image
    {
        float X, Y, W, H;
        GLuint TextureID;
        std::string FilePath;
        bool TextureLoaded;

        image(float x, float y, float w, float h, GLuint textureID)
            : X(x), Y(y), W(w), H(h), TextureID(textureID), FilePath(""), TextureLoaded(true) {}

        image(float x, float y, float w, float h, const char* filePath)
            : X(x), Y(y), W(w), H(h), TextureID(0), FilePath(filePath ? filePath : ""), TextureLoaded(false) {}

        void Draw();
        void LoadTexture();
    };
}

#endif