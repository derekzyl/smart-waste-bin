from fastapi import FastAPI, File, UploadFile, HTTPException, Depends
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel
from typing import Optional, List
from sqlalchemy.orm import Session
import uvicorn
import cv2
import numpy as np
from datetime import datetime
import os
from image_classifier import MaterialClassifier
from database import get_db, engine, Base
from models import Bin, DetectionLog, BinEvent

# Create tables
Base.metadata.create_all(bind=engine)

app = FastAPI(title="Smart Waste Bin Backend API", version="2.0.0")

# CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Initialize material classifier
model_path = os.getenv("MODEL_PATH", "models/material_classifier.pkl")
classifier = MaterialClassifier(model_path=model_path)

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

# Initialize default bins if they don't exist
@app.on_event("startup")
async def startup_event():
    """Create default bins on startup if they don't exist"""
    from database import SessionLocal
    db = SessionLocal()
    try:
        organic_bin = db.query(Bin).filter(Bin.id == "0x001").first()
        if not organic_bin:
            organic_bin = Bin(id="0x001", type="organic")
            db.add(organic_bin)
        
        non_organic_bin = db.query(Bin).filter(Bin.id == "0x002").first()
        if not non_organic_bin:
            non_organic_bin = Bin(id="0x002", type="non_organic")
            db.add(non_organic_bin)
        
        db.commit()
        print("Default bins initialized")
    except Exception as e:
        print(f"Error initializing bins: {e}")
        db.rollback()
    finally:
        db.close()

# ==================== API ENDPOINTS ====================

@app.get("/")
async def root():
    return {
        "message": "Smart Waste Bin Backend API",
        "version": "2.0.0",
        "database": "PostgreSQL",
        "endpoints": {
            "detect": "/api/detect (POST)",
            "bins": "/api/bins (GET)",
            "bin_status": "/api/bins/{bin_id} (GET)",
            "update_bins": "/api/bins/update (POST)",
            "events": "/api/events (GET)",
            "detections": "/api/detections (GET)"
        }
    }

@app.post("/api/detect")
async def detect_material(file: UploadFile = File(...), db: Session = Depends(get_db)):
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
        
        # Classify material using advanced image recognition
        result = classifier.classify(image)
        
        # Log detection to database
        detection_log = DetectionLog(
            material=result['material'],
            confidence=result['confidence'],
            method=result.get('method', 'unknown')
        )
        db.add(detection_log)
        db.commit()
        
        # Log detection
        method = result.get('method', 'unknown')
        print(f"[{datetime.now()}] Material detected: {result['material']} "
              f"(confidence: {result['confidence']:.2f}, method: {method})")
        
        return JSONResponse(content=result)
    
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Detection error: {str(e)}")

@app.post("/api/bins/update")
async def update_bins(data: BinUpdate, db: Session = Depends(get_db)):
    """
    Update bin status from ESP32.
    """
    try:
        # Update organic bin
        organic_bin = db.query(Bin).filter(Bin.id == data.bin_organic_id).first()
        if organic_bin:
            organic_bin.weight = data.organic_weight
            organic_bin.level = int((data.organic_weight / 10.0) * 100)
            organic_bin.full = data.organic_full
            organic_bin.last_update = datetime.now()
            
            # Log event if bin became full
            if data.organic_full and not organic_bin.full:
                event = BinEvent(bin_id=data.bin_organic_id, event_type="full")
                db.add(event)
        else:
            # Create new bin if it doesn't exist
            organic_bin = Bin(
                id=data.bin_organic_id,
                type="organic",
                weight=data.organic_weight,
                level=int((data.organic_weight / 10.0) * 100),
                full=data.organic_full
            )
            db.add(organic_bin)
        
        # Update non-organic bin
        non_organic_bin = db.query(Bin).filter(Bin.id == data.bin_non_organic_id).first()
        if non_organic_bin:
            non_organic_bin.weight = data.non_organic_weight
            non_organic_bin.level = int((data.non_organic_weight / 10.0) * 100)
            non_organic_bin.full = data.non_organic_full
            non_organic_bin.last_update = datetime.now()
            
            # Log event if bin became full
            if data.non_organic_full and not non_organic_bin.full:
                event = BinEvent(bin_id=data.bin_non_organic_id, event_type="full")
                db.add(event)
        else:
            # Create new bin if it doesn't exist
            non_organic_bin = Bin(
                id=data.bin_non_organic_id,
                type="non_organic",
                weight=data.non_organic_weight,
                level=int((data.non_organic_weight / 10.0) * 100),
                full=data.non_organic_full
            )
            db.add(non_organic_bin)
        
        db.commit()
        
        return {
            "status": "success",
            "message": "Bins updated successfully"
        }
    
    except Exception as e:
        db.rollback()
        raise HTTPException(status_code=500, detail=f"Update error: {str(e)}")

@app.get("/api/bins")
async def get_all_bins(db: Session = Depends(get_db)):
    """
    Get status of all bins.
    """
    bins = db.query(Bin).all()
    return {
        "bins": [bin.to_dict() for bin in bins],
        "timestamp": datetime.now().isoformat()
    }

@app.get("/api/bins/{bin_id}")
async def get_bin_status(bin_id: str, db: Session = Depends(get_db)):
    """
    Get status of a specific bin.
    """
    bin = db.query(Bin).filter(Bin.id == bin_id).first()
    if not bin:
        raise HTTPException(status_code=404, detail="Bin not found")
    
    return bin.to_dict()

@app.get("/api/stats")
async def get_statistics(db: Session = Depends(get_db)):
    """
    Get overall statistics.
    """
    bins = db.query(Bin).all()
    total_weight = sum(bin.weight for bin in bins)
    full_bins = sum(1 for bin in bins if bin.full)
    avg_level = sum(bin.level for bin in bins) / len(bins) if bins else 0
    
    # Get recent detections count
    recent_detections = db.query(DetectionLog).count()
    
    return {
        "total_bins": len(bins),
        "full_bins": full_bins,
        "total_weight": total_weight,
        "average_level": round(avg_level, 2),
        "total_detections": recent_detections,
        "timestamp": datetime.now().isoformat()
    }

@app.post("/api/bins/{bin_id}/reset")
async def reset_bin(bin_id: str, db: Session = Depends(get_db)):
    """
    Reset bin (for maintenance).
    """
    bin = db.query(Bin).filter(Bin.id == bin_id).first()
    if not bin:
        raise HTTPException(status_code=404, detail="Bin not found")
    
    bin.weight = 0.0
    bin.level = 0
    bin.full = False
    bin.last_update = datetime.now()
    
    # Log reset event
    event = BinEvent(bin_id=bin_id, event_type="reset")
    db.add(event)
    db.commit()
    
    return {
        "status": "success",
        "message": f"Bin {bin_id} reset successfully"
    }

@app.get("/api/events")
async def get_events(limit: int = 50, db: Session = Depends(get_db)):
    """
    Get recent bin events.
    """
    events = db.query(BinEvent).order_by(BinEvent.timestamp.desc()).limit(limit).all()
    return {
        "events": [event.to_dict() for event in events],
        "count": len(events)
    }

@app.get("/api/detections")
async def get_detections(limit: int = 50, db: Session = Depends(get_db)):
    """
    Get recent material detections.
    """
    detections = db.query(DetectionLog).order_by(DetectionLog.timestamp.desc()).limit(limit).all()
    return {
        "detections": [detection.to_dict() for detection in detections],
        "count": len(detections)
    }

@app.post("/api/bins/{bin_id}/event")
async def log_bin_event(bin_id: str, event_type: str, db: Session = Depends(get_db)):
    """
    Log a bin event (open, close, etc.).
    """
    event = BinEvent(bin_id=bin_id, event_type=event_type)
    db.add(event)
    db.commit()
    
    return {
        "status": "success",
        "event": event.to_dict()
    }

if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
