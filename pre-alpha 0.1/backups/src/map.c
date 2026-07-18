#include "map.h"
#include "textures.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MAX_MODIFIED_BLOCKS 10000

Block modified_blocks[MAX_MODIFIED_BLOCKS];
int num_modified_blocks = 0;

void init_map(void) {
    num_modified_blocks = 0;
}

// الحصول على نوع المكعب في العالم ثلاثي الأبعاد اللانهائي
BlockType get_block_at(int x, int y, int z) {
    // 1. البحث أولاً في التعديلات التي قام بها اللاعب
    for (int i = 0; i < num_modified_blocks; i++) {
        if (modified_blocks[i].x == x && modified_blocks[i].y == y && modified_blocks[i].z == z) {
            return modified_blocks[i].type;
        }
    }

    // 2. التوليد التلقائي لسطح الأرض الافتراضي
    if (y > GROUND_LEVEL) {
        return BLOCK_AIR; // فوق السطح هواء
    }
    if (y == GROUND_LEVEL) {
        return BLOCK_GRASS; // السطح عشب
    }
    if (y < GROUND_LEVEL && y >= MAP_DEPTH_LIMIT) {
        return BLOCK_STONE; // تحت السطح حجر صلب
    }

    return BLOCK_AIR; // ما تحت حدود الهاوية هواء (فراغ)
}

// تعديل أو وضع مكعب جديد
void set_block_at(int x, int y, int z, BlockType type) {
    for (int i = 0; i < num_modified_blocks; i++) {
        if (modified_blocks[i].x == x && modified_blocks[i].y == y && modified_blocks[i].z == z) {
            modified_blocks[i].type = type;
            return;
        }
    }

    if (num_modified_blocks < MAX_MODIFIED_BLOCKS) {
        modified_blocks[num_modified_blocks].x = x;
        modified_blocks[num_modified_blocks].y = y;
        modified_blocks[num_modified_blocks].z = z;
        modified_blocks[num_modified_blocks].type = type;
        num_modified_blocks++;
    } else {
        printf("Warning: Modified blocks limit reached!\n");
    }
}

// دالة داخلية ذكية لرسم مكعب واحد مجسم بـ 6 أوجه كاملة مع Face Shading
void draw_cube(float x, float y, float z, GLuint texture) {
    if (texture == 0) {
        glDisable(GL_TEXTURE_2D);
        glColor3f(1.0f, 0.0f, 1.0f); 
    } else {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    glBegin(GL_QUADS);

    // 1. الوجه الأمامي (Front) - إضاءة متوسطة
    if (texture != 0) glColor3f(0.8f, 0.8f, 0.8f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x,       y,       z + 1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x + 1.0f, y,       z + 1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x,       y + 1.0f, z + 1.0f);

    // 2. الوجه الخلفي (Back) - إضاءة متوسطة
    if (texture != 0) glColor3f(0.8f, 0.8f, 0.8f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x,       y,       z);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x,       y + 1.0f, z);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x + 1.0f, y + 1.0f, z);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x + 1.0f, y,       z);

    // 3. الوجه العلوي (Top) - إضاءة كاملة (أفتح وجه)
    if (texture != 0) glColor3f(1.0f, 1.0f, 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x,       y + 1.0f, z);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x,       y + 1.0f, z + 1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x + 1.0f, y + 1.0f, z);

    // 4. الوجه السفلي (Bottom) - إضاءة خافتة (أغمق وجه)
    if (texture != 0) glColor3f(0.5f, 0.5f, 0.5f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x,       y,       z);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x + 1.0f, y,       z);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x + 1.0f, y,       z + 1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x,       y,       z + 1.0f);

    // 5. الوجه الأيمن (Right) - إضاءة متوسطة
    if (texture != 0) glColor3f(0.8f, 0.8f, 0.8f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x + 1.0f, y,       z);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x + 1.0f, y + 1.0f, z);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x + 1.0f, y + 1.0f, z + 1.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x + 1.0f, y,       z + 1.0f);

    // 6. الوجه الأيسر (Left) - إضاءة متوسطة
    if (texture != 0) glColor3f(0.8f, 0.8f, 0.8f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(x,       y,       z);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(x,       y,       z + 1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(x,       y + 1.0f, z + 1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(x,       y + 1.0f, z);

    glEnd();
}

// رسم العالم ثلاثي الأبعاد بمفهوم مسافة الرؤية (Render Distance)
void draw_map(float player_x, float player_y, float player_z) {
    // 💡 السحر هنا: جعلنا المدى العمودي (16) مساوياً تماماً للمدى الأفقي (16)
    // هذا يجعل حدود الرسم عبارة عن مكعب متناسق يحيط بكروية الضباب تماماً!
    int range_xz = 16; 
    int range_y = 16; // تم تعديلها من 10 إلى 16 لحل مشكلة الاختفاء العمودي

    int start_x = (int)floor(player_x) - range_xz;
    int end_x = (int)ceil(player_x) + range_xz;

    int start_y = (int)floor(player_y) - range_y;
    int end_y = (int)ceil(player_y) + range_y;

    int start_z = (int)floor(player_z) - range_xz;
    int end_z = (int)ceil(player_z) + range_xz;

    // حصر حدود الارتفاع
    if (start_y < MAP_DEPTH_LIMIT) start_y = MAP_DEPTH_LIMIT;
    if (end_y > GROUND_LEVEL + 250) end_y = GROUND_LEVEL + 250; 

    glEnable(GL_TEXTURE_2D);

    for (int x = start_x; x <= end_x; x++) {
        for (int y = start_y; y <= end_y; y++) {
            for (int z = start_z; z <= end_z; z++) {
                BlockType type = get_block_at(x, y, z);
                if (type == BLOCK_AIR) continue;

                GLuint tex = block_textures[type];
                draw_cube((float)x, (float)y, (float)z, tex);
            }
        }
    }

    glDisable(GL_TEXTURE_2D);
}

// إيجاد أعلى نقطة صلبة في العمود عند الإحداثيات (x, z)
int get_highest_solid_block_y(int x, int z) {
    for (int y = GROUND_LEVEL; y >= MAP_DEPTH_LIMIT; y--) {
        if (get_block_at(x, y, z) != BLOCK_AIR) {
            return y;
        }
    }
    return GROUND_LEVEL;
}

// حفظ تقدم العالم الإحداثيات الثلاثية وموقع اللاعب
bool save_world(const char* filepath, float player_x, float player_y, float player_z) {
    FILE* file = fopen(filepath, "wb");
    if (file == NULL) {
        printf("Failed to open file for saving: %s\n", filepath);
        return false;
    }

    fwrite(&player_x, sizeof(float), 1, file);
    fwrite(&player_y, sizeof(float), 1, file);
    fwrite(&player_z, sizeof(float), 1, file);

    fwrite(&num_modified_blocks, sizeof(int), 1, file);

    if (num_modified_blocks > 0) {
        fwrite(modified_blocks, sizeof(Block), num_modified_blocks, file);
    }

    fclose(file);
    printf("3D World saved successfully to %s!\n", filepath);
    return true;
}

// تحميل العالم ثلاثي الأبعاد وموقع اللاعب
bool load_world(const char* filepath, float* player_x, float* player_y, float* player_z) {
    FILE* file = fopen(filepath, "rb");
    if (file == NULL) {
        return false;
    }

    fread(player_x, sizeof(float), 1, file);
    fread(player_y, sizeof(float), 1, file);
    fread(player_z, sizeof(float), 1, file);

    fread(&num_modified_blocks, sizeof(int), 1, file);

    if (num_modified_blocks > 0) {
        fread(modified_blocks, sizeof(Block), num_modified_blocks, file);
    }

    fclose(file);
    printf("3D World loaded successfully from %s!\n", filepath);
    return true;
}