HDCD decoder code, originally in foo_hdcd, then developed further as part of FFmpeg, then made into this generic form.

Features
--------

* Basic HDCD decoding
* Optional HDCD detection code
* Optional Analyze mode
* Optional logging callback interface

Simplest use
------------

For any number of channels, process one at a time.

#### Declare
    hdcd_state_t state[MAX_CHANNELS];

#### Initialize
    /* foreach(channel) */
        hdcd_reset(&state[channel], sample_rate);

#### Each frame
    /* foreach(channel) */
        hdcd_process(&state[channel], *samples, count, nb_channels);


Simplest use (stereo functions)
-------------------------------

When there are exactly two stereo channels, target_gain matching is enabled.

#### Declare
    hdcd_state_stereo_t state_stereo;

#### Initialize
    hdcd_reset_stereo(&state_stereo, sample_rate);

#### Each frame
    hdcd_process_stereo(&state_stereo, *samples, count);


HDCD detection functions
------------------------

### Declare
    hdcd_detection_t detect;

### Initialize
    hdcd_detect_reset(&detect);

### Each frame (n-channel)
    hdcd_detect_start(&detect);
    /* foreach(channel) */
        hdcd_process(&state[channel], *samples, count, nb_channels);
        hdcd_detect_onech(&state[channel], &detect);
    hdcd_detect_end(&detect, nb_channels);

### Each frame (stereo)
    hdcd_process_stereo(&state_stereo, *samples, count);
    hdcd_detect_stereo(&state_stereo, detect);

Analyze mode
============

A mode to aid in analysis of HDCD encoded audio. In this mode the audio is replaced by a solid tone and the amplitude is adjusted to signal some specified aspect of the process. The output can be loaded in an audio editor alongside the original, where the user can see where different features or states are present.
See hdcd_ana_mode_t in hdcd_decoder2.h.

    set_analyze_mode(MODE);

    set_analyze_mode_stereo(MODE);

