#include "SDL2/SDL.h"
#include "src/gameloop.h"

int main(int argc, char* argv[]) {
    // 1. تهيئة سياق الـ 3D ونافذة OpenGL والأنسجة
    if (!init_game()) {
        return 1;
    }

    // 2. تشغيل حلقة اللعبة الأساسية ثلاثية الأبعاد
    run_game_loop();

    // 3. تنظيف الذاكرة وحفظ العالم عند الإغلاق
    clean_game();

    return 0;
}