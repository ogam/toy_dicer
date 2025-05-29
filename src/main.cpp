#include <cute.h>

typedef int32_t s32;
typedef uint64_t u64;
typedef uint8_t u8;
typedef int32_t b32;
typedef float f32;

#define STR(X) #X

const char* shader_str = STR(
                             vec4 shader(vec4 color, vec2 pos, vec2 screen_uv, vec4 params)
                             {
                                 vec4 fade_color = params;
                                 vec4 result = texture(u_image, smooth_uv(v_uv, u_texture_size));
                                 // this should be layered on top if possible, if placing with a color vec4(0) then this would show as white pixels
                                 return result * fade_color;
                             }
                             );

#include "entity.h"
#include "audio.h"

enum GameMode
{
    GameMode_Menu,
    GameMode_Playing,
    GameMode_Paused,
    GameMode_Game_Over
};

struct Ctx
{
    // draw
    CF_Shader shader;
    CF_Mesh mesh;
    CF_Material material;
    CF_M3x2 projection;
    CF_Vertex* verts;
    CF_V2 atlas_dims;
    
    CF_V2 screen_size;
    
    // input
    CF_V2 mouse_position;
    CF_V2 mouse_world_position;
    
    // assets
    CF_Audio music_track;
    CF_Audio slice_track;
    CF_Sound music_handle;
    CF_Sprite bullet_sprite;
    
    // audio
    AudioInfo audio_info;
    s32 last_spawn_fft_index;
    f32 volume;
    
    // entities
    Player player;
    dyna Bullet* bullets;
    dyna SpriteChunk* chunks;
    
    CF_Arena arena;
    CF_Threadpool* threadpool;
    
    GameMode mode;
    f32 dt;
    
    CF_Rnd rnd;
};

static Ctx ctx;

#include "entity.cpp"
#include "audio.cpp"

void mount_directory(const char* dir);
void init(s32 w, s32 h);
void cleanup();
void load_assets();
void task_process_audio(void* udata);
void set_game_mode(GameMode next_mode);
void load_level();
void update_level();

void update_ui();

void update(void* udata);
void draw();
void frame_cleanup();

void mount_directory(const char* dir)
{
    const char* path = cf_fs_get_base_directory();
    path = cf_path_normalize(path);
    //  @todo:  before shipping this needs to be a pop_n(path, 1)
    path = cf_path_pop_n(path, 2);
    
    char mount_path[1024];
    
    CF_SNPRINTF(mount_path, sizeof(mount_path), "%s/%s", path, dir);
	cf_fs_mount(mount_path, "/", false);
}

void init(s32 w, s32 h)
{
    ctx.atlas_dims = cf_v2(2048, 2048);
    cf_draw_set_atlas_dimensions((s32)ctx.atlas_dims.x, (s32)ctx.atlas_dims.y);
    
    // cute_draw.h
    // void cf_make_draw()
    CF_VertexAttribute attrs[] = {
        {
            .name = "in_pos",
            .format = CF_VERTEX_FORMAT_FLOAT2,
            .offset = CF_OFFSET_OF(CF_Vertex, p)
        },
        {
            .name = "in_posH", 
            .format = CF_VERTEX_FORMAT_FLOAT2, 
            .offset = CF_OFFSET_OF(CF_Vertex, posH)
        },
        {
            .name = "in_n", 
            .format = CF_VERTEX_FORMAT_INT, 
            .offset = CF_OFFSET_OF(CF_Vertex, n)
        },
        {
            .name = "in_ab", 
            .format = CF_VERTEX_FORMAT_FLOAT4, 
            .offset = CF_OFFSET_OF(CF_Vertex, shape[0])
        },
        {
            .name = "in_cd", 
            .format = CF_VERTEX_FORMAT_FLOAT4, 
            .offset = CF_OFFSET_OF(CF_Vertex, shape[2])
        },
        {
            .name = "in_ef", 
            .format = CF_VERTEX_FORMAT_FLOAT4, 
            .offset = CF_OFFSET_OF(CF_Vertex, shape[4])
        },
        {
            .name = "in_gh", 
            .format = CF_VERTEX_FORMAT_FLOAT4, 
            .offset = CF_OFFSET_OF(CF_Vertex, shape[6])
        },
        {
            .name = "in_uv", 
            .format = CF_VERTEX_FORMAT_FLOAT2, 
            .offset = CF_OFFSET_OF(CF_Vertex, uv)
        },
        {
            .name = "in_col", 
            .format = CF_VERTEX_FORMAT_UBYTE4_NORM, 
            .offset = CF_OFFSET_OF(CF_Vertex, color)
        },
        {
            .name = "in_radius", 
            .format = CF_VERTEX_FORMAT_FLOAT, 
            .offset = CF_OFFSET_OF(CF_Vertex, radius)
        },
        {
            .name = "in_stroke", 
            .format = CF_VERTEX_FORMAT_FLOAT, 
            .offset = CF_OFFSET_OF(CF_Vertex, stroke)
        },
        {
            .name = "in_aa", 
            .format = CF_VERTEX_FORMAT_FLOAT, 
            .offset = CF_OFFSET_OF(CF_Vertex, aa)
        },
        {
            .name = "in_params", 
            .format = CF_VERTEX_FORMAT_UBYTE4_NORM, 
            .offset = CF_OFFSET_OF(CF_Vertex, type)
        },
        {
            .name = "in_user_params", 
            .format = CF_VERTEX_FORMAT_FLOAT4, 
            .offset = CF_OFFSET_OF(CF_Vertex, attributes)
        }
    };
    ctx.shader = cf_make_draw_shader_from_source(shader_str);
    ctx.mesh = cf_make_mesh(CF_MB * 5, attrs, CF_ARRAY_SIZE(attrs), sizeof(CF_Vertex));
    {
        ctx.material = cf_make_material();
        CF_RenderState state = cf_render_state_defaults();
        state.blend.enabled = true;
        state.blend.rgb_src_blend_factor = CF_BLENDFACTOR_ONE;
        state.blend.rgb_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        state.blend.rgb_op = CF_BLEND_OP_MAX;
        state.blend.alpha_src_blend_factor = CF_BLENDFACTOR_ONE;
        state.blend.alpha_dst_blend_factor = CF_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        state.blend.alpha_op = CF_BLEND_OP_MIN;
        cf_material_set_render_state(ctx.material, state);
    }
    
    ctx.verts = NULL;
    cf_array_fit(ctx.verts, 1 << 15);
    
    ctx.arena = cf_make_arena(8, CF_MB * 5);
    
    ctx.threadpool = cf_make_threadpool(1);
    
    mount_directory("data");
    
    ctx.player = {};
    cf_array_fit(ctx.bullets, 1024);
    
    ctx.volume = 0.7f;
    
    ctx.rnd = cf_rnd_seed(0);
}

void cleanup()
{
    cf_destroy_threadpool(ctx.threadpool);
    cf_destroy_material(ctx.material);
    cf_destroy_mesh(ctx.mesh);
    //cf_destroy_shader(ctx.shader);
    cf_destroy_arena(&ctx.arena);
}

void load_assets()
{
    ctx.music_track = cf_audio_load_ogg("music.ogg");
    ctx.slice_track = cf_audio_load_ogg("slice.ogg");
    ctx.bullet_sprite = cf_make_sprite("bullet.ase");
    
    ctx.audio_info = {};
    ctx.audio_info.src = ctx.music_track;
    ctx.audio_info.arena = &ctx.arena;
    // 100ms
    ctx.audio_info.min_delay_between_beats = 0.1f;
    ctx.audio_info.min_magnitude_to_detect_beat = 40.0f;
    
    cf_threadpool_add_task(ctx.threadpool, task_process_audio, &ctx.audio_info);
    cf_threadpool_kick(ctx.threadpool);
}

void task_process_audio(void* udata)
{
    AudioInfo* info = (AudioInfo*)udata;
    info->is_processing = true;
    process_audio(info);
    info->is_processing = false;
}

void set_game_mode(GameMode next_mode)
{
    if (ctx.mode != next_mode)
    {
        if (ctx.mode == GameMode_Playing)
        {
            if (next_mode == GameMode_Paused)
            {
                cf_sound_set_is_paused(ctx.music_handle, true);
            }
        }
        else if (ctx.mode == GameMode_Paused)
        {
            if (next_mode == GameMode_Playing)
            {
                cf_sound_set_is_paused(ctx.music_handle, false);
            }
        }
        
        ctx.mode = next_mode;
    }
}

void load_level()
{
    CF_SoundParams params = cf_sound_params_defaults();
    params.volume = ctx.volume;
    ctx.music_handle = cf_play_sound(ctx.music_track, params);
    ctx.rnd = cf_rnd_seed(ctx.audio_info.hash);
    
    ctx.last_spawn_fft_index = 0;
    
    ctx.player = {};
    cf_array_clear(ctx.bullets);
    cf_array_clear(ctx.chunks);
    
    set_game_mode(GameMode_Playing);
}

void update_level()
{
    AudioBeat* beats = ctx.audio_info.beats;
    
    f32 bullet_life_time = 2.0f;
    
    s32 sample_index = cf_sound_get_sample_index(ctx.music_handle);
    s32 spawn_sample_index = sample_index + cf_audio_sample_rate(ctx.music_track) * (s32)bullet_life_time;
    
    s32 fft_index = spawn_sample_index / FFT_SIZE;
    CF_V2 spawn_min = cf_v2(-200.0f, -200.0f);
    CF_V2 spawn_max = cf_v2(200.0f, 200.0f);
    
    CF_Color text_color = cf_color_grey();
    
    cf_sound_set_volume(ctx.music_handle, ctx.volume);
    
    for (s32 beat_index = 0; beat_index < cf_array_count(beats); ++beat_index)
    {
        AudioBeat* beat = beats + beat_index;
        if (beat->sample_index > spawn_sample_index)
        {
            break;
        }
        
        if (sample_index / FFT_SIZE == beat->fft_index)
        {
            text_color = cf_color_red();
        }
        
        if (fft_index == beat->fft_index)
        {
            if (ctx.last_spawn_fft_index < fft_index)
            {
                CF_V2 bullet_position;
                bullet_position.x = cf_rnd_range_float(&ctx.rnd, spawn_min.x, spawn_max.x);
                bullet_position.y = cf_rnd_range_float(&ctx.rnd, spawn_min.y, spawn_max.y);
                Bullet bullet = make_bullet(bullet_position, bullet_life_time);
                cf_array_push(ctx.bullets, bullet);
                ctx.last_spawn_fft_index = fft_index;
            }
            break;
        }
    }
    
    if (cf_key_just_pressed(CF_KEY_ESCAPE))
    {
        if (ctx.mode == GameMode_Playing)
        {
            set_game_mode(GameMode_Paused);
        }
        else if (ctx.mode == GameMode_Paused)
        {
            set_game_mode(GameMode_Playing);
        }
    }
    
    // loop for now
    if (ctx.mode == GameMode_Playing)
    {
        if (!cf_sound_is_active(ctx.music_handle))
        {
            set_game_mode(GameMode_Game_Over);
        }
    }
}

void update_ui()
{
    cf_push_font("Calibri");
    cf_push_font_size(32);
    
    f32 button_skin = 5.0f;
    f32 y_padding = 10.0f;
    CF_V2 item_cursor_position = cf_v2(0, 0);
    
    cf_draw_push_layer(DRAW_LAYER_UI);
    b32 is_audio_still_processing = ctx.audio_info.is_processing;
    
    const char* play_text = "Play";
    
    if (ctx.mode == GameMode_Paused)
    {
        play_text = "Resume";
    }
    
    // this should be do_button() or something but this is a 1 day project..
    if (ctx.mode != GameMode_Playing)
    {
        // play/resume button
        {
            CF_V2 text_size = cf_text_size(play_text, -1);
            CF_V2 position = item_cursor_position;
            position.x -= text_size.x * 0.5f;
            CF_V2 min = position;
            CF_V2 max = position;
            min.y -= text_size.y;
            max.x += text_size.x;
            
            CF_Aabb aabb = cf_make_aabb(min, max);
            CF_Color background_color = cf_color_grey();
            CF_Color text_color = cf_color_white();
            
            if (cf_contains_point(aabb, ctx.mouse_world_position) && !is_audio_still_processing)
            {
                background_color = cf_color_orange();
                if (cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT))
                {
                    if (ctx.mode == GameMode_Paused)
                    {
                        set_game_mode(GameMode_Playing);
                    }
                    else
                    {
                        load_level();
                    }
                }
            }
            
            cf_draw_push_color(background_color);
            cf_draw_box_fill(aabb, 0.0f);
            cf_draw_pop_color();
            cf_draw_push_color(text_color);
            cf_draw_text(play_text, position, -1);
            cf_draw_pop_color();
            
            item_cursor_position.y -= text_size.y + y_padding;
        }
        
        // exit
        {
            CF_V2 text_size = cf_text_size("Exit", -1);
            CF_V2 position = item_cursor_position;
            position.x -= text_size.x * 0.5f;
            CF_V2 min = position;
            CF_V2 max = position;
            min.y -= text_size.y;
            max.x += text_size.x;
            
            CF_Aabb aabb = cf_make_aabb(min, max);
            CF_Color background_color = cf_color_grey();
            CF_Color text_color = cf_color_white();
            
            if (cf_contains_point(aabb, ctx.mouse_world_position))
            {
                background_color = cf_color_orange();
                if (cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT))
                {
                    cf_app_signal_shutdown();
                }
            }
            
            cf_draw_push_color(background_color);
            cf_draw_box_fill(aabb, 0.0f);
            cf_draw_pop_color();
            cf_draw_push_color(text_color);
            cf_draw_text("Exit", position, -1);
            cf_draw_pop_color();
            
            item_cursor_position.y -= text_size.y + y_padding;
        }
    }
    
    // draw score
    if (ctx.mode == GameMode_Playing || ctx.mode == GameMode_Paused || ctx.mode == GameMode_Game_Over)
    {
        Player* player = &ctx.player;
        char buf[1024];
        CF_SNPRINTF(buf, sizeof(buf), "Slice: %d\nScore: %d", player->bullet_slice_count, player->score);
        CF_V2 text_size = cf_text_size(buf, -1);
        
        CF_V2 position = cf_v2(-ctx.screen_size.x * 0.5f, -ctx.screen_size.y * 0.5f + text_size.y);
        cf_draw_text(buf, position, -1);
    }
    
    // loading text
    if (ctx.audio_info.is_processing)
    {
        f32 progress = audio_processing_get_progress(&ctx.audio_info);
        progress *= 100.0f;
        
        char buf[1024];
        CF_SNPRINTF(buf, sizeof(buf), "Loading: %3.0f", progress);
        
        CF_Color text_color = cf_color_white();
        cf_draw_push_color(text_color);
        
        CF_V2 text_size = cf_text_size(buf, -1);
        // center bottom
        CF_V2 text_position = cf_v2(-text_size.x * 0.5f, -ctx.screen_size.y * 0.5f + text_size.y);
        cf_draw_text(buf, text_position, -1);
        
        cf_draw_pop_color();
    }
    
    cf_draw_pop_layer();
    
    cf_pop_font_size();
    cf_pop_font();
}

void update(void* udata)
{
    cf_array_clear(ctx.verts);
    
    ctx.dt = 0;
    if (ctx.mode == GameMode_Playing)
    {
        ctx.dt = CF_DELTA_TIME;
    }
    
    // window events
    {
        ctx.screen_size.x = (f32)cf_app_get_width();
        ctx.screen_size.y = (f32)cf_app_get_height();
        
        ctx.projection = cf_ortho_2d(0, 0, ctx.screen_size.x, ctx.screen_size.y);
        cf_draw_projection(ctx.projection);
        
        if (cf_app_was_resized())
        {
            // rebuild app_canvas otherwise things will look blurry
            cf_app_set_canvas_size(cf_app_get_width(), cf_app_get_height());
        }
        
        b32 hide_mouse = cf_app_has_focus() && cf_app_mouse_inside();
        
        //  @todo:  api has this backwards, get a fix in 
        cf_mouse_hide(!hide_mouse);
    }
    // input events
    {
        ctx.mouse_position.x = (f32)cf_mouse_x();
        ctx.mouse_position.y = (f32)cf_mouse_y();
        ctx.mouse_world_position = cf_screen_to_world(ctx.mouse_position);
    }
    
    if (!ctx.audio_info.is_processing)
    {
        if (ctx.audio_info.arena != NULL)
        {
            cf_arena_reset(ctx.audio_info.arena);
            ctx.audio_info.arena = NULL;
        }
    }
    
    update_level();
    
    update_player();
    update_bullets();
    update_sprite_chunks();
    
    update_ui();
}

void draw()
{
    draw_player();
    draw_bullets();
    draw_sprite_chunks();
    
    CF_Canvas app_canvas = cf_app_get_canvas();
    
    {
        CF_TemporaryImage image = cf_fetch_image(&ctx.bullet_sprite);
        
        cf_draw_push_layer(DRAW_LAYER_SPRITE_CHUNK);
        
        cf_apply_canvas(app_canvas, true);
        cf_draw_push_shader(ctx.shader);
        
        // still relying on base shader so need to setup `u_image` and `u_texture_size`
        // TODO: you might have more than 1 texture so this would cause
        //       you to get bad sampling if you have a lot of sprites
        cf_material_set_texture_fs(ctx.material, "u_image", image.tex);
        cf_material_set_uniform_fs(ctx.material, "u_texture_size", &ctx.atlas_dims, CF_UNIFORM_TYPE_FLOAT2, 1);
        
        cf_mesh_update_vertex_data(ctx.mesh, ctx.verts, cf_array_count(ctx.verts));
        cf_apply_mesh(ctx.mesh);
        
        cf_apply_shader(ctx.shader, ctx.material);
        cf_draw_elements();
        
        cf_draw_pop_shader();
        
        cf_draw_pop_layer();
    }
}

void frame_cleanup()
{
    // shift entire arrays instead of unordered pops to maintain back to front draw order
    {
        Bullet* bullets = ctx.bullets;
        s32 index = 0;
        while (index < cf_array_count(bullets))
        {
            Bullet* bullet = bullets + index;
            if (bullet->is_dead)
            {
                s32 copy_count = cf_array_count(bullets) - 1;
                CF_MEMCPY(bullets + index, bullets + index + 1, sizeof(Bullet) * copy_count);
                cf_array_pop(bullets);
                continue;
            }
            ++index;
        }
    }
    
    {
        SpriteChunk* chunks = ctx.chunks;
        s32 index = 0;
        while (index < cf_array_count(chunks))
        {
            SpriteChunk* chunk = chunks + index;
            if (chunk->fade_time < 0)
            {
                s32 copy_count = cf_array_count(chunks) - 1;
                CF_MEMCPY(chunks + index, chunks + index + 1, sizeof(SpriteChunk) * copy_count);
                cf_array_pop(chunks);
                continue;
            }
            ++index;
        }
    }
}

int main(int argc, char** argv)
{
    s32 w = 640;
    s32 h = 480;
    
    s32 options = CF_APP_OPTIONS_WINDOW_POS_CENTERED_BIT | CF_APP_OPTIONS_RESIZABLE_BIT;
    CF_Result result = cf_make_app("Toy Dicer", 0, 0, 0, w, h, options, argv[0]);
	if (cf_is_error(result)) return -1;
    
    init(w, h);
    load_assets();
    
    while (cf_app_is_running())
    {
        cf_app_update(update);
        draw();
        
        cf_app_draw_onto_screen(false);
        frame_cleanup();
    }
    
    cleanup();
    
    cf_destroy_app();
    
    return 0;
}