#ifndef ENTITY_H
#define ENTITY_H

#define SPRITE_CHUNK_FADE_TIME (1.0f)
#define DRAW_LAYER_ENEMY (1)
#define DRAW_LAYER_BULLET (2)
#define DRAW_LAYER_SPRITE_CHUNK (3)
#define DRAW_LAYER_PLAYER (4)
// this shouldn't really be here
#define DRAW_LAYER_UI (5)
#define DRAW_LAYER_CURSOR (6)

enum StrokeState
{
    StrokeState_None,
    StrokeState_Begin,
    StrokeState_Drawing,
};

struct SpriteChunk
{
    CF_Sprite sprite;
    CF_Poly poly;
    CF_V2 uvs[8];
    CF_V2 velocity;
    CF_Color color;
    f32 fade_time;
    
    b32 can_pause;
};

struct Player
{
    CF_V2 points[64];
    s32 point_count;
    f32 idle_decay_time;
    s32 score;
    s32 bullet_slice_count;
    s32 stroke_count;
    StrokeState stroke_state;
    StrokeState prev_stroke_state;
};

struct Bullet
{
    CF_V2 position;
    CF_Sprite sprite;
    f32 life_time;
    f32 base_life_time;
    b32 is_dead;
};

void points_pop_front_n(CF_V2* points, s32* count, s32 capacity, s32 n);
void update_player();
void draw_player();

Bullet make_bullet(CF_V2 position, f32 life_time);
void update_bullets();
void draw_bullets();

void set_sprite_chunk(SpriteChunk* chunk, CF_Poly* poly, CF_Sprite* sprite, CF_V2 position, f32 push_strength);
b32 do_slice_sprite(CF_Sprite* sprite, CF_V2 position, CF_Color sprite_color, CF_V2 start, CF_V2 end);
void update_sprite_chunks();
void draw_sprite_chunks();

#endif
