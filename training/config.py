"""Shared settings. The firmware uses the SAME values (they are exported to
model_config.h), so if you change anything here you must retrain and
re-import the model into STM32Cube.AI."""

SAMPLE_RATE = 16000      # Hz, the PDM mic is decimated to this by DFSDM
N_FFT = 1024             # 64 ms analysis frame
HOP = 512                # 32 ms hop (one DMA half-buffer on the MCU)
N_MELS = 40              # mel bands
N_FRAMES = 30            # frames per model input  -> ~1 s of audio
CLIP_LEN = N_FFT + (N_FRAMES - 1) * HOP   # 15872 samples = 0.99 s
FMIN = 50.0
FMAX = 8000.0
LOG_EPS = 1e-6

# "background" is mandatory: it is everything you do NOT want to alert on
# (speech, TV, music, traffic, typing, dishes, silence ...).
BACKGROUND = "background"
