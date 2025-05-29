#include <float.h>

#define FFT_SIZE (1024)
#define SAMPLE_MAX ((1 << 15) - 1)
#define SAMPLE_MIN (-(1 << 15))

//  cute_sound.h
typedef struct cs_audio_source_t
{
	s32 sample_rate;
	s32 sample_count;
	s32 channel_count;
    
	// Number of instances currently referencing this audio. Must be zero
	// in order to safely delete the audio. References are automatically
	// updated whenever playing instances are inserted into the context.
	s32 playing_count;
    
	// The actual raw audio samples in memory.
	void* channels[2];
} cs_audio_source_t;

Band make_band(f32 minHz, f32 maxHz)
{
    Band band = {};
    band.min = minHz;
    band.max = maxHz;
    return band;
}

//  https://literateprograms.org/cooley-tukey_fft_algorithm__c_.html#:~:text=The%20Cooley%2DTukey%20FFT%20algorithm,usually%20a%20power%20of%202.
void fft(f32* re, f32* im, s32 n, f32* out_re, f32* out_im, CF_Arena* arena)
{
    if (n == 1)
    {
        out_re[0] = re[0];
        out_im[0] = im[0];
        return;
    }
    
    f32 *d_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    f32 *e_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    f32 *d_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    f32 *e_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    
    f32 *D_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    f32 *E_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    f32 *D_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    f32 *E_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * n / 2);
    
    for (s32 k = 0; k < n / 2; ++k)
    {
        e_re[k] = re[2 * k];
        e_im[k] = im[2 * k];
        d_re[k] = re[2 * k + 1];
        d_im[k] = re[2 * k + 1];
    }
    
    fft(e_re, e_im, n / 2, E_re, E_im, arena);
    fft(d_re, d_im, n / 2, D_re, D_im, arena);
    
    // complex_mul(complex_polar())
    for (s32 k = 0; k < n / 2; ++k)
    {
        f32 r = 1.0f;
        f32 rads = -2.0f * CF_PI * k / n;
        
        f32 p_re = r * CF_COSF(rads);
        f32 p_im = r * CF_SINF(rads);
        
        f32 m_re = p_re * D_re[k] - p_im * D_im[k];
        f32 m_im = p_re * D_re[k] + p_im * D_im[k];
        
        D_re[k] = m_re;
        D_im[k] = m_im;
    }
    
    for (s32 k = 0; k < n / 2; ++k)
    {
        // complex_add()
        f32 a_re = E_re[k] + D_re[k];
        f32 a_im = E_im[k] + D_im[k];
        
        // complex_sub()
        f32 s_re = E_re[k] - D_re[k];
        f32 s_im = E_im[k] - D_im[k];
        
        out_re[k] = a_re;
        out_im[k] = a_im;
        
        out_re[k + n / 2] = s_re;
        out_im[k + n / 2] = s_im;
    }
}

// this is not a good beat detector, it's enough to randomly generate levels based off of a possible beat
void process_audio(AudioInfo *info)
{
    cs_audio_source_t* src = (cs_audio_source_t*)info->src.id;
    CF_Arena* arena = info->arena;
    info->processing_fft_index = 0;
    
    info->min[0] = FLT_MAX;
    info->min[1] = FLT_MAX;
    info->max[0] = FLT_MIN;
    info->max[1] = FLT_MIN;
    
    f32* cA = (f32*)src->channels[0];
    f32* cB = (f32*)src->channels[0];
    
    s32 max_beats = src->sample_count / FFT_SIZE;
    if (src->sample_count % FFT_SIZE > 0)
    {
        max_beats++;
    }
    
    if (cf_array_count(info->beats))
    {
        cf_array_clear(info->beats);
    }
    
    cf_array_fit(info->beats, max_beats);
    dyna AudioBeat* beats = info->beats;
    
    info->sample_count = src->sample_count;
    
    cf_array_clear(info->avgs[0]);
    cf_array_clear(info->avgs[1]);
    
    cf_array_fit(info->avgs[0], 1 << 10);
    cf_array_fit(info->avgs[1], 1 << 10);
    
    s32 sample_index = 0;
    if (src->channel_count == 2)
    {
        f32 *cA = (f32*)src->channels[0];
        f32 *cB = (f32*)src->channels[1];
    }
    
    f32* cA_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    f32* cA_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    f32* cB_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    f32* cB_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    
    
    f32* inA_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    f32* inB_re = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    f32* inA_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    f32* inB_im = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    
    f32* frequencies = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    
    // double buffer the min/maxes to diff between each set of reads
    s32 mag_cur = 0;
    s32 mag_prev = 0;
    f32* magnitudesA[2];
    f32* magnitudesB[2];
    
    f32 beat_minimum_magnitude = info->min_magnitude_to_detect_beat;
    // 100 ms
    s32 beat_minimum_delta_time = (s32)(src->sample_rate * info->min_delay_between_beats / FFT_SIZE);
    
    magnitudesA[0] = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    magnitudesB[0] = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    magnitudesA[1] = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    magnitudesB[1] = (f32*)cf_arena_alloc(arena, sizeof(f32) * FFT_SIZE);
    
    f32 bin_multiplier = (f32)src->sample_rate / FFT_SIZE;
    for (s32 bin_index = 0; bin_index < FFT_SIZE; ++bin_index)
    {
        frequencies[bin_index] = bin_index * bin_multiplier;
    }
    
    Band bands[] = 
    {
        // sub-bass
        make_band(20.0f, 60.0f),
        // bass
        make_band(60.0f, 250.0f),
        // low mid range
        //make_band(250.0f, 500.0f),
    };
    
    while (sample_index < src->sample_count)
    {
        s32 remaining = src->sample_count - sample_index;
        s32 read_count = cf_min(remaining, FFT_SIZE);
        
        CF_MEMCPY(inA_re, cA + sample_index, sizeof(f32) * read_count);
        CF_MEMCPY(inB_re, cB + sample_index, sizeof(f32) * read_count);
        
        CF_MEMSET(inA_im, 0, sizeof(f32) * FFT_SIZE);
        CF_MEMSET(inB_im, 0, sizeof(f32) * FFT_SIZE);
        
        for (s32 index = 0; index < read_count; ++index)
        {
            inA_re[index] = cf_remap(inA_re[index], SAMPLE_MIN, SAMPLE_MAX, -1.0f, 1.0f);
            inB_re[index] = cf_remap(inB_re[index], SAMPLE_MIN, SAMPLE_MAX, -1.0f, 1.0f);
        }
        
        fft(inA_re, inA_im, read_count, cA_re, cA_im, arena);
        fft(inB_re, inB_im, read_count, cB_re, cB_im, arena);
        
        f32* magsA = magnitudesA[mag_cur];
        f32* magsB = magnitudesB[mag_cur];
        
        for (s32 index = 0; index < read_count; ++index)
        {
            magsA[index] = CF_SQRTF(cA_re[index] * cA_re[index] + cA_im[index] * cA_im[index]);
            magsB[index] = CF_SQRTF(cB_re[index] * cB_re[index] + cB_im[index] * cB_im[index]);
        }
        
        for (s32 band_index = 0; band_index < CF_ARRAY_SIZE(bands); ++band_index)
        {
            // rate / fft * index = Hz
            // index = Hz * fft / rate
            Band* band = bands + band_index;
            s32 frequency_start = (s32)(band->min * FFT_SIZE / src->sample_rate);
            s32 frequency_end = (s32)(band->max * FFT_SIZE / src->sample_rate) + 1;
            f32 avgsA = 0;
            f32 avgsB = 0;
            f32 mag_peakA = 0;
            f32 mag_peakB = 0;
            s32 mag_count = 0;
            
            for (s32 frequency_index = frequency_start; frequency_index < frequency_end; ++frequency_index)
            {
                if (frequencies[frequency_index] < band->min)
                {
                    continue;
                }
                if (frequencies[frequency_index] > band->max)
                {
                    continue;
                }
                
                avgsA += magsA[frequency_index];
                avgsB += magsB[frequency_index];
                mag_peakA = cf_max(mag_peakA, magsA[frequency_index]);
                mag_peakB = cf_max(mag_peakB, magsB[frequency_index]);
                
                ++mag_count;
            }
            
            avgsA /= mag_count;
            avgsB /= mag_count;
            
            band->avg_prev = band->avg_cur;
            band->peak_prev = band->peak_cur;
            f32 avg = cf_max(avgsA, avgsB);
            
            if (band->running_counter > 100)
            {
                band->avg_cur = band->avg_cur * ((band->running_counter - 1) / band->running_counter) +  avg;
            }
            else
            {
                band->avg_cur = band->avg_cur + avg;
                band->running_counter++;
            }
            band->peak_cur = cf_max(mag_peakA, mag_peakB);
        }
        
        // https://en.wikipedia.org/wiki/Beat_detection#:~:text=In%20signal%20analysis%2C%20beat%20detection,as%20some%20media%20player%20plugins.
        if (mag_prev != mag_cur)
        {
            for (s32 band_index = 0; band_index < CF_ARRAY_SIZE(bands); ++band_index)
            {
                Band* band = bands + band_index;
                f32 threshold = 2.0f;
                f32 beat_threshold_prev = band->avg_prev * threshold;
                f32 beat_threshold_cur = band->avg_cur * threshold;
                s32 fft_index = sample_index / FFT_SIZE;
                
                if (band->peak_cur > beat_minimum_magnitude)
                {
                    if (band->peak_prev > beat_threshold_prev && band->peak_cur > beat_threshold_cur)
                    {
                        if (AudioBeat* last_beat = &cf_array_last(beats))
                        {
                            if (last_beat->fft_index + beat_minimum_delta_time < fft_index)
                            {
                                AudioBeat beat = {};
                                beat.value = band->peak_cur;
                                beat.fft_index = fft_index;
                                beat.sample_index = sample_index + (s32)(band->min * FFT_SIZE / src->sample_rate);
                                cf_array_push(beats, beat);
                            }
                        }
                    }
                }
            }
        }
        
        mag_prev = mag_cur;
        mag_cur = (mag_cur + 1) % 2;
        
        f32 sumA = 0;
        f32 sumB = 0;
        for (s32 read_counter = 0; read_counter < read_count; ++read_counter)
        {
            s32 read_index = sample_index + read_counter;
            sumA += cA[read_index];
            sumB += cB[read_index];
        }
        
        cf_array_push(info->avgs[0], sumA / read_count);
        cf_array_push(info->avgs[1], sumB / read_count);
        
        info->max[0] = cf_max(info->max[0], cf_array_last(info->avgs[0]));
        info->max[1] = cf_max(info->max[1], cf_array_last(info->avgs[1]));
        
        info->min[0] = cf_min(info->min[0], cf_array_last(info->avgs[0]));
        info->min[1] = cf_min(info->min[1], cf_array_last(info->avgs[1]));
        
        sample_index += FFT_SIZE;
        info->processing_fft_index++;
    }
    
    u64 hashA = cf_fnv1a(src->channels[0], sizeof(f32) * src->sample_count);
    u64 hashB = cf_fnv1a(src->channels[1], sizeof(f32) * src->sample_count);
    
    info->hash = hashA ^ hashB;
    
    s32 beat_count = cf_array_count(beats);
    for (s32 index = 0; index < beat_count; ++index)
    {
        AudioBeat* beat = beats + index;
        printf("[%d][%d] %.2f\n", beat->sample_index, beat->fft_index, beat->value);
    }
    
    printf("[0] avg_count: %d\n", cf_array_count(info->avgs[0]));
    printf("[1] avg_count: %d\n", cf_array_count(info->avgs[1]));
    printf("beats: %d\n", beat_count);
}

f32 audio_processing_get_progress(AudioInfo* info)
{
    cs_audio_source_t* src = (cs_audio_source_t*)info->src.id;
    s32 fft_count = src->sample_count / FFT_SIZE;
    if (src->sample_count % FFT_SIZE)
    {
        fft_count++;
    }
    f32 result = (f32)info->processing_fft_index / fft_count;
    return result;
}