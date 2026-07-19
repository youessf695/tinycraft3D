#include "textures.h"
#include "common.h"  // 🔥 هذا هو السطر الناقص الذي سيحل المشكلة فوراً!
#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"
#include <stdio.h>

GLuint block_textures[MAX_TEXTURES] = {0};

static GLuint load_texture_to_gl(const char* filepath) {
    // 1. تحميل الصورة كـ SDL_Surface مؤقت
    SDL_Surface* temp_surface = SDL_LoadBMP(filepath);
    if (!temp_surface) {
        printf("Failed to load image %s: %s\n", filepath, SDL_GetError());
        return 0;
    }

    // 2. التحويل الإجباري إلى تنسيق RGBA 32-bit لضمان 4 بايت لكل بكسل بشكل قياسي
    SDL_Surface* formatted_surface = SDL_ConvertSurfaceFormat(temp_surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(temp_surface); // لم نعد بحاجة للسطح القديم غير المتوافق
    
    if (!formatted_surface) {
        printf("Failed to convert surface format for %s: %s\n", filepath, SDL_GetError());
        return 0;
    }

    // 3. توليد المعرف وتفعيل الأنسجة في OpenGL
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    // 4. منع أي انحراف خطي عبر ضبط محاذاة البيانات على 1 بايت
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // 5. رفع البيانات إلى كرت الشاشة بتنسيق RGBA حقيقي وآمن
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        formatted_surface->w,
        formatted_surface->h,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        formatted_surface->pixels
    );

    // 6. ضبط فلاتر الأنسجة لتكون حادة ومكسلة (Retro Pixel-Art) وتمنع الضبابية والتشويه
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // 7. تفريغ الذاكرة المؤقتة من RAM
    SDL_FreeSurface(formatted_surface);

    return texture_id;
}

bool load_all_textures(void) {
    glEnable(GL_TEXTURE_2D);

    block_textures[BLOCK_GRASS]  = load_texture_to_gl("res/grass.bmp");     // 1 -> الوجه العلوي
    block_textures[BLOCK_STONE]  = load_texture_to_gl("res/stone.bmp");     // 2 -> الحجر
    block_textures[BLOCK_WOOD]   = load_texture_to_gl("res/wood.bmp");      // 3 -> الخشب
    block_textures[BLOCK_DIRT]   = load_texture_to_gl("res/dirt.bmp");      // 4 -> التربة والوجه السفلي للعشب
    block_textures[BLOCK_BRICKS] = load_texture_to_gl("res/bricks.bmp");    // 5 -> الطوب الأحمر
    block_textures[6]            = load_texture_to_gl("res/dirtgrass.bmp"); // 6 -> جوانب العشب الأربعة

    // الفولباك الاحتياطي (إذا كانت الملفات بجانب الـ exe مباشرة)
    if (block_textures[1] == 0) block_textures[1] = load_texture_to_gl("grass.bmp");
    if (block_textures[2] == 0) block_textures[2] = load_texture_to_gl("stone.bmp");
    if (block_textures[3] == 0) block_textures[3] = load_texture_to_gl("wood.bmp");
    if (block_textures[4] == 0) block_textures[4] = load_texture_to_gl("dirt.bmp");
    if (block_textures[5] == 0) block_textures[5] = load_texture_to_gl("bricks.bmp");
    if (block_textures[6] == 0) block_textures[6] = load_texture_to_gl("dirtgrass.bmp");

    return true;
}

void free_all_textures(void) {
    for (int i = 0; i < MAX_TEXTURES; i++) {
        if (block_textures[i] != 0) {
            glDeleteTextures(1, &block_textures[i]);
            block_textures[i] = 0;
        }
    }
}