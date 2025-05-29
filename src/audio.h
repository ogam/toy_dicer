#ifndef AUDIO_H
#define AUDIO_H

struct AudioBeat
{
    f32 value;
    s32 sample_index;
    s32 fft_index;
};

struct Band
{
    f32 min;
    f32 max;
    f32 avg_prev;
    f32 avg_cur;
    f32 peak_prev;
    f32 peak_cur;
    
    s32 running_counter;
};

struct AudioInfo
{
    CF_Audio src;
    f32 max[2];
    f32 min[2];
    
    f32 threshold;
    
    dyna AudioBeat* beats;
    
    // avgs looks to be what dps software uses to display current amps..?
    dyna f32* avgs[2];
    
    s32 sample_count;
    s32 processing_fft_index;
    b32 is_processing;
    u64 hash;
    
    f32 min_delay_between_beats;
    f32 min_magnitude_to_detect_beat;
    
    CF_Arena* arena;
};

void process_audio(AudioInfo* info);
f32 audio_processing_get_progress(AudioInfo* info);

#endif