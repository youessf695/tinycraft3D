#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h> // 💡 المكتبة القياسية للأنواع الرقمية ثنائية الحجم

// --- الثوابت العامة للعبة 3D ---
#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600

// في الـ 3D، يفضل أن يكون حجم المكعب هو 1.0 وحدة برمجية لتسهيل الحسابات الرياضية في OpenGL
#define BLOCK_SIZE 1.0f

// حدود العالم الافتراضي على المحور Y (الارتفاع)
#define GROUND_LEVEL 0      // مستوى سطح الأرض الافتراضي (العشب)
#define MAP_DEPTH_LIMIT -15 // الهاوية تبدأ تحت مستوى -15 مكعباً

extern int render_distance_chunks;

// --- أنواع المكعبات ---
typedef enum
{
    BLOCK_AIR = 0,
    BLOCK_GRASS = 1,   // القيمة القديمة الأصلية للعشب
    BLOCK_STONE = 2,   // القيمة القديمة الأصلية للحجر
    BLOCK_WOOD = 3,    // القيمة القديمة الأصلية للخشب
    BLOCK_DIRT = 4,    // المادة الجديدة 1
    BLOCK_BRICKS = 5   // المادة الجديدة 2
} BlockType;

// --- هيكل بيانات المكعب ثلاثي الأبعاد (حفظ التعديلات) ---
typedef struct
{
    int x;          // الإحداثي X في العالم ثلاثي الأبعاد
    int y;          // الإحداثي Y (الارتفاع)
    int z;          // الإحداثي Z (العمق)
    BlockType type; // نوع المكعب
} Block;

// --- هيكل بيانات اللاعب ثلاثي الأبعاد (FPS Player) ---
// تحديث هيكل اللاعب هنا وحذف أي نسخة قديمة له في هذا الملف

// هيكل اللاعب الموحد
typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float yaw, pitch;
    float width, height;
    bool is_grounded;
    int selected;
    
    // المتغيرات الجديدة
    bool is_flying;          
    uint32_t last_space_time;  // 💡 تم التغيير إلى uint32_t لتجنب مشاكل التضمين نهائياً
} Player;

#endif // COMMON_H