#ifndef MAP_H
#define MAP_H

#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"
#include "common.h"

// تهيئة مصفوفة التعديلات
void init_map(void);

// الحصول على نوع المكعب عند الإحداثيات (x, y, z)
BlockType get_block_at(int x, int y, int z);

// تعديل أو بناء مكعب عند الإحداثيات (x, y, z)
void set_block_at(int x, int y, int z, BlockType type);

// رسم العالم ثلاثي الأبعاد حول موقع اللاعب الحالي (Render Distance)
void draw_map(float player_x, float player_y, float player_z);

// البحث عن أعلى مكعب صلب عند العمود (x, z)
int get_highest_solid_block_y(int x, int z);

// حفظ حالة العالم ثلاثي الأبعاد وموقع اللاعب
bool save_world(const char* filepath, float player_x, float player_y, float player_z);

// تحميل حالة العالم ثلاثي الأبعاد وموقع اللاعب
bool load_world(const char* filepath, float* player_x, float* player_y, float* player_z);

#endif // MAP_H