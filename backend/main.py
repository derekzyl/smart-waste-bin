from fastapi import FastAPI, File, UploadFile, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel
from typing import Optional
import uvicorn
import cv2
import numpy as np
from PIL import Image
import io
import base64
from datetime import datetime
import json
import os

app = FastAPI(title="Smart Waste Bin Backend API")

# CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# In-memory storage (replace with database in production)
bins_db = {
    "0x001": {
        "id": "0x001",
        "type": "organic",
        "weight": 0.0,
        "level": 0,
        "full": False,
        "last_update": None
    },
    "0x002": {
        "id": "0x002",
        "type": "non_organic",
        "weight": 0.0,
        "level": 0,
        "full": False,
        "last_update": None
    }
}

# Simple material classification model (mock - replace with actual ML model)
class MaterialClassifier:
    def __init__(self):
        # In production, load a trained model here
        # For now, using simple color-based detection
        pass
    
    def classify(self, image: np.ndarray) -> dict:
        """
        Classify material type from image.
        Returns: {"material": "ORGANIC" or "NON_ORGANIC", "confidence": float}
        """
        # Convert to HSV for better color analysis
        hsv = cv2.cvtColor(image, cv2.COLOR_BGR2HSV)
        
        # Simple heuristic: analyze dominant colors
        # Green/brown tones -> organic
        # Blue/white/metallic tones -> non-organic
        
        # Calculate average hue
        avg_hue = np.mean(hsv[:, :, 0])
        avg_sat = np.mean(hsv[:, :, 1])
        avg_val = np.mean(hsv[:, :, 2])
        
        # Simple classification logic
        # Organic: green/brown (hue 30-90), higher saturation
        # Non-organic: blue/white (hue 90-150 or low saturation)
        
        if 30 <= avg_hue <= 90 and avg_sat > 80:
            material = "ORGANIC"
            confidence = 0.75
        elif avg_hue < 30 or avg_hue > 150 or avg_sat < 50:
            material = "NON_ORGANIC"
            confidence = 0.70
        else:
            # Default classification
            material = "ORGANIC"
            confidence = 0.60
        
        return {
            "material": material,
            "confidence": confidence,
            "hue": float(avg_hue),
            "saturation": float(avg_sat),
            "brightness": float(avg_val)
        }

classifier = MaterialClassifier()

# Request/Response Models
class BinUpdate(BaseModel):
    bin_organic_id: str
    bin_non_organic_id: str
    organic_weight: float
    non_organic_weight: float
    organic_full: bool
    non_organic_full: bool
    timestamp: Optional[int] = None

class BinStatus(BaseModel):
    id: str
    type: str
    weight: float
    level: int
    full: bool
    last_update: Optional[str] = None

# ==================== API ENDPOINTS ====================

@app.get("/")
async def root():
    return {
        "message": "Smart Waste Bin Backend API",
        "version": "1.0.0",
        "endpoints": {
            "detect": "/api/detect (POST)",
            "bins": "/api/bins (GET)",
            "bin_status": "/api/bins/{bin_id} (GET)",
            "update_bins": "/api/bins/update (POST)"
        }
    }

@app.post("/api/detect")
async def detect_material(file: UploadFile = File(...)):
    """
    Detect material type from image.
    Accepts JPEG image and returns classification result.
    """
    try:
        # Read image file
        contents = await file.read()
        
        # Convert to numpy array
        nparr = np.frombuffer(contents, np.uint8)
        image = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
        
        if image is None:
            raise HTTPException(status_code=400, detail="Invalid image format")
        
        # Classify material
        result = classifier.classify(image)
        
        # Log detection
        print(f"[{datetime.now()}] Material detected: {result['material']} (confidence: {result['confidence']:.2f})")
        
        return JSONResponse(content=result)
    
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Detection error: {str(e)}")

@app.post("/api/bins/update")
async def update_bins(data: BinUpdate):
    """
    Update bin status from ESP32.
    """
    try:
        # Update organic bin
        if data.bin_organic_id in bins_db:
            bins_db[data.bin_organic_id]["weight"] = data.organic_weight
            bins_db[data.bin_organic_id]["level"] = int((data.organic_weight / 10.0) * 100)
            bins_db[data.bin_organic_id]["full"] = data.organic_full
            bins_db[data.bin_organic_id]["last_update"] = datetime.now().isoformat()
        
        # Update non-organic bin
        if data.bin_non_organic_id in bins_db:
            bins_db[data.bin_non_organic_id]["weight"] = data.non_organic_weight
            bins_db[data.bin_non_organic_id]["level"] = int((data.non_organic_weight / 10.0) * 100)
            bins_db[data.bin_non_organic_id]["full"] = data.non_organic_full
            bins_db[data.bin_non_organic_id]["last_update"] = datetime.now().isoformat()
        
        return {
            "status": "success",
            "message": "Bins updated successfully"
        }
    
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Update error: {str(e)}")

@app.get("/api/bins")
async def get_all_bins():
    """
    Get status of all bins.
    """
    return {
        "bins": list(bins_db.values()),
        "timestamp": datetime.now().isoformat()
    }

@app.get("/api/bins/{bin_id}")
async def get_bin_status(bin_id: str):
    """
    Get status of a specific bin.
    """
    if bin_id not in bins_db:
        raise HTTPException(status_code=404, detail="Bin not found")
    
    return bins_db[bin_id]

@app.get("/api/stats")
async def get_statistics():
    """
    Get overall statistics.
    """
    total_weight = sum(bin["weight"] for bin in bins_db.values())
    full_bins = sum(1 for bin in bins_db.values() if bin["full"])
    avg_level = sum(bin["level"] for bin in bins_db.values()) / len(bins_db)
    
    return {
        "total_bins": len(bins_db),
        "full_bins": full_bins,
        "total_weight": total_weight,
        "average_level": round(avg_level, 2),
        "timestamp": datetime.now().isoformat()
    }

@app.post("/api/bins/{bin_id}/reset")
async def reset_bin(bin_id: str):
    """
    Reset bin (for maintenance).
    """
    if bin_id not in bins_db:
        raise HTTPException(status_code=404, detail="Bin not found")
    
    bins_db[bin_id]["weight"] = 0.0
    bins_db[bin_id]["level"] = 0
    bins_db[bin_id]["full"] = False
    bins_db[bin_id]["last_update"] = datetime.now().isoformat()
    
    return {
        "status": "success",
        "message": f"Bin {bin_id} reset successfully"
    }

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)

