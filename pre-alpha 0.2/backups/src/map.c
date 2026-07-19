#include "map.h"
#include "textures.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>

#ifndef APIENTRY
#define APIENTRY
#endif

// --- تعريف أنواع مؤشرات دالات VBO الخاصة بـ OpenGL لضمان التجميع بدون مكتبات خارجية ---
typedef void (APIENTRY * PFNGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef void (APIENTRY * PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRY * PFNGLBUFFERDATAPROC) (GLenum target, ptrdiff_t size, const GLvoid *data, GLenum usage);
typedef void (APIENTRY * PFNGLDELETEBUFFERSPROC) (GLsizei n, const GLuint *buffers);

static PFNGLGENBUFFERSPROC glGenBuffers_ptr = NULL;
static PFNGLBINDBUFFERPROC glBindBuffer_ptr = NULL;
static PFNGLBUFFERDATAPROC glBufferData_ptr = NULL;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers_ptr = NULL;
int render_distance_chunks = 4; // القيمة الافتراضية الحالية (4 مقاطع دائرية)

// دالة داخلية لجلب الدالات ديناميكياً عبر SDL2
static void init_vbo_functions(void) {
    if (glGenBuffers_ptr != NULL) return; // تم التهيئة مسبقاً
    glGenBuffers_ptr   = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    glBindBuffer_ptr   = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    glBufferData_ptr   = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    glDeleteBuffers_ptr = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
}

// --- إعدادات هيكل البيانات للنظام الجديد (Chunks & VBO) ---
#define CHUNK_SIZE 16
#define MAX_CHUNKS 8192    // رفع الحد الأقصى للمقاطع
#define HASH_SIZE 16384    // رفع حجم جدول الهاش لضمان سرعة البحث

// هيكل رأس البوكسيل (Vertex) المخزن في الـ VBO
typedef struct {
    float x, y, z;    // الإحداثيات ثلاثية الأبعاد
    float u, v;       // إحداثيات التكستشر
    float r, g, b;    // الألوان لـ Face Shading
} ChunkVertex;

// هيكل المقطع (Chunk) ثلاثي الأبعاد
typedef struct {
    int cx, cy, cz;   // إحداثيات المقطع (World / 16)
    BlockType blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
    GLuint vbo_ids[MAX_TEXTURES];     // VBO منفصل لكل خامة لمنع تداخل التكستشرات
    int vertex_counts[MAX_TEXTURES];  // عدد النقاط لكل خامة
    bool is_dirty;                    // هل يحتاج المقطع لإعادة بناء المش VBO؟
    bool active;
} Chunk;

// ذاكرة تخزين المقاطع النشطة وجدول الهاش السريع O(1)
static Chunk chunks[MAX_CHUNKS];
static int num_chunks = 0;
static int chunk_hash_table[HASH_SIZE];

// الحفاظ على المصفوفة القديمة لضمان توافق ملفات الحفظ والتخزين 100%
#define MAX_MODIFIED_BLOCKS 10000
static Block modified_blocks[MAX_MODIFIED_BLOCKS];
static int num_modified_blocks = 0;

// بافر مؤقت ضخم لبناء أسطح المقطع (Mesh) قبل رفعها للـ VBO
static ChunkVertex temp_vertices[MAX_TEXTURES][24576]; 
static int temp_counts[MAX_TEXTURES];

// دالة الهاش الرياضية لتوزيع المقاطع بذكاء وسرعة
static int get_chunk_hash(int cx, int cy, int cz) {
    unsigned int hash = (unsigned int)cx * 73856093 ^ (unsigned int)cy * 19349663 ^ (unsigned int)cz * 83492791;
    return hash % HASH_SIZE;
}

// البحث عن مقطع مخزن بسرعة O(1)
static Chunk* get_chunk(int cx, int cy, int cz) {
    int hash = get_chunk_hash(cx, cy, cz);
    int start_hash = hash;
    while (chunk_hash_table[hash] != -1) {
        int idx = chunk_hash_table[hash];
        if (chunks[idx].cx == cx && chunks[idx].cy == cy && chunks[idx].cz == cz && chunks[idx].active) {
            return &chunks[idx];
        }
        hash = (hash + 1) % HASH_SIZE;
        if (hash == start_hash) break;
    }
    return NULL;
}

// توليد مقطع جديد وتعبئته بالتوليد الافتراضي للعالم ثلاثي الأبعاد
static Chunk* create_chunk(int cx, int cy, int cz) {
    Chunk* existing = get_chunk(cx, cy, cz);
    if (existing) return existing;

    int idx = -1;
    if (num_chunks < MAX_CHUNKS) {
        idx = num_chunks++;
    } else {
        // 🔥 [نظام إعادة التدوير الذكي - Garbage Collector]
        // إذا امتلأت الذاكرة، نبحث عن أبعد مقطع بالنسبة للموقع المطلوب حالياً لإعادة استخدامه
        int farthest_idx = -1;
        long max_dist = -1;
        
        for (int i = 0; i < MAX_CHUNKS; i++) {
            long dx = chunks[i].cx - cx;
            long dz = chunks[i].cz - cz;
            long dist = dx * dx + dz * dz;
            if (dist > max_dist) {
                max_dist = dist;
                farthest_idx = i;
            }
        }
        
        if (farthest_idx != -1) {
            idx = farthest_idx;
            // 1. تنظيف بافرات كرت الشاشة (VBO) للمقطع القديم فوراً لمنع تسريب الذاكرة (Memory Leak)
            for (int t = 0; t < MAX_TEXTURES; t++) {
                if (chunks[idx].vbo_ids[t] != 0) {
                    glDeleteBuffers_ptr(1, &chunks[idx].vbo_ids[t]);
                    chunks[idx].vbo_ids[t] = 0;
                }
            }
        } else {
            return NULL;
        }
        
        // 2. إعادة بناء جدول الهاش لضمان سلامة روابط الوصول السريع O(1) بعد تفريغ المقطع القديم
        for (int i = 0; i < HASH_SIZE; i++) chunk_hash_table[i] = -1;
        for (int i = 0; i < MAX_CHUNKS; i++) {
            if (i == idx) continue; // تخطي المقطع الذي يتم تجديده الآن
            int hash = get_chunk_hash(chunks[i].cx, chunks[i].cy, chunks[i].cz);
            while (chunk_hash_table[hash] != -1) {
                hash = (hash + 1) % HASH_SIZE;
            }
            chunk_hash_table[hash] = i;
        }
    }

    // تهيئة المقطع الجديد تماماً في المساحة التي استرديناها بأمان
    Chunk* c = &chunks[idx];
    c->cx = cx; c->cy = cy; c->cz = cz;
    c->active = true;
    c->is_dirty = true;
    
    for (int t = 0; t < MAX_TEXTURES; t++) {
        c->vbo_ids[t] = 0;
        c->vertex_counts[t] = 0;
    }

    // توليد تضاريس العالم الافتراضية داخل المقطع الجديد
    for (int bx = 0; bx < CHUNK_SIZE; bx++) {
        for (int by = 0; by < CHUNK_SIZE; by++) {
            for (int bz = 0; bz < CHUNK_SIZE; bz++) {
                int wy = cy * CHUNK_SIZE + by;
                BlockType type = BLOCK_AIR;
                
                if (wy > GROUND_LEVEL) type = BLOCK_AIR; 
                else if (wy == GROUND_LEVEL) type = BLOCK_GRASS; 
                else if (wy < GROUND_LEVEL && wy >= GROUND_LEVEL - 3) type = BLOCK_DIRT; 
                else if (wy < GROUND_LEVEL - 3 && wy >= MAP_DEPTH_LIMIT) type = BLOCK_STONE; 
                
                c->blocks[bx][by][bz] = type;
            }
        }
    }

    // تسجيل المقطع الجديد في جدول الهاش المحدث
    int hash = get_chunk_hash(cx, cy, cz);
    while (chunk_hash_table[hash] != -1) {
        hash = (hash + 1) % HASH_SIZE;
    }
    chunk_hash_table[hash] = idx;
    
    return c;
}

// دالة لتحديد الصورة المناسبة لكل وجه بناءً على نوع البلكة ورقم الوجه
// 0=أمام، 1=خلف، 2=أعلى، 3=أسفل، 4=يمين، 5=يسار
static int get_face_texture(BlockType type, int face_id) {
    switch(type) {
        case BLOCK_DIRT:   return BLOCK_DIRT;   // يعود بـ 4 (dirt.bmp)
        case BLOCK_STONE:  return BLOCK_STONE;  // يعود بـ 2 (stone.bmp)
        case BLOCK_WOOD:   return BLOCK_WOOD;   // يعود بـ 3 (wood.bmp)
        case BLOCK_BRICKS: return BLOCK_BRICKS; // يعود بـ 5 (bricks.bmp)
        case BLOCK_GRASS:
            if (face_id == 2) return BLOCK_GRASS; // الوجه العلوي -> 1 (grass.bmp)
            if (face_id == 3) return BLOCK_DIRT;  // الوجه السفلي -> 4 (dirt.bmp)
            return 6;                             // الأوجه الأربعة الجانبية -> 6 (dirtgrass.bmp)
        default: return 0;
    }
}

// تهيئة نظام الخريطة بالكامل وتصفير الجداول
void init_map(void) {
    num_modified_blocks = 0; //[cite: 3]
    num_chunks = 0;
    for (int i = 0; i < HASH_SIZE; i++) {
        chunk_hash_table[i] = -1;
    }
    init_vbo_functions();
}

// تحديث الفولباك التلقائي في get_block_at[cite: 3]
BlockType get_block_at(int x, int y, int z) {
    if (y > GROUND_LEVEL + 250 || y < MAP_DEPTH_LIMIT) return BLOCK_AIR; //[cite: 3]

    int cx = x >> 4; int cy = y >> 4; int cz = z >> 4; //[cite: 3]
    Chunk* c = get_chunk(cx, cy, cz); //[cite: 3]
    
    if (!c) {
        if (y > GROUND_LEVEL) return BLOCK_AIR; //[cite: 3]
        if (y == GROUND_LEVEL) return BLOCK_GRASS; //[cite: 3]
        if (y < GROUND_LEVEL && y >= GROUND_LEVEL - 3) return BLOCK_DIRT; // طبقة انتقالية طينية
        if (y < GROUND_LEVEL - 3 && y >= MAP_DEPTH_LIMIT) return BLOCK_STONE; // ثم الحجر الصلب[cite: 3]
        return BLOCK_AIR; //[cite: 3]
    }

    return c->blocks[x & 15][y & 15][z & 15]; //[cite: 3]
}

// دالة داخلية مساعدة لتطبيق التعديلات على البلوكات والمقاطع المحيطة
static void add_modification_to_chunk(int x, int y, int z, BlockType type) {
    int cx = x >> 4; int cy = y >> 4; int cz = z >> 4;
    Chunk* c = get_chunk(cx, cy, cz);
    if (!c) c = create_chunk(cx, cy, cz);
    
    if (c) {
        int bx = x & 15; int by = y & 15; int bz = z & 15;
        c->blocks[bx][by][bz] = type;
        c->is_dirty = true;

        // 💡 لمسة احترافية: إذا تم تعديل بلكة على حافة الـ Chunk، نقوم بوضع علامة Dirty للمقطع المجاور
        // لكي يعيد بناء نفسه ويقوم بعمل الـ Culling للأوجه المشتركة بين المقطعين بشكل صحيح!
        if (bx == 0)  { Chunk* n = get_chunk(cx - 1, cy, cz); if (n) n->is_dirty = true; }
        if (bx == 15) { Chunk* n = get_chunk(cx + 1, cy, cz); if (n) n->is_dirty = true; }
        if (by == 0)  { Chunk* n = get_chunk(cx, cy - 1, cz); if (n) n->is_dirty = true; }
        if (by == 15) { Chunk* n = get_chunk(cx, cy + 1, cz); if (n) n->is_dirty = true; }
        if (bz == 0)  { Chunk* n = get_chunk(cx, cy, cz - 1); if (n) n->is_dirty = true; }
        if (bz == 15) { Chunk* n = get_chunk(cx, cy, cz + 1); if (n) n->is_dirty = true; }
    }
}

// تعديل أو بناء مكعب جديد
void set_block_at(int x, int y, int z, BlockType type) {
    // 1. تطبيق التعديل في نظام المقاطع والـ VBO فوراً
    add_modification_to_chunk(x, y, z, type);

    // 2. تحديث المصفوفة الأصلية لكي يعمل نظام الحفظ والتخزين دون كسر اللعبة[cite: 3]
    bool found = false;
    for (int i = 0; i < num_modified_blocks; i++) { //[cite: 3]
        if (modified_blocks[i].x == x && modified_blocks[i].y == y && modified_blocks[i].z == z) { //[cite: 3]
            modified_blocks[i].type = type; //[cite: 3]
            found = true;
            break;
        }
    }

    if (!found) {
        if (num_modified_blocks < MAX_MODIFIED_BLOCKS) { //[cite: 3]
            modified_blocks[num_modified_blocks].x = x; //[cite: 3]
            modified_blocks[num_modified_blocks].y = y; //[cite: 3]
            modified_blocks[num_modified_blocks].z = z; //[cite: 3]
            modified_blocks[num_modified_blocks].type = type; //[cite: 3]
            num_modified_blocks++; //[cite: 3]
        } else {
            printf("Warning: Modified blocks limit reached!\n"); //[cite: 3]
        }
    }
}

// دالة إعادة بناء المش وتطبيق الـ Hidden Face Culling ورفع البيانات للـ VBO
static void rebuild_chunk_mesh(Chunk* c) {
    init_vbo_functions();

    for (int t = 0; t < MAX_TEXTURES; t++) temp_counts[t] = 0;

    for (int bx = 0; bx < CHUNK_SIZE; bx++) {
        for (int by = 0; by < CHUNK_SIZE; by++) {
            for (int bz = 0; bz < CHUNK_SIZE; bz++) {
                BlockType type = c->blocks[bx][by][bz];
                if (type == BLOCK_AIR) continue;

                int tex_idx = (int)type; 
                if (tex_idx < 0 || tex_idx >= MAX_TEXTURES) continue;

                int wx = c->cx * CHUNK_SIZE + bx;
                int wy = c->cy * CHUNK_SIZE + by;
                int wz = c->cz * CHUNK_SIZE + bz;

                float fx = (float)wx;
                float fy = (float)wy;
                float fz = (float)wz;

                // --- تطبيق الـ Hidden Face Culling وفحص الجيران الستة المحيطين بالبلكة[cite: 3] ---
                
                // 1. الوجه الأمامي (Front Face)
                if (get_block_at(wx, wy, wz + 1) == BLOCK_AIR) {
                    int tex_idx = get_face_texture(type, 0); // جلب الخامة المخصصة للوجه الأمامي
                    int idx = temp_counts[tex_idx];
                    if (idx + 4 <= 24576) {
                        temp_vertices[tex_idx][idx+0] = (ChunkVertex){fx,        fy,        fz + 1.0f, 0.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+1] = (ChunkVertex){fx + 1.0f, fy,        fz + 1.0f, 1.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+2] = (ChunkVertex){fx + 1.0f, fy + 1.0f, fz + 1.0f, 1.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+3] = (ChunkVertex){fx,        fy + 1.0f, fz + 1.0f, 0.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_counts[tex_idx] += 4;
                    }
                }

                // 2. الوجه الخلفي (Back Face)
                if (get_block_at(wx, wy, wz - 1) == BLOCK_AIR) {
                    int tex_idx = get_face_texture(type, 1); // جلب الخامة المخصصة للوجه الخلفي
                    int idx = temp_counts[tex_idx];
                    if (idx + 4 <= 24576) {
                        temp_vertices[tex_idx][idx+0] = (ChunkVertex){fx,        fy,        fz,        1.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+1] = (ChunkVertex){fx,        fy + 1.0f, fz,        1.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+2] = (ChunkVertex){fx + 1.0f, fy + 1.0f, fz,        0.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+3] = (ChunkVertex){fx + 1.0f, fy,        fz,        0.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_counts[tex_idx] += 4;
                    }
                }

                // 3. الوجه العلوي (Top Face)
                if (get_block_at(wx, wy + 1, wz) == BLOCK_AIR) {
                    int tex_idx = get_face_texture(type, 2); // جلب الخامة المخصصة للوجه العلوي
                    int idx = temp_counts[tex_idx];
                    if (idx + 4 <= 24576) {
                        temp_vertices[tex_idx][idx+0] = (ChunkVertex){fx,        fy + 1.0f, fz,        0.0f, 1.0f, 1.0f, 1.0f, 1.0f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+1] = (ChunkVertex){fx,        fy + 1.0f, fz + 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+2] = (ChunkVertex){fx + 1.0f, fy + 1.0f, fz + 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+3] = (ChunkVertex){fx + 1.0f, fy + 1.0f, fz,        1.0f, 1.0f, 1.0f, 1.0f, 1.0f}; //[cite: 3]
                        temp_counts[tex_idx] += 4;
                    }
                }

                // 4. الوجه السفلي (Bottom Face)
                if (get_block_at(wx, wy - 1, wz) == BLOCK_AIR) {
                    int tex_idx = get_face_texture(type, 3); // جلب الخامة المخصصة للوجه السفلي
                    int idx = temp_counts[tex_idx];
                    if (idx + 4 <= 24576) {
                        temp_vertices[tex_idx][idx+0] = (ChunkVertex){fx,        fy,        fz,        1.0f, 1.0f, 0.5f, 0.5f, 0.5f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+1] = (ChunkVertex){fx + 1.0f, fy,        fz,        0.0f, 1.0f, 0.5f, 0.5f, 0.5f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+2] = (ChunkVertex){fx + 1.0f, fy,        fz + 1.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+3] = (ChunkVertex){fx,        fy,        fz + 1.0f, 1.0f, 0.0f, 0.5f, 0.5f, 0.5f}; //[cite: 3]
                        temp_counts[tex_idx] += 4;
                    }
                }

                // 5. الوجه الأيمن (Right Face)
                if (get_block_at(wx + 1, wy, wz) == BLOCK_AIR) {
                    int tex_idx = get_face_texture(type, 4); // جلب الخامة المخصصة للوجه الأيمن
                    int idx = temp_counts[tex_idx];
                    if (idx + 4 <= 24576) {
                        temp_vertices[tex_idx][idx+0] = (ChunkVertex){fx + 1.0f, fy,        fz,        1.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+1] = (ChunkVertex){fx + 1.0f, fy + 1.0f, fz,        1.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+2] = (ChunkVertex){fx + 1.0f, fy + 1.0f, fz + 1.0f, 0.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+3] = (ChunkVertex){fx + 1.0f, fy,        fz + 1.0f, 0.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_counts[tex_idx] += 4;
                    }
                }

                // 6. الوجه الأيسر (Left Face)
                if (get_block_at(wx - 1, wy, wz) == BLOCK_AIR) {
                    int tex_idx = get_face_texture(type, 5); // جلب الخامة المخصصة للوجه الأيسر
                    int idx = temp_counts[tex_idx];
                    if (idx + 4 <= 24576) {
                        temp_vertices[tex_idx][idx+0] = (ChunkVertex){fx,        fy,        fz,        0.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+1] = (ChunkVertex){fx,        fy,        fz + 1.0f, 1.0f, 0.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+2] = (ChunkVertex){fx,        fy + 1.0f, fz + 1.0f, 1.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_vertices[tex_idx][idx+3] = (ChunkVertex){fx,        fy + 1.0f, fz,        0.0f, 1.0f, 0.8f, 0.8f, 0.8f}; //[cite: 3]
                        temp_counts[tex_idx] += 4;
                    }
                }
            }
        }
    }

    // رفع البيانات المفلترة والمجمعة مباشرة إلى بافر كرت الشاشة (VBO)
    for (int t = 0; t < MAX_TEXTURES; t++) {
        c->vertex_counts[t] = temp_counts[t];
        if (c->vertex_counts[t] > 0) {
            if (c->vbo_ids[t] == 0) {
                glGenBuffers_ptr(1, &c->vbo_ids[t]);
            }
            glBindBuffer_ptr(GL_ARRAY_BUFFER, c->vbo_ids[t]);
            glBufferData_ptr(GL_ARRAY_BUFFER, c->vertex_counts[t] * sizeof(ChunkVertex), temp_vertices[t], GL_STATIC_DRAW);
        } else {
            if (c->vbo_ids[t] != 0) {
                glDeleteBuffers_ptr(1, &c->vbo_ids[t]);
                c->vbo_ids[t] = 0;
            }
        }
    }
    c->is_dirty = false;
}

// رسم العالم بالكامل بنظام الـ VBO فائق السرعة
void draw_map(float player_x, float player_y, float player_z) {
    init_vbo_functions();

    // 💡 الآن يمكنك رفع مدى رؤية اللاعب لـ 4 مقاطع دائرية (أي 64 بلكة كاملة) بسلاسة فائقة![cite: 3]
    int radius_xz = render_distance_chunks; // يقرأ القيمة ديناميكياً الآن
    int radius_y = 4;

    int p_cx = (int)floor(player_x) >> 4;
    int p_cy = (int)floor(player_y) >> 4;
    int p_cz = (int)floor(player_z) >> 4;

    // تفعيل خاصية مصفوفات النقاط الفائقة في OpenGL (Vertex Arrays via VBO)
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glEnable(GL_TEXTURE_2D); //[cite: 3]

    for (int cx = p_cx - radius_xz; cx <= p_cx + radius_xz; cx++) {
        for (int cy = p_cy - radius_y; cy <= p_cy + radius_y; cy++) {
            for (int cz = p_cz - radius_xz; cz <= p_cz + radius_xz; cz++) {
                
                // التأكد من أن المشهد يقع ضمن حدود ارتفاع اللعبة[cite: 3]
                if (cy < (MAP_DEPTH_LIMIT >> 4) || cy > ((GROUND_LEVEL + 250) >> 4)) continue; //[cite: 3]

                Chunk* c = get_chunk(cx, cy, cz);
                if (!c) c = create_chunk(cx, cy, cz);
                if (!c) continue;

                if (c->is_dirty) {
                    rebuild_chunk_mesh(c);
                }

                // رسم طبقات الـ VBO لكل خامة بضربة واحدة لكرت الشاشة
                for (int t = 0; t < MAX_TEXTURES; t++) {
                    if (c->vertex_counts[t] > 0 && c->vbo_ids[t] != 0) {
                        glBindTexture(GL_TEXTURE_2D, block_textures[t]); //[cite: 3]
                        glBindBuffer_ptr(GL_ARRAY_BUFFER, c->vbo_ids[t]);

                        // تحديد الهيكل الداخلي للبيانات المرفوعة (تحديد قفزات الذاكرة Stride بدقة)
                        glVertexPointer(3, GL_FLOAT, sizeof(ChunkVertex), (void*)0);
                        glTexCoordPointer(2, GL_FLOAT, sizeof(ChunkVertex), (void*)(3 * sizeof(float)));
                        glColorPointer(3, GL_FLOAT, sizeof(ChunkVertex), (void*)(5 * sizeof(float)));

                        // رسم كل الأوجه في المقطع بطلقة برمجية واحدة!
                        glDrawArrays(GL_QUADS, 0, c->vertex_counts[t]);
                    }
                }
            }
        }
    }

    // تنظيف الحالات وإلغاء تفعيل البافر لكي لا تؤثر على واجهة الـ HUD
    glBindBuffer_ptr(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D); //[cite: 3]
}

// إيجاد أعلى نقطة صلبة في العمود[cite: 3]
int get_highest_solid_block_y(int x, int z) {
    for (int y = GROUND_LEVEL; y >= MAP_DEPTH_LIMIT; y--) { //[cite: 3]
        if (get_block_at(x, y, z) != BLOCK_AIR) { //[cite: 3]
            return y; //[cite: 3]
        }
    }
    return GROUND_LEVEL; //[cite: 3]
}

// حفظ تقدم العالم والإحداثيات (متوافق 100% مع الإصدار القديم)[cite: 3]
bool save_world(const char* filepath, float player_x, float player_y, float player_z) {
    FILE* file = fopen(filepath, "wb"); //[cite: 3]
    if (file == NULL) { //[cite: 3]
        printf("Failed to open file for saving: %s\n", filepath); //[cite: 3]
        return false; //[cite: 3]
    }

    fwrite(&player_x, sizeof(float), 1, file); //[cite: 3]
    fwrite(&player_y, sizeof(float), 1, file); //[cite: 3]
    fwrite(&player_z, sizeof(float), 1, file); //[cite: 3]
    fwrite(&num_modified_blocks, sizeof(int), 1, file); //[cite: 3]

    if (num_modified_blocks > 0) { //[cite: 3]
        fwrite(modified_blocks, sizeof(Block), num_modified_blocks, file); //[cite: 3]
    }

    fclose(file); //[cite: 3]
    printf("3D World saved successfully to %s!\n", filepath); //[cite: 3]
    return true; //[cite: 3]
}

// تحميل العالم ثلاثي الأبعاد وإعادة تعبئة نظام الـ Chunks ديناميكياً
bool load_world(const char* filepath, float* player_x, float* player_y, float* player_z) {
    FILE* file = fopen(filepath, "rb"); //[cite: 3]
    if (file == NULL) { //[cite: 3]
        return false; //[cite: 3]
    }

    fread(player_x, sizeof(float), 1, file); //[cite: 3]
    fread(player_y, sizeof(float), 1, file); //[cite: 3]
    fread(player_z, sizeof(float), 1, file); //[cite: 3]
    fread(&num_modified_blocks, sizeof(int), 1, file); //[cite: 3]

    if (num_modified_blocks > 0) { //[cite: 3]
        fread(modified_blocks, sizeof(Block), num_modified_blocks, file); //[cite: 3]
    }
    fclose(file); //[cite: 3]

    // 🔥 إعادة بناء وحقن كافة الكتل المعدلة داخل نظام الـ Chunks بعد تحميل الملف مباشرة
    num_chunks = 0;
    for (int i = 0; i < HASH_SIZE; i++) chunk_hash_table[i] = -1;
    
    for (int i = 0; i < num_modified_blocks; i++) {
        add_modification_to_chunk(modified_blocks[i].x, modified_blocks[i].y, modified_blocks[i].z, modified_blocks[i].type);
    }

    printf("3D World loaded and Chunks built successfully from %s!\n", filepath); //[cite: 3]
    return true; //[cite: 3]
}