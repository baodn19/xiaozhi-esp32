# LVGL Image Converter

This directory contains two Python scripts for processing and converting images to LVGL format:

## 1. LVGLImage (LVGLImage.py)

Adapted from the LVGL [official repo](https://github.com/lvgl/lvgl) conversion script [LVGLImage.py](https://github.com/lvgl/lvgl/blob/master/scripts/LVGLImage.py).

## 2. LVGL Image Converter (lvgl_tools_gui.py)

Calls `LVGLImage.py` to batch convert images to LVGL image format.
Can be used to modify Xiaozhi's default emojis; see the tutorial [here](https://www.bilibili.com/video/BV12FQkYeEJ3/).

### Features

- Graphical interface for easier operation
- Batch image conversion support
- Auto-detect image format and choose optimal color format conversion
- Multiple resolution support

### Usage

Create virtual environment
```bash
# Create venv
python -m venv venv
# Activate environment
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate      # Windows
```

Install dependencies
```bash
pip install -r requirements.txt
```

Run conversion tool

```bash
# Activate environment
source venv/bin/activate  # Linux/Mac
venv\Scripts\activate      # Windows
# Run
python lvgl_tools_gui.py
```
