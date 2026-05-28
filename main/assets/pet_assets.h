#pragma once
#include <stdint.h>

#define CAT_SPRITE_WIDTH 32
#define CAT_SPRITE_HEIGHT 24
#define PET_WIDTH CAT_SPRITE_WIDTH
#define PET_HEIGHT CAT_SPRITE_HEIGHT

#define BOWL_SPRITE_WIDTH 40
#define BOWL_SPRITE_HEIGHT 18
#define CAT_FLIP_FRAME_COUNT 4
#define BOWL_FRAME_COUNT 5

extern const uint16_t cat_idle[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_happy[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_hungry[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_thirsty[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_tired[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_sleep[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];

extern const uint16_t cat_flip_0[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_flip_1[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_flip_2[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t cat_flip_3[CAT_SPRITE_WIDTH * CAT_SPRITE_HEIGHT];
extern const uint16_t *const cat_flip_frames[CAT_FLIP_FRAME_COUNT];

extern const uint16_t food_bowl_0[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_25[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_50[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_75[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t food_bowl_100[BOWL_SPRITE_WIDTH * BOWL_SPRITE_HEIGHT];
extern const uint16_t *const food_bowl_frames[BOWL_FRAME_COUNT];
