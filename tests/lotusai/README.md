# LotusAI tests

Host-side C++ unit tests and Python integration tests for `LotusAiController`.

## C++ unit tests (no hardware)

```bash
cd tests/lotusai
mkdir -p build && cd build
cmake ..
cmake --build .
ctest --output-on-failure
```

## Python integration tests (live API)

```bash
cd tests/lotusai
pip install -r requirements.txt
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 pytest -p pytest --run-integration -v
```

Optional: point at local backend:

```bash
LOTUSAI_BASE_URL=http://192.168.x.x:8000 pytest --run-integration -v
```

## Refresh golden fixtures

```bash
python capture_web_reference.py --base-url https://lotusfoodasmedicine.com
```

## Manual device checklist

See [MANUAL_CHECKLIST.md](MANUAL_CHECKLIST.md).

## Setup local LotusAI
### ESP32-S3 side (Client)
1. Source esp-idf
2. Navigate to the `xiaozhi-esp32` folder
3. Run `idf.py -p /dev/ttyACM0 monitor`

### LotusAI side (Server)
1. Navigate to `food_as_medicine` folder
2. Source venv
3. Run `<your venv>/bin/uvicorn backend.app.main:app --host 0.0.0.0 --port <8000 or any other free port>`