# SPIFFS Assets Builder

This script builds the SPIFFS assets partition for ESP32 projects, packaging various resource files into a format usable on the device.

## Features

- Process WakeNet models
- Integrate text font files
- Process emoji image collections
- Automatically generate resource index files
- Package the final `assets.bin` file

## Requirements

- Python 3.6+
- Related resource files

## Usage

### Basic Syntax

```bash
./build.py --wakenet_model <wakenet_model_dir> \
    --text_font <text_font_file> \
    --emoji_collection <emoji_collection_dir>
```

### Parameters

| Parameter | Type | Required | Description |
|------|------|------|------|
| `--wakenet_model` | Directory path | No | WakeNet model directory path |
| `--text_font` | File path | No | Text font file path |
| `--emoji_collection` | Directory path | No | Emoji image collection directory path |

### Examples

```bash
# Full parameter example
./build.py \
    --wakenet_model ../../managed_components/espressif__esp-sr/model/wakenet_model/wn9_nihaoxiaozhi_tts \
    --text_font ../../components/xiaozhi-fonts/build/font_puhui_common_20_4.bin \
    --emoji_collection ../../components/xiaozhi-fonts/build/emojis_64/

# Font file only
./build.py --text_font ../../components/xiaozhi-fonts/build/font_puhui_common_20_4.bin

# Emoji collection only
./build.py --emoji_collection ../../components/xiaozhi-fonts/build/emojis_64/
```

## Workflow

1. **Create build directory structure**
   - `build/` - Main build directory
   - `build/assets/` - Resource files directory
   - `build/output/` - Output files directory

2. **Process WakeNet model**
   - Copy model files to build directory
   - Use `pack_model.py` to generate `srmodels.bin`
   - Copy generated model file to assets directory

3. **Process text font**
   - Copy font file to assets directory
   - Supports `.bin` format font files

4. **Process emoji collection**
   - Scan image files in the specified directory
   - Supports `.png` and `.gif` formats
   - Automatically generate emoji index

5. **Generate configuration files**
   - `index.json` - Resource index file
   - `config.json` - Build configuration file

6. **Package final assets**
   - Use `spiffs_assets_gen.py` to generate `assets.bin`
   - Copy to build root directory

## Output Files

After the build completes, the following files are generated under `build/`:

- `assets/` - All resource files
- `assets.bin` - Final SPIFFS assets file
- `config.json` - Build configuration
- `output/` - Intermediate output files

## Supported Resource Formats

- **Model files**: `.bin` (processed via pack_model.py)
- **Font files**: `.bin`
- **Image files**: `.png`, `.gif`
- **Config files**: `.json`

## Error Handling

The script includes robust error handling:

- Checks that source files/directories exist
- Validates subprocess execution results
- Provides detailed error messages and warnings

## Notes

1. Ensure all dependent Python scripts are in the same directory
2. Use absolute paths or paths relative to the script directory for resource files
3. The build process cleans previous build files
4. Generated `assets.bin` file size is limited by SPIFFS partition size
