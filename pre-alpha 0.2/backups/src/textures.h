#ifndef TEXTURES_H
#define TEXTURES_H

#include "SDL2/SDL_opengl.h"
#include <stdbool.h>

// 1. تحديد الحد الأقصى لعدد الأنسجة (قمنا بوضع 10 كحد أقصى وهو كافٍ جداً حالياً)
#define MAX_TEXTURES 10

// 2. مصفوفة تخزين معرفات الأنسجة في OpenGL لتكون مرئية لكل الملفات
extern GLuint block_textures[MAX_TEXTURES];

// 3. الإعلان عن دوال تحميل وتحرير الأنسجة
bool load_all_textures(void);
void free_all_textures(void);

#endif // TEXTURES_H