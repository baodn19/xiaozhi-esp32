# ogg_converter Xiaozhi AI OGG Sound Batch Converter

This script is an OGG batch conversion tool that converts input audio files to OGG format usable by Xiaozhi.

Implemented using the Python third-party library `ffmpeg-python`; requires an `ffmpeg` environment.

Download the ffmpeg distribution for your system [here](https://ffmpeg.org/download.html) and add it to your PATH or place it in the script directory.

Supports conversion between OGG and audio formats, loudness adjustment, and more.

# Create and Activate Virtual Environment

```bash
# Create virtual environment
python -m venv venv
# Activate virtual environment
source venv/bin/activate # Mac/Linux
venv\Scripts\activate # Windows
```
# Download FFmpeg
Download ffmpeg [here](https://ffmpeg.org/download.html).

Download the version for your system and place the `ffmpeg` executable in the script directory or add its directory to your PATH.

# Install Dependencies
Run inside the virtual environment:

```bash
pip install ffmpeg-python
```

# Run Script
```bash
python ogg_covertor.py
```
