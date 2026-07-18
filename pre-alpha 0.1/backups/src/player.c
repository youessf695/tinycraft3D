#include "player.h"
#include "map.h"
#include <math.h>
#include <stdio.h>

// 💡 تم ضبط الثوابت الفيزيائية لتوفير قفزة رشيقة وسلسة بارتفاع 1.4 بلكة في الـ FPS العالي
#define GRAVITY 20.0f           // قوة الجاذبية لأسفل المحور Y
#define JUMP_FORCE 7.5f         // قوة القفز لأعلى المحور Y
#define MOVE_SPEED 5.0f         // سرعة المشي الطبيعية (وحدة في الثانية)
#define MOUSE_SENSITIVITY 0.15f // حساسية دوران الماوس
#define PI 3.14159265f

void init_player(Player *player)
{
    player->x = 0.5f;                
    player->y = GROUND_LEVEL + 2.0f; 
    player->z = 0.5f;
    player->vx = 0.0f;
    player->vy = 0.0f;
    player->vz = 0.0f;
    player->yaw = 0.0f;    
    player->pitch = 0.0f;  
    player->width = 0.6f;  
    player->height = 1.8f; 
    player->is_grounded = false;
    player->selected = BLOCK_GRASS;
    
    // تهيئة متغيرات الطيران الجديدة
    player->is_flying = false;
    player->last_space_time = 0;
}

// دالة التحقق من تصادم مجسم اللاعب مع أي مكعب صلب في العالم ثلاثي الأبعاد
static bool is_colliding_at(float px, float py, float pz, float pw, float ph)
{
    float half_w = pw / 2.0f;

    float min_x = px - half_w;
    float max_x = px + half_w;
    float min_y = py;
    float max_y = py + ph;
    float min_z = pz - half_w;
    float max_z = pz + half_w;

    int start_x = (int)floorf(min_x);
    int end_x = (int)ceilf(max_x);
    int start_y = (int)floorf(min_y);
    int end_y = (int)ceilf(max_y);
    int start_z = (int)floorf(min_z);
    int end_z = (int)ceilf(max_z);

    for (int x = start_x; x < end_x; x++)
    {
        for (int y = start_y; y < end_y; y++)
        {
            for (int z = start_z; z < end_z; z++)
            {
                if (get_block_at(x, y, z) != BLOCK_AIR)
                {
                    if (min_x < x + 1.0f && max_x > x &&
                        min_y < y + 1.0f && max_y > y &&
                        min_z < z + 1.0f && max_z > z)
                    {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void handle_player_mouse(Player *player, float xrel, float yrel)
{
    player->yaw += xrel * MOUSE_SENSITIVITY;
    player->pitch -= yrel * MOUSE_SENSITIVITY; 

    if (player->pitch > 89.0f)
        player->pitch = 89.0f;
    if (player->pitch < -89.0f)
        player->pitch = -89.0f;
}

void handle_player_input(Player *player, float dt)
{
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    float yaw_rad = player->yaw * PI / 180.0f;

    float forward_x = sinf(yaw_rad);
    float forward_z = -cosf(yaw_rad);
    float right_x = cosf(yaw_rad);
    float right_z = sinf(yaw_rad);

    player->vx = 0.0f;
    player->vz = 0.0f;

    // 💡 [ميزة الرقض من زر Shift]: مضاعفة سرعة التحرك عند الضغط مطولاً على Shift
    float current_speed = MOVE_SPEED;
    if (state[SDL_SCANCODE_LSHIFT]) {
        current_speed = MOVE_SPEED * 1.8f; // زيادة السرعة بنسبة 80% للركض السريع
    }

    if (state[SDL_SCANCODE_W])
    {
        player->vx += forward_x * current_speed;
        player->vz += forward_z * current_speed;
    }
    if (state[SDL_SCANCODE_S])
    {
        player->vx -= forward_x * current_speed;
        player->vz -= forward_z * current_speed;
    }
    if (state[SDL_SCANCODE_A])
    {
        player->vx -= right_x * current_speed;
        player->vz -= right_z * current_speed;
    }
    if (state[SDL_SCANCODE_D])
    {
        player->vx += right_x * current_speed;
        player->vz += right_z * current_speed;
    }

    // 💡 التحكم في الارتفاع والانخفاض أثناء الطيران
    if (player->is_flying) {
        player->vy = 0.0f; // إلغاء أي حركة عمودية تلقائية
        if (state[SDL_SCANCODE_SPACE]) {
            player->vy = current_speed; // الارتفاع للأعلى عند تعليق زر المسافة
        }
        if (state[SDL_SCANCODE_LCTRL]) {
            player->vy = -current_speed; // الانخفاض للأسفل عند تعليق زر الكنترول اليسار
        }
    } else {
        // القفز الطبيعي فوق الأرض فقط
        if (state[SDL_SCANCODE_SPACE] && player->is_grounded)
        {
            player->vy = JUMP_FORCE;
            player->is_grounded = false;
        }
    }

    if (state[SDL_SCANCODE_1])
        player->selected = BLOCK_GRASS;
    if (state[SDL_SCANCODE_2])
        player->selected = BLOCK_STONE;
    if (state[SDL_SCANCODE_3])
        player->selected = BLOCK_WOOD;
}

void update_player(Player *player, float dt)
{
    // 💡 حل مشكلة الاختراق (Tunneling): تقسيم الوقت إلى خطوات صغيرة جداً عند السقوط السريع
    int substeps = 1;
    if (!player->is_flying && fabs(player->vy) > 10.0f) {
        substeps = 4; // زيادة الخطوات لـ 4 أجزاء لضمان دقة التصادم ومنع اختراق الأرض
    }
    float sub_dt = dt / substeps;

    // حلقة لتحديث حركة اللاعب خطوة بخطوة داخل نفس الفريم
    for (int step = 0; step < substeps; step++) {
        
        // 1. تطبيق الجاذبية فقط إذا لم يكن اللاعب في وضع الطيران
        if (!player->is_flying) {
            player->vy -= GRAVITY * sub_dt;
            if (player->vy < -25.0f) {
                player->vy = -25.0f;
            }
        }

        // 2. تحديث المحور X والتحقق من التصادم الأفقي X
        player->x += player->vx * sub_dt;
        if (is_colliding_at(player->x, player->y, player->z, player->width, player->height))
        {
            if (player->vx > 0) {
                player->x = floorf(player->x + player->width / 2.0f) - player->width / 2.0f - 0.001f;
            }
            else if (player->vx < 0) {
                player->x = ceilf(player->x - player->width / 2.0f) + player->width / 2.0f + 0.001f;
            }
            player->vx = 0.0f;
        }

        // 3. تحديث المحور Z والتحقق من التصادم الأفقي Z
        player->z += player->vz * sub_dt;
        if (is_colliding_at(player->x, player->y, player->z, player->width, player->height))
        {
            if (player->vz > 0) {
                player->z = floorf(player->z + player->width / 2.0f) - player->width / 2.0f - 0.001f;
            }
            else if (player->vz < 0) {
                player->z = ceilf(player->z - player->width / 2.0f) + player->width / 2.0f + 0.001f;
            }
            player->vz = 0.0f;
        }

        // 4. تحديث المحور Y والتحقق من التصادم العمودي
        if (!player->is_flying) {
            player->is_grounded = false;
        }
        player->y += player->vy * sub_dt;
        
        if (is_colliding_at(player->x, player->y, player->z, player->width - 0.02f, player->height))
        {
            if (player->is_flying) {
                if (player->vy < 0) {                                         
                    player->y = floorf(player->y) + 1.0f; 
                } else if (player->vy > 0) {
                    player->y = ceilf(player->y + player->height) - player->height - 1.0f - 0.001f;
                }
                player->vy = 0.0f;
            } else {
                if (player->vy < 0) {                                         
                    player->y = floorf(player->y) + 1.0f; 
                    player->is_grounded = true;
                    player->vy = 0.0f;
                }
                else if (player->vy > 0) { 
                    player->y = ceilf(player->y + player->height) - player->height - 1.0f - 0.001f;
                    player->vy = 0.0f;
                }
            }
        }
    }

    // 5. الحماية من السقوط الحر والهاوية
    if (player->y < MAP_DEPTH_LIMIT)
    {
        int px = (int)floorf(player->x);
        int pz = (int)floorf(player->z);
        int safe_y = get_highest_solid_block_y(px, pz);

        player->x = px + 0.5f;
        player->y = safe_y + 2.0f;
        player->z = pz + 0.5f;
        player->vx = 0.0f;
        player->vy = 0.0f;
        player->vz = 0.0f;
        player->is_grounded = true;
        player->is_flying = false; 
        printf("Player fell into the 3D void! Teleported safely.\n");
    }
}