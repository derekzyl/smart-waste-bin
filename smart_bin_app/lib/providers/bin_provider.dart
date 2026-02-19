import 'dart:async';

import 'package:flutter/foundation.dart';
import 'dart:convert';

import '../models/bin_model.dart';
import '../services/bin_service.dart';

/// Detection flow status for UI (processing → result → opening).
enum DetectionStatus {
  idle,
  analyzing,
  opening,
  closing,
}

class BinProvider with ChangeNotifier {
  final BinService _binService = BinService();
  
  List<BinModel> _bins = [];
  Map<String, dynamic> _stats = {};
  bool _isLoading = false;
  String? _error;
  String? _esp32Ip;

  DetectionStatus _detectionStatus = DetectionStatus.idle;
  String _lastDetectionMaterial = '';
  double _lastDetectionConfidence = 0.0;
  Timer? _detectionResetTimer;
  Timer? _simulationDetectionPollTimer;

  List<BinModel> get bins => _bins;
  Map<String, dynamic> get stats => _stats;
  bool get isLoading => _isLoading;
  String? get error => _error;
  String? get esp32Ip => _esp32Ip;
  DetectionStatus get detectionStatus => _detectionStatus;
  String get lastDetectionMaterial => _lastDetectionMaterial;
  double get lastDetectionConfidence => _lastDetectionConfidence;

  bool get isLiveDevice => _esp32Ip != null && _esp32Ip!.isNotEmpty;

  void setEsp32Ip(String? ip) {
    _esp32Ip = ip;
    notifyListeners();
  }

  Future<void> loadBins() async {
    _isLoading = true;
    _error = null;
    notifyListeners();

    try {
      _bins = await _binService.getAllBins();
      _stats = await _binService.getStatistics();
      _error = null;
    } catch (e) {
      _error = e.toString();
    } finally {
      _isLoading = false;
      notifyListeners();
    }
  }

  Future<bool> openBin(String binType) async {
    try {
      final result = await _binService.openBin(binType, esp32Ip: _esp32Ip);
      if (result) {
        if (!isLiveDevice) setSimulationOpening(binType);
        await loadBins();
      }
      return result;
    } catch (e) {
      _error = e.toString();
      notifyListeners();
      return false;
    }
  }

  Future<bool> closeBin(String binType) async {
    try {
      final result = await _binService.closeBin(binType, esp32Ip: _esp32Ip);
      if (result) {
        await loadBins();
      }
      return result;
    } catch (e) {
      _error = e.toString();
      notifyListeners();
      return false;
    }
  }

  Future<bool> resetBin(String binId) async {
    try {
      final result = await _binService.resetBin(binId);
      if (result) {
        await loadBins();
      }
      return result;
    } catch (e) {
      _error = e.toString();
      notifyListeners();
      return false;
    }
  }

  Future<bool> toggleMaintenanceMode() async {
    try {
      return await _binService.toggleMaintenanceMode(esp32Ip: _esp32Ip);
    } catch (e) {
      _error = e.toString();
      notifyListeners();
      return false;
    }
  }

  void _setDetectionState(DetectionStatus status, {String material = '', double confidence = 0.0}) {
    _detectionStatus = status;
    _lastDetectionMaterial = material;
    _lastDetectionConfidence = confidence;
    _detectionResetTimer?.cancel();
    if (status == DetectionStatus.opening || status == DetectionStatus.closing) {
      _detectionResetTimer = Timer(const Duration(seconds: 4), () {
        _detectionStatus = DetectionStatus.idle;
        _lastDetectionMaterial = '';
        _lastDetectionConfidence = 0.0;
        notifyListeners();
      });
    }
    notifyListeners();
  }

  void _onWebSocketMessage(dynamic message) {
    if (message is! String) return;
    try {
      final map = json.decode(message) as Map<String, dynamic>?;
      if (map == null) return;
      final type = map['type'] as String?;
      if (type != 'DETECTION_EVENT') {
        loadBins();
        return;
      }
      final action = map['action'] as String? ?? '';
      final material = map['material']?.toString() ?? '';
      final confidence = (map['confidence'] is num) ? (map['confidence'] as num).toDouble() : 0.0;

      if (action == 'ANALYZING') {
        _setDetectionState(DetectionStatus.analyzing);
      } else if (action == 'OPENING') {
        _setDetectionState(DetectionStatus.opening, material: material, confidence: confidence);
      } else if (action == 'CLOSING') {
        _setDetectionState(DetectionStatus.closing, material: material, confidence: confidence);
      }
      loadBins();
    } catch (_) {
      loadBins();
    }
  }

  void startWebSocketConnection() {
    if (_esp32Ip != null) {
      final channel = _binService.connectWebSocket(_esp32Ip!);
      channel?.stream.listen((message) {
        _onWebSocketMessage(message);
      });
    }
  }

  void stopWebSocketConnection() {
    _binService.disconnectWebSocket();
    _detectionResetTimer?.cancel();
  }

  /// When using backend only (simulation), poll latest detection and show in UI.
  void startSimulationDetectionPoll() {
    _simulationDetectionPollTimer?.cancel();
    if (_esp32Ip != null && _esp32Ip!.isNotEmpty) return;
    void poll() async {
      final list = await _binService.getDetections(limit: 1);
      if (list.isNotEmpty && _detectionStatus == DetectionStatus.idle) {
        final d = list.first;
        final material = d['material']?.toString() ?? '';
        final confidence = (d['confidence'] is num) ? (d['confidence'] as num).toDouble() : 0.0;
        if (material.isNotEmpty && material != 'ERROR') {
          _lastDetectionMaterial = material;
          _lastDetectionConfidence = confidence;
          notifyListeners();
        }
      }
    }
    poll();
    _simulationDetectionPollTimer = Timer.periodic(const Duration(seconds: 8), (_) => poll());
  }

  void stopSimulationDetectionPoll() {
    _simulationDetectionPollTimer?.cancel();
    _simulationDetectionPollTimer = null;
  }

  /// Call when user taps Open in simulation so UI shows "Opening...".
  void setSimulationOpening(String binType) {
    final material = binType == 'organic' ? 'ORGANIC' : 'NON_ORGANIC';
    _setDetectionState(DetectionStatus.opening, material: material, confidence: 0.95);
  }
}

