# Smart Waste Bin Backend API

FastAPI backend for material detection and bin management.

## Installation

```bash
pip install -r requirements.txt
```

## Running

```bash
python main.py
```

Or with uvicorn:

```bash
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

## API Endpoints

- `GET /` - API information
- `POST /api/detect` - Detect material type from image (JPEG)
- `POST /api/bins/update` - Update bin status from ESP32
- `GET /api/bins` - Get all bins status
- `GET /api/bins/{bin_id}` - Get specific bin status
- `GET /api/stats` - Get overall statistics
- `POST /api/bins/{bin_id}/reset` - Reset bin (maintenance)

## Notes

- The material detection uses a simple color-based classifier. For production, replace with a trained ML model (e.g., TensorFlow, PyTorch).
- Current storage is in-memory. For production, use a database (PostgreSQL, MongoDB, etc.).

