#ifndef GAMELOOP_H
#define GAMELOOP_H

#include <stdbool.h>

// دوال تهيئة وإدارة وتشغيل الحلقة الأساسية للعبة 3D
bool init_game(void);
void run_game_loop(void);
void clean_game(void);

#endif // GAMELOOP_H