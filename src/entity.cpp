void points_pop_front_n(CF_V2* points, s32* count, s32 capacity, s32 n)
{
    if (*count > 0 && capacity > 0)
    {
        n = cf_min(n, *count);
        s32 start_index = n;
        CF_MEMCPY(points, points + start_index, sizeof(CF_V2) * (capacity - start_index));
        *count -= start_index;
    }
}

void update_player()
{
    Player* player = &ctx.player;
    CF_V2 next_point = ctx.mouse_world_position;
    f32 min_point_distance_sq = 7.5f * 7.5f;
    f32 decay_start_time = 0.25f;
    f32 decay_interval = CF_DELTA_TIME * 5;
    
    CF_V2* slice_p0 = NULL;
    CF_V2* slice_p1 = NULL;
    
    player->prev_stroke_state = player->stroke_state;
    // you can draw while paused, just not slice stuff
    player->idle_decay_time -= CF_DELTA_TIME;
    
    if (cf_mouse_just_pressed(CF_MOUSE_BUTTON_LEFT))
    {
        player->points[0] = next_point;
        player->point_count = 1;
        player->stroke_state = StrokeState_Begin;
    }
    else if (cf_mouse_down(CF_MOUSE_BUTTON_LEFT))
    {
        CF_V2 last_point = player->points[player->point_count - 1];
        CF_V2 dp = cf_sub_v2(last_point, next_point);
        if (cf_len_sq(dp) > min_point_distance_sq)
        {
            if (player->point_count >= CF_ARRAY_SIZE(player->points))
            {
                s32 removal_count = player->point_count -  CF_ARRAY_SIZE(player->points) + 1;
                points_pop_front_n(player->points, &player->point_count, CF_ARRAY_SIZE(player->points), removal_count);
            }
            
            slice_p0 = &player->points[player->point_count - 1];
            slice_p1 = &next_point;
            
            player->points[player->point_count++] = next_point;
            
            player->idle_decay_time = decay_start_time;
            
            player->stroke_state = StrokeState_Drawing;
        }
        
        if (player->idle_decay_time < 0)
        {
            if (cf_on_interval(decay_interval, 0))
            {
                points_pop_front_n(player->points, &player->point_count, CF_ARRAY_SIZE(player->points), 1);
            }
        }
    }
    else
    {
        // decay when mouse not drawing
        if (cf_on_interval(decay_interval, 0))
        {
            points_pop_front_n(player->points, &player->point_count, CF_ARRAY_SIZE(player->points), 1);
        }
        
        if (player->stroke_state != StrokeState_None)
        {
            player->stroke_count++;
        }
        player->stroke_state = StrokeState_None;
    }
    
    if (ctx.mode == GameMode_Playing)
    {
        if (slice_p0 && slice_p1)
        {
            Bullet* bullets = ctx.bullets;
            for (s32 index = 0; index < cf_array_count(bullets); ++index)
            {
                Bullet* bullet = bullets + index;
                if (do_slice_sprite(&bullet->sprite, bullet->position, cf_color_white(), *slice_p0, *slice_p1))
                {
                    bullet->is_dead = true;
                    cf_sprite_pause(&bullet->sprite);
                    player->bullet_slice_count++;
                    
                    f32 score_t = 1.0f - bullet->life_time / bullet->base_life_time;
                    f32 add_score = score_t * 100.0f;
                    add_score = CF_FLOORF(add_score);
                    player->score += (s32)add_score;
                    
                    // play faster if size is small
                    CF_SoundParams params = cf_sound_params_defaults();
                    params.pitch = cf_remap01(score_t, 2.0f, 1.0f);
                    params.volume = ctx.volume;
                    cf_play_sound(ctx.slice_track, params);
                }
            }
        }
    }
}

void draw_player()
{
    Player* player = &ctx.player;
    
    static s32 color_index = 0;
    
    // no purple and yellow, when hsv lerped to clear, these colors are too close to their neighbors
    CF_Color start_colors[] = {
        cf_color_red(),
        cf_color_orange(),
        //cf_color_yellow(),
        cf_color_green(),
        cf_color_cyan(),
        cf_color_blue(),
        //cf_color_purple(),
        cf_color_magenta(),
    };
    
    if (player->prev_stroke_state == StrokeState_None && player->stroke_state != StrokeState_None)
    {
        color_index = (color_index + 1) % CF_ARRAY_SIZE(start_colors);
    }
    
    CF_Color line_start_color = start_colors[color_index];
    CF_Color line_end_color = cf_color_clear();
    
    if (player->point_count == 2)
    {
        cf_draw_push_color(line_start_color);
        CF_V2 c0 = player->points[0];
        CF_V2 c1 = player->points[1];
        cf_draw_line(c0, c1, 1.0f);
        cf_draw_pop_color();
    }
    else if (player->point_count == 3)
    {
        cf_draw_push_color(line_start_color);
        CF_V2 c0 = player->points[0];
        CF_V2 c1 = player->points[1];
        CF_V2 c2 = player->points[2];
        cf_draw_bezier_line(c0, c1, c2, 32, 1.0f);
        cf_draw_pop_color();
    }
    else
    {
        CF_Color hsv_start = cf_rgb_to_hsv(line_start_color);
        CF_Color hsv_end = cf_rgb_to_hsv(line_end_color);
        f32 line_thickness_start = 2.0f;
        f32 line_thickness_end = 0.0f;
        
        for (s32 point_index = 0; point_index < player->point_count - 3; ++point_index)
        {
            f32 t = 1.0f - (f32)point_index / player->point_count;
            t = cf_smoothstep(t);
            CF_Color hsv = cf_color_lerp(hsv_start, hsv_end, t);
            CF_Color color = cf_hsv_to_rgb(hsv);
            f32 line_thickness = cf_lerp(line_thickness_start, line_thickness_end, t);
            
            cf_draw_push_color(color);
            
            CF_V2 c0 = player->points[point_index    ];
            CF_V2 c1 = player->points[point_index + 1];
            CF_V2 c2 = player->points[point_index + 2];
            CF_V2 c3 = player->points[point_index + 3];
            cf_draw_bezier_line2(c0, c1, c2, c3, 32, line_thickness);
            
            cf_draw_pop_color();
        }
    }
    cf_draw_pop_layer();
    
    //  @todo:  fix draw order, some reason this isn't drawing above UI even though layer is higher?
    cf_draw_push_layer(DRAW_LAYER_CURSOR);
    {
        f32 cursor_radius = 2.5f;
        CF_Color cursor_color = cf_color_grey();
        
        if (player->stroke_state != StrokeState_None)
        {
            cursor_radius *= 2;
            cursor_color = cf_color_white();
        }
        
        cf_draw_push_color(cursor_color);
        cf_draw_circle_fill2(ctx.mouse_world_position, cursor_radius);
        cf_draw_pop_color();
    }
    cf_draw_pop_layer();
}

Bullet make_bullet(CF_V2 position, f32 life_time)
{
    Bullet bullet = {
        .position = position,
        .sprite = ctx.bullet_sprite,
        .life_time = life_time,
        .base_life_time = life_time
    };
    
    bullet.sprite.scale = cf_v2(5, 5);
    
    return bullet;
}

void update_bullets()
{
    Bullet* bullets = ctx.bullets;
    f32 dt = ctx.dt;
    
    for (s32 index = 0; index < cf_array_count(bullets); ++index)
    {
        Bullet* bullet = bullets + index;
        bullet->life_time -= dt;
        
        if (bullet->life_time < 0)
        {
            bullet->is_dead = true;
        }
        
        if (!bullet->is_dead)
        {
            f32 t = cf_max(1.0f - bullet->life_time / bullet->base_life_time, 0.0f);
            t = cf_smoothstep(t);
            CF_V2 scale_start = cf_v2(0, 0);
            CF_V2 scale_end = cf_v2(5, 5);
            CF_V2 scale = cf_lerp_v2(scale_start, scale_end, t);
            bullet->sprite.scale = scale;
        }
        
        if (dt > 0)
        {
            cf_sprite_update(&bullet->sprite);
        }
    }
}

void draw_bullets()
{
    Bullet* bullets = ctx.bullets;
    cf_draw_push_layer(DRAW_LAYER_BULLET);
    
    // draw back to front, newest bullets are always spawned furthest back
    for (s32 index = cf_array_count(bullets) - 1; index >= 0; --index)
    {
        Bullet* bullet = bullets + index;
        if (!bullet->is_dead)
        {
            bullet->sprite.transform.p = bullet->position;
            cf_draw_sprite(&bullet->sprite);
        }
    }
    
    cf_draw_pop_layer();
}

void set_sprite_chunk(SpriteChunk* chunk, CF_Poly* poly, CF_Sprite* sprite, CF_V2 position, f32 push_strength)
{
    CF_V2 sprite_bottom_left = position;
    f32 w = sprite->w * sprite->scale.x;
    f32 h = sprite->h * sprite->scale.y;
    sprite_bottom_left.x -= w * 0.5f;
    sprite_bottom_left.y -= h * 0.5f;
    
    chunk->sprite = *sprite;
    cf_sprite_pause(&chunk->sprite);
    
    chunk->poly = *poly;
    for (s32 index = 0; index < chunk->poly.count; ++index)
    {
        CF_V2 uv = cf_sub_v2(chunk->poly.verts[index], sprite_bottom_left);
        uv.x = cf_abs(uv.x / w);
        uv.y = cf_abs(uv.y / h);
        
        chunk->uvs[index] = uv;
    }
    
    chunk->velocity = cf_center_of_mass(chunk->poly);
    chunk->velocity = cf_sub_v2(chunk->velocity, sprite->transform.p);
    chunk->velocity = cf_norm(chunk->velocity);
    chunk->velocity = cf_mul_v2_f(chunk->velocity, push_strength);
}

b32 do_slice_sprite(CF_Sprite* sprite, CF_V2 position, CF_Color sprite_color, CF_V2 start, CF_V2 end)
{
    CF_V2 sprite_min = position;
    CF_V2 sprite_max = sprite_min;
    f32 w = sprite->w * sprite->scale.x;
    f32 h = sprite->h * sprite->scale.y;
    
    sprite_min.x -= w / 2;
    sprite_min.y -= h / 2;
    sprite_max.x += w / 2;
    sprite_max.y += h / 2;
    
    CF_Aabb aabb = cf_make_aabb(sprite_min, sprite_max);
    CF_V2 sprite_top_left = cf_top_left(aabb);
    
    f32 push_strength = 10.0f;
    
    CF_Ray ray;
    ray.p = start;
    ray.d = cf_sub_v2(end, start);
    ray.d = cf_safe_norm(ray.d);
    ray.t = cf_distance(end, start);
    
    b32 is_sliced = false;
    
    if (ray.t > 0 && !cf_contains_point(aabb, ray.p))
    {
        CF_Raycast hit_result = cf_ray_to_aabb(ray, aabb);
        if (hit_result.hit)
        {
            CF_V2 hit_point = cf_mul_v2_f(ray.d, hit_result.t);
            hit_point = cf_add_v2(start, hit_point);
            
            CF_Poly sprite_poly;
            sprite_poly.verts[0] = sprite_min;
            sprite_poly.verts[1] = cf_v2(sprite_min.x, sprite_max.y);
            sprite_poly.verts[2] = cf_v2(sprite_max.x, sprite_min.y);
            sprite_poly.verts[3] = sprite_max;
            sprite_poly.count = 4;
            cf_make_poly(&sprite_poly);
            
            const f32 epsilon = 1e-4f;
            CF_V2 n = cf_perp(ray.d);
            
            CF_Halfspace slice_plane = cf_plane2(n, hit_point);
            CF_SliceOutput output = cf_slice(slice_plane, sprite_poly, epsilon);
            
            SpriteChunk chunk = {};
            chunk.fade_time = SPRITE_CHUNK_FADE_TIME;
            chunk.color = sprite_color;
            
            set_sprite_chunk(&chunk, &output.front, sprite, position, push_strength);
            cf_array_push(ctx.chunks, chunk);
            
            set_sprite_chunk(&chunk, &output.back, sprite, position, push_strength);
            cf_array_push(ctx.chunks, chunk);
            
            is_sliced = true;
        }
    }
    return is_sliced;
}

void update_sprite_chunks()
{
    SpriteChunk* chunks = ctx.chunks;
    f32 dt = ctx.dt;
    for (int index = 0; index < cf_array_count(chunks); ++index)
    {
        SpriteChunk* chunk = chunks + index;
        CF_Poly* poly = &chunk->poly;
        CF_V2 dp = cf_mul_v2_f(chunk->velocity, dt);
        
        for (s32 poly_index = 0; poly_index < poly->count; ++poly_index)
        {
            poly->verts[poly_index] = cf_add_v2(poly->verts[poly_index], dp);
        }
        
        chunk->fade_time -= dt;
    }
}

void draw_sprite_chunks()
{
    CF_M3x2 mvp = cf_draw_peek();
    mvp = cf_mul_m32(ctx.projection, mvp);
    
    CF_Vertex verts[8] = {};
    
    SpriteChunk* chunks = ctx.chunks;
    
    for (s32 index = cf_array_count(chunks) - 1; index >= 0; --index)
    {
        SpriteChunk* chunk = chunks + index;
        
        CF_TemporaryImage image = cf_fetch_image(&chunk->sprite);
        CF_V2 duv = cf_sub_v2(image.v, image.u);
        
        f32 opacity = cf_max(chunk->fade_time / SPRITE_CHUNK_FADE_TIME, 0.0f);
        u8 alpha = (u8)(opacity * 255.0f);
        // reverse lerp since opacity 1.0f -> 0.0f
        CF_Color color = cf_color_lerp(cf_color_clear(), chunk->color, opacity);
        
        for (int vertex = 0; vertex < chunk->poly.count; ++vertex)
        {
            CF_Vertex* vert = verts + vertex;
            CF_V2 uv = cf_mul_v2(chunk->uvs[vertex], duv);
            uv = cf_add_v2(image.u, uv);
            
            vert->p = chunk->poly.verts[vertex];
            vert->posH = cf_mul_m32_v2(mvp, vert->p);
            vert->uv = uv;
            vert->aa = false;
            vert->attributes = color;
        }
        
        for (int vertex = 0; vertex < chunk->poly.count - 1; ++vertex)
        {
            cf_array_push(ctx.verts, verts[0]);
            cf_array_push(ctx.verts, verts[vertex]);
            cf_array_push(ctx.verts, verts[vertex + 1]);
        }
    }
}
