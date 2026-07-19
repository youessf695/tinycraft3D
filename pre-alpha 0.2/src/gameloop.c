#include "gameloop.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_opengl.h"
#include "SDL2/SDL_ttf.h"
#include "common.h"
#include "textures.h"
#include "map.h"
#include "player.h"
#include <stdio.h>
#include <math.h>

static SDL_Window *window = NULL;
static SDL_GLContext gl_context = NULL;
static Player player;
static bool is_running = true;
static bool mouse_grabbed = true;
static bool show_hud = false; // 💡 ميزة P-A 0.2: إخفاء واجهة النصوص عند بدء اللعبة افتراضياً

// متغيرات واجهة المستخدم والنصوص الآمنة
static TTF_Font *game_font = NULL;
static GLuint notification_tex = 0;
static int notification_w = 0;
static int notification_h = 0;
static int notification_pot_w = 0;
static int notification_pot_h = 0;
static Uint32 notification_timer = 0;

// --- [متغيرات الكاش الجديدة للنصوص الأخرى لضمان عدم حدوث لاق] ---
static GLuint block_tex = 0;
static int block_w = 0, block_h = 0, block_pot_w = 0, block_pot_h = 0;
static BlockType last_selected = BLOCK_AIR;
// --- متغيرات الكاش لعداد الفريمات FPS ---
static GLuint fps_tex = 0;
static int fps_w = 0, fps_h = 0, fps_pot_w = 0, fps_pot_h = 0;
static int last_fps = -1;

static GLuint coords_tex = 0;
static int coords_w = 0, coords_h = 0, coords_pot_w = 0, coords_pot_h = 0;
static int last_cx = -999, last_cy = -999, last_cz = -999;

static int next_power_of_two(int val)
{
    int p = 1;
    while (p < val)
        p *= 2;
    return p;
}

// دالة مساعدة لتوليد وتحديث أسطح النصوص وتحويلها إلى تكستشر لـ OpenGL
static void create_text_texture(GLuint *tex_id, const char *text, int *w, int *h, int *pot_w, int *pot_h)
{
    if (*tex_id != 0)
    {
        glDeleteTextures(1, tex_id);
        *tex_id = 0;
    }
    if (game_font == NULL || text == NULL || text[0] == '\0')
        return;

    SDL_Color text_col = {255, 255, 255, 255};
    SDL_Surface *surf = TTF_RenderUTF8_Blended(game_font, text, text_col);
    if (surf != NULL)
    {
        *w = surf->w;
        *h = surf->h;
        *pot_w = next_power_of_two(surf->w);
        *pot_h = next_power_of_two(surf->h);

        SDL_Surface *pot_surf = SDL_CreateRGBSurfaceWithFormat(0, *pot_w, *pot_h, 32, SDL_PIXELFORMAT_RGBA32);
        if (pot_surf != NULL)
        {
            SDL_FillRect(pot_surf, NULL, SDL_MapRGBA(pot_surf->format, 0, 0, 0, 0));
            SDL_BlitSurface(surf, NULL, pot_surf, NULL);

            glGenTextures(1, tex_id);
            glBindTexture(GL_TEXTURE_2D, *tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *pot_w, *pot_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pot_surf->pixels);

            SDL_FreeSurface(pot_surf);
        }
        SDL_FreeSurface(surf);
    }
}

// دالة مساعدة لرسم النص المخزن مسبقاً مع إضافة ظل أسود خلفي لجعل القراءة واضحة
static void draw_cached_text(GLuint tex_id, float x, float y, int w, int h, int pot_w, int pot_h, SDL_Color color)
{
    if (tex_id == 0)
        return;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex_id);

    float tex_u = (float)w / (float)pot_w;
    float tex_v = (float)h / (float)pot_h;

    // 5 تمريرات رسم: 4 للظل الأسود المحيط بالنص، وتمريرة أخيرة للنص الملون الأصلي
    struct
    {
        float dx, dy;
        float r, g, b;
    } passes[5] = {
        {-1.5f, 0.0f, 0.0f, 0.0f, 0.0f},
        {1.5f, 0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, -1.5f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.5f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, (float)color.r / 255.0f, (float)color.g / 255.0f, (float)color.b / 255.0f}};

    for (int i = 0; i < 5; i++)
    {
        glColor4f(passes[i].r, passes[i].g, passes[i].b, (float)color.a / 255.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(x + passes[i].dx, y + passes[i].dy);
        glTexCoord2f(tex_u, 0.0f);
        glVertex2f(x + passes[i].dx + w, y + passes[i].dy);
        glTexCoord2f(tex_u, tex_v);
        glVertex2f(x + passes[i].dx + w, y + passes[i].dy + h);
        glTexCoord2f(0.0f, tex_v);
        glVertex2f(x + passes[i].dx, y + passes[i].dy + h);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

typedef struct
{
    bool hit;
    int hit_x, hit_y, hit_z;
    int prev_x, prev_y, prev_z;
    bool has_prev;
} RayHit;

static RayHit perform_raycast(void)
{
    RayHit result = {false, -1, -1, -1, -1, -1, -1, false};
    float px = player.x;
    float py = player.y + 1.6f;
    float pz = player.z;

    float yaw_rad = player.yaw * 3.14159265f / 180.0f;
    float pitch_rad = player.pitch * 3.14159265f / 180.0f;

    float dx = sinf(yaw_rad) * cosf(pitch_rad);
    float dy = -sinf(pitch_rad);
    float dz = -cosf(yaw_rad) * cosf(pitch_rad);

    float cx = px;
    float cy = py;
    float cz = pz;
    int last_bx = -1, last_by = -1, last_bz = -1;
    bool has_last = false;

    for (float dist = 0.0f; dist < 5.0f; dist += 0.05f)
    {
        cx = px + dx * dist;
        cy = py + dy * dist;
        cz = pz + dz * dist;

        int bx = (int)floorf(cx);
        int by = (int)floorf(cy);
        int bz = (int)floorf(cz);

        if (get_block_at(bx, by, bz) != BLOCK_AIR)
        {
            result.hit = true;
            result.hit_x = bx;
            result.hit_y = by;
            result.hit_z = bz;
            result.prev_x = last_bx;
            result.prev_y = last_by;
            result.prev_z = last_bz;
            result.has_prev = has_last;
            break;
        }

        last_bx = bx;
        last_by = by;
        last_bz = bz;
        has_last = true;
    }
    return result;
}

static void draw_highlight_cube(int x, int y, int z)
{
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // 💡 الحل السحري: تفعيل خاصية الإزاحة لمنع الـ Z-Fighting نهائياً عن الأسطح الشفافة
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f); // دفع الأسطح خطوة ميكروسكوبية نحو الكاميرا لضمان الأولوية

    glColor4f(1.0f, 1.0f, 1.0f, 0.25f);

    // زيادة مسافة الأمان الهندسية قليلاً من 0.002 إلى 0.004 لراحة كرت الشاشة
    float offset = 0.004f;
    float fx = (float)x - offset;
    float fy = (float)y - offset;
    float fz = (float)z - offset;
    float s = 1.0f + (offset * 2.0f); // ستصبح 1.008f بدلاً من 1.004f

    glBegin(GL_QUADS);
    glVertex3f(fx, fy, fz + s);
    glVertex3f(fx + s, fy, fz + s);
    glVertex3f(fx + s, fy + s, fz + s);
    glVertex3f(fx, fy + s, fz + s);
    glVertex3f(fx, fy, fz);
    glVertex3f(fx, fy + s, fz);
    glVertex3f(fx + s, fy + s, fz);
    glVertex3f(fx + s, fy, fz);
    glVertex3f(fx, fy + s, fz);
    glVertex3f(fx, fy + s, fz + s);
    glVertex3f(fx + s, fy + s, fz + s);
    glVertex3f(fx + s, fy + s, fz);
    glVertex3f(fx, fy, fz);
    glVertex3f(fx + s, fy, fz);
    glVertex3f(fx + s, fy, fz + s);
    glVertex3f(fx, fy, fz + s);
    glVertex3f(fx + s, fy, fz);
    glVertex3f(fx + s, fy + s, fz);
    glVertex3f(fx + s, fy + s, fz + s);
    glVertex3f(fx + s, fy, fz + s);
    glVertex3f(fx, fy, fz);
    glVertex3f(fx, fy, fz + s);
    glVertex3f(fx, fy + s, fz + s);
    glVertex3f(fx, fy + s, fz);
    glEnd();

    // إيقاف ميزة الإزاحة فوراً بعد الانتهاء من رسم الأسطح الممتلئة لكي لا تؤثر على بقية اللعبة
    glDisable(GL_POLYGON_OFFSET_FILL);

    // رسم الخطوط الخارجية البيضاء (محيط المكعب)
    glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    glVertex3f(fx, fy, fz);
    glVertex3f(fx + s, fy, fz);
    glVertex3f(fx + s, fy, fz);
    glVertex3f(fx + s, fy, fz + s);
    glVertex3f(fx + s, fy, fz + s);
    glVertex3f(fx, fy, fz + s);
    glVertex3f(fx, fy, fz + s);
    glVertex3f(fx, fy, fz);
    glVertex3f(fx, fy + s, fz);
    glVertex3f(fx + s, fy + s, fz);
    glVertex3f(fx + s, fy + s, fz);
    glVertex3f(fx + s, fy + s, fz + s);
    glVertex3f(fx + s, fy + s, fz + s);
    glVertex3f(fx, fy + s, fz + s);
    glVertex3f(fx, fy + s, fz + s);
    glVertex3f(fx, fy + s, fz);
    glVertex3f(fx, fy, fz);
    glVertex3f(fx, fy + s, fz);
    glVertex3f(fx + s, fy, fz);
    glVertex3f(fx + s, fy + s, fz);
    glVertex3f(fx + s, fy, fz + s);
    glVertex3f(fx + s, fy + s, fz + s);
    glVertex3f(fx, fy, fz + s);
    glVertex3f(fx, fy + s, fz + s);
    glEnd();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_FOG);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

bool init_game(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    window = SDL_CreateWindow("TinyCraft 3D FPS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (window == NULL)
        return false;

    gl_context = SDL_GL_CreateContext(window);
    if (gl_context == NULL)
        return false;

    SDL_GL_SetSwapInterval(1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glClearColor(0.5f, 0.7f, 1.0f, 1.0f);

    void update_game_fog(void) {
        glEnable(GL_FOG);
        GLfloat fogColor[4] = {0.5f, 0.7f, 1.0f, 1.0f};
        glFogfv(GL_FOG_COLOR, fogColor);
        glFogi(GL_FOG_MODE, GL_LINEAR);

        // الحساب الديناميكي بناءً على عدد الـ Chunks
        float max_block_distance = render_distance_chunks * 16.0f;
        float fog_start = max_block_distance * 0.60f; // يبدأ عند 60% من الرؤية
        float fog_end = max_block_distance * 0.90f;   // ينتهي ويصبح كثيفاً عند 90% لحجب الحواف

        glFogf(GL_FOG_START, fog_start);
        glFogf(GL_FOG_END, fog_end);
        glHint(GL_FOG_HINT, GL_NICEST);
    }

    if (!load_all_textures())
        return false;

    if (TTF_Init() >= 0)
    {
        game_font = TTF_OpenFont("C:\\Windows\\Fonts\\Minecraft.ttf", 20);
        if (game_font == NULL)
        {
            game_font = TTF_OpenFont("font.ttf", 20);
        }
    }

    init_map();
    init_player(&player);
    load_world("world3d.dat", &player.x, &player.y, &player.z);

    SDL_SetRelativeMouseMode(SDL_TRUE);
    mouse_grabbed = true;
    return true;
}

void run_game_loop(void)
{
    Uint32 last_time = SDL_GetTicks(); //[cite: 1]
    SDL_Event event;                   //[cite: 1]

    // متغيرات حساب الـ FPS في الخلفية
    Uint32 fps_last_time = SDL_GetTicks();
    int frame_count = 0;
    int current_fps_val = 0;

    while (is_running)
    {
        // زيادة عداد الفريمات مع كل دورة
        frame_count++;

        Uint32 current_time = SDL_GetTicks();            //[cite: 1]
        float dt = (current_time - last_time) / 1000.0f; //[cite: 1]
        last_time = current_time;                        //[cite: 1]
        if (dt > 0.1f)                                   //[cite: 1]
            dt = 0.1f;                                   //[cite: 1]

        // --- حساب معدل الفريمات الفعلي كل 500 ميلي ثانية (نصف ثانية) ---
        Uint32 now = SDL_GetTicks();
        if (now - fps_last_time >= 500)
        {
            float elapsed = (now - fps_last_time) / 1000.0f;
            current_fps_val = (int)(frame_count / elapsed);
            frame_count = 0;
            fps_last_time = now;
        }

        while (SDL_PollEvent(&event)) //[cite: 1]
        {
            if (event.type == SDL_QUIT)
            {
                is_running = false;
            }
            else if (event.type == SDL_KEYDOWN)
            {
                if (event.key.keysym.sym == SDLK_ESCAPE)
                {
                    mouse_grabbed = !mouse_grabbed;
                    SDL_SetRelativeMouseMode(mouse_grabbed ? SDL_TRUE : SDL_FALSE);
                }

                // 🔥 إضافة التقاط زر F12 لعكس حالة إظهار وإخفاء النصوص
                else if (event.key.keysym.sym == SDLK_F12)
                {
                    show_hud = !show_hud;
                }

                // 💡 [ميزة كشف الضغط المزدوج على Space لتفعيل الطيران]
                else if (event.key.keysym.sym == SDLK_SPACE)
                {
                    if (event.key.repeat == 0)
                    { // 🔥 السطر السحري: تخطي التكرار التلقائي تماماً عند تعليق الزر
                        Uint32 now = SDL_GetTicks();
                        if (now - player.last_space_time < 250)
                        { // تم الضغط مرتين متتاليتين في أقل من ربع ثانية
                            player.is_flying = !player.is_flying;
                            player.vy = 0.0f; // تصفير السرعة العمودية لمنع الاندفاع المفاجئ
                        }
                        player.last_space_time = now;
                    }
                }

                else if (event.key.keysym.sym == SDLK_RETURN)
                {
                    save_world("world3d.dat", player.x, player.y, player.z);

                    notification_timer = SDL_GetTicks();

                    if (notification_tex != 0)
                    {
                        glDeleteTextures(1, &notification_tex);
                        notification_tex = 0;
                    }

                    if (game_font != NULL)
                    {
                        SDL_Color text_col = {255, 255, 255, 255};
                        SDL_Surface *surf = TTF_RenderUTF8_Blended(game_font, "Game Saved Successfully!", text_col);

                        if (surf != NULL)
                        {
                            notification_w = surf->w;
                            notification_h = surf->h;
                            notification_pot_w = next_power_of_two(surf->w);
                            notification_pot_h = next_power_of_two(surf->h);

                            SDL_Surface *pot_surf = SDL_CreateRGBSurfaceWithFormat(0, notification_pot_w, notification_pot_h, 32, SDL_PIXELFORMAT_RGBA32);
                            if (pot_surf != NULL)
                            {
                                SDL_FillRect(pot_surf, NULL, SDL_MapRGBA(pot_surf->format, 0, 0, 0, 0));
                                SDL_BlitSurface(surf, NULL, pot_surf, NULL);

                                glGenTextures(1, &notification_tex);
                                glBindTexture(GL_TEXTURE_2D, notification_tex);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, notification_pot_w, notification_pot_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pot_surf->pixels);

                                SDL_FreeSurface(pot_surf);
                            }
                            SDL_FreeSurface(surf);
                        }
                    }
                }
            }
            else if (event.type == SDL_MOUSEMOTION)
            {
                if (mouse_grabbed)
                {
                    handle_player_mouse(&player, (float)event.motion.xrel, (float)event.motion.yrel);
                }
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN)
            {
                if (mouse_grabbed)
                {
                    RayHit ray = perform_raycast();
                    if (ray.hit)
                    {
                        if (event.button.button == SDL_BUTTON_LEFT)
                        {
                            set_block_at(ray.hit_x, ray.hit_y, ray.hit_z, BLOCK_AIR);
                        }
                        else if (event.button.button == SDL_BUTTON_RIGHT && ray.has_prev)
                        {
                            float half_w = player.width / 2.0f;
                            bool overlap = (ray.prev_x + 1.0f > player.x - half_w && ray.prev_x < player.x + half_w &&
                                            ray.prev_y + 1.0f > player.y && ray.prev_y < player.y + player.height &&
                                            ray.prev_z + 1.0f > player.z - half_w && ray.prev_z < player.z + half_w);

                            if (!overlap && ray.prev_y <= GROUND_LEVEL + 250)
                            {
                                set_block_at(ray.prev_x, ray.prev_y, ray.prev_z, player.selected);
                            }
                        }
                    }
                }
            }
        }

        if (mouse_grabbed)                    //[cite: 1]
            handle_player_input(&player, dt); //[cite: 1]
        update_player(&player, dt);           //[cite: 1]

        if (player.y > GROUND_LEVEL + 250.0f) //[cite: 1]
        {
            player.y = GROUND_LEVEL + 250.0f; //[cite: 1]
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); //[cite: 1]

        // إعداد المنظور 3D
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        float fov = 60.0f;
        float aspect = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
        float near_plane = 0.1f, far_plane = 100.0f;
        float f = 1.0f / tanf(fov * (3.14159265f / 360.0f));
        float proj_matrix[16] = {
            f / aspect, 0, 0, 0,
            0, f, 0, 0,
            0, 0, (far_plane + near_plane) / (near_plane - far_plane), -1,
            0, 0, (2.0f * far_plane * near_plane) / (near_plane - far_plane), 0};
        glMultMatrixf(proj_matrix);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotatef(player.pitch, 1.0f, 0.0f, 0.0f);
        glRotatef(player.yaw, 0.0f, 1.0f, 0.0f);
        glTranslatef(-player.x, -(player.y + 1.6f), -player.z);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); //[cite: 1]

        // =============================================================
        // 🌌 [الضباب الديناميكي الذكي المربوط بالـ Chunks بنسبة 100%]
        // =============================================================
        glEnable(GL_FOG);
        
        // لون السماء الأزرق ليتلاشى الأفق فيه بنعومة[cite: 1]
        GLfloat fogColor[4] = {0.5f, 0.7f, 1.0f, 1.0f}; //[cite: 1]
        glFogfv(GL_FOG_COLOR, fogColor); //[cite: 1]
        glFogi(GL_FOG_MODE, GL_LINEAR); //[cite: 1]

        // الحساب الفعلي بناءً على اختيار اللاعب للمقاطع
        float max_block_distance = render_distance_chunks * 16.0f; 
        
        // الضباب يبدأ مبكراً ليصنع تدرجاً ناعماً، وينتهي عند 90% ليخفي لحظة تحميل الـ Chunks تماماً
        float fog_start = max_block_distance * 0.60f; 
        float fog_end = max_block_distance * 0.90f;   

        glFogf(GL_FOG_START, fog_start);
        glFogf(GL_FOG_END, fog_end);
        glHint(GL_FOG_HINT, GL_NICEST); //[cite: 1]
        // =============================================================

        // رسم العالم ثلاثي الأبعاد[cite: 1]
        draw_map(player.x, player.y, player.z); //[cite: 1]

        RayHit sight = perform_raycast();
        if (sight.hit)
        {
            draw_highlight_cube(sight.hit_x, sight.hit_y, sight.hit_z);
        }

        // =============================================================
        // [قسم واجهة المستخدم ثنائي الأبعاد الموحد والآمن HUD 2D]
        // =============================================================
        glMatrixMode(GL_PROJECTION);                       //[cite: 1]
        glPushMatrix();                                    //[cite: 1]
        glLoadIdentity();                                  //[cite: 1]
        glOrtho(0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, -1, 1); //[cite: 1]
        glMatrixMode(GL_MODELVIEW);                        //[cite: 1]
        glPushMatrix();                                    //[cite: 1]
        glLoadIdentity();                                  //[cite: 1]

        glDisable(GL_DEPTH_TEST);                          //[cite: 1]
        glDisable(GL_FOG);                                 //[cite: 1]
        glDisable(GL_CULL_FACE);                           //[cite: 1]
        glEnable(GL_BLEND);                                //[cite: 1]
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); //[cite: 1]

        if (show_hud) //[cite: 1]
        {
            // --- 1. نص المكعب المحدد حالياً (عند Y = 20) ---
            // --- تحديث ورسم نص المكعب المحدد حالياً في اليد ---
            if (player.selected != last_selected)
            {
                last_selected = player.selected; //[cite: 1]
                const char *block_name = "Selected Block: Dirt (Press 1)";

                if (player.selected == BLOCK_GRASS)
                    block_name = "Selected Block: Grass (Press 2)";
                if (player.selected == BLOCK_STONE)
                    block_name = "Selected Block: Stone (Press 3)";
                if (player.selected == BLOCK_WOOD)
                    block_name = "Selected Block: Wood (Press 4)";
                if (player.selected == BLOCK_BRICKS)
                    block_name = "Selected Block: Bricks (Press 5)";

                create_text_texture(&block_tex, block_name, &block_w, &block_h, &block_pot_w, &block_pot_h); //[cite: 1]
            }
            SDL_Color white_color = {255, 255, 255, 255};                                                       //[cite: 1]
            draw_cached_text(block_tex, 20.0f, 20.0f, block_w, block_h, block_pot_w, block_pot_h, white_color); //[cite: 1]

            // --- 2. إحداثيات اللاعب الحالية (عند Y = 50) ---
            int cx = (int)floorf(player.x);                      //[cite: 1]
            int cy = (int)floorf(player.y);                      //[cite: 1]
            int cz = (int)floorf(player.z);                      //[cite: 1]
            if (cx != last_cx || cy != last_cy || cz != last_cz) //[cite: 1]
            {
                last_cx = cx;
                last_cy = cy;
                last_cz = cz;         //[cite: 1]
                char coords_str[128]; //[cite: 1]
                if (player.is_flying)
                {                                                                       //[cite: 1]
                    sprintf(coords_str, "XYZ: %d / %d / %d [FLYING MODE]", cx, cy, cz); //[cite: 1]
                }
                else
                {                                                         //[cite: 1]
                    sprintf(coords_str, "XYZ: %d / %d / %d", cx, cy, cz); //[cite: 1]
                }
                create_text_texture(&coords_tex, coords_str, &coords_w, &coords_h, &coords_pot_w, &coords_pot_h); //[cite: 1]
            }
            SDL_Color gold_color = {240, 220, 130, 255};                                                            //[cite: 1]
            draw_cached_text(coords_tex, 20.0f, 50.0f, coords_w, coords_h, coords_pot_w, coords_pot_h, gold_color); //[cite: 1]

            // 🔥 --- 3. جديد: تحديث ورسم عداد الفريمات FPS (عند Y = 80) ---
            if (current_fps_val != last_fps)
            {
                last_fps = current_fps_val;
                char fps_str[32];
                sprintf(fps_str, "FPS: %d", current_fps_val);
                create_text_texture(&fps_tex, fps_str, &fps_w, &fps_h, &fps_pot_w, &fps_pot_h);
            }
            // سنعطيه لوناً أخضراً فسفورياً جميلاً ومريحاً للعين
            SDL_Color lime_green = {50, 255, 50, 255};
            draw_cached_text(fps_tex, 20.0f, 80.0f, fps_w, fps_h, fps_pot_w, fps_pot_h, lime_green);
        }

        // --- 3. رسم إشعار حفظ اللعبة التلقائي --- (تركناه خارج الشرط لكي يرى اللاعب تأكيد الحفظ دائماً)
        if (notification_timer > 0 && SDL_GetTicks() - notification_timer < 3000)
        {
            SDL_Color green_color = {120, 255, 120, 255};
            draw_cached_text(notification_tex, 20.0f, (float)SCREEN_HEIGHT - 40.0f, notification_w, notification_h, notification_pot_w, notification_pot_h, green_color);
        }
        else
        {
            if (notification_timer > 0)
            {
                if (notification_tex != 0)
                {
                    glDeleteTextures(1, &notification_tex);
                    notification_tex = 0;
                }
                notification_timer = 0;
            }
        }

        // --- 4. رسم علامة التصويب البيضاء (Crosshair) ---
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_LINES);
        glVertex2f((SCREEN_WIDTH / 2.0f) - 8.0f, SCREEN_HEIGHT / 2.0f);
        glVertex2f((SCREEN_WIDTH / 2.0f) + 8.0f, SCREEN_HEIGHT / 2.0f);
        glVertex2f(SCREEN_WIDTH / 2.0f, (SCREEN_HEIGHT / 2.0f) - 8.0f);
        glVertex2f(SCREEN_WIDTH / 2.0f, (SCREEN_HEIGHT / 2.0f) + 8.0f);
        glEnd();

        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glEnable(GL_FOG);
        glEnable(GL_DEPTH_TEST);

        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();

        SDL_GL_SwapWindow(window);
    }
}

void clean_game(void)
{
    if (notification_tex != 0)                  //[cite: 1]
        glDeleteTextures(1, &notification_tex); //[cite: 1]
    if (block_tex != 0)                         //[cite: 1]
        glDeleteTextures(1, &block_tex);        //[cite: 1]
    if (coords_tex != 0)                        //[cite: 1]
        glDeleteTextures(1, &coords_tex);       //[cite: 1]

    // 🔥 تدمير تكستشر عداد الفريمات الآمن
    if (fps_tex != 0)
        glDeleteTextures(1, &fps_tex);

    if (game_font) //[cite: 1]
    {
        TTF_CloseFont(game_font);
    }
    TTF_Quit();

    free_all_textures();
    if (gl_context != NULL)
        SDL_GL_DeleteContext(gl_context);
    if (window != NULL)
        SDL_DestroyWindow(window);
    SDL_Quit();
}