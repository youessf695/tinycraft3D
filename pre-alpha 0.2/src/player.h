#ifndef PLAYER_H
#define PLAYER_H

// نقوم بتضمين common.h فقط ليقرأ التعريف الموحد للاعب
#include "common.h" 

// هنا نترك إعلانات الدوال فقط بدون إعادة تعريف الهيكل
void init_player(Player *player);
void handle_player_mouse(Player *player, float xrel, float yrel);
void handle_player_input(Player *player, float dt);
void update_player(Player *player, float dt);

#endif