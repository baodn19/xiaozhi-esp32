# P3 Audio Format Conversion and Playback Tools

This directory contains Python scripts for handling P3 format audio files:

## 1. Audio Conversion Tool (convert_audio_to_p3.py)

Converts regular audio files to P3 format (4-byte header + Opus packet stream structure) with loudness normalization.

### Usage

```bash
python convert_audio_to_p3.py <input_audio_file> <output_p3_file> [-l LUFS] [-d]
```

Optional `-l` specifies the target loudness for normalization (default: -16 LUFS). Optional `-d` disables loudness normalization.

If the input audio meets any of the following conditions, consider using `-d` to disable loudness normalization:
- Audio is too short
- Audio has already been loudness-adjusted
- Audio is from default TTS (Xiaozhi's current TTS default loudness is already -16 LUFS)

Example:
```bash
python convert_audio_to_p3.py input.mp3 output.p3
```

## 2. P3 Audio Playback Tool (play_p3.py)

Plays P3 format audio files.

### Features

- Decode and play P3 format audio files
- Apply fade-out on playback end or user interrupt to avoid clicks
- Support specifying the file to play via command-line arguments

### Usage

```bash
python play_p3.py <p3_file_path>
```

Example:
```bash
python play_p3.py output.p3
```

## 3. Audio Conversion Back Tool (convert_p3_to_audio.py)

Converts P3 format back to regular audio files.

### Usage

```bash
python convert_p3_to_audio.py <input_p3_file> <output_audio_file>
```

The output audio file must have a file extension.

Example:
```bash
python convert_p3_to_audio.py input.p3 output.wav
```
## 4. Audio/P3 Batch Conversion Tool

A graphical tool supporting batch conversion from audio to P3 and P3 to audio.

![](./img/img.png)

### Usage:
```bash
python batch_convert_gui.py
```

## Dependency Installation

Before using these scripts, ensure the required Python libraries are installed:

```bash
pip install librosa opuslib numpy tqdm sounddevice pyloudnorm soundfile
```

Or use the provided requirements.txt file:

```bash
pip install -r requirements.txt
```

## P3 Format Description

P3 is a simple streaming audio format with the following structure:
- Each audio frame consists of a 4-byte header and an Opus-encoded data packet
- Header format: [1 byte type, 1 byte reserved, 2 byte length]
- Sample rate fixed at 16000Hz, mono
- Each frame duration is 60ms
