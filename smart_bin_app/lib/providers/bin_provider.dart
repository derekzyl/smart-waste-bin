import 'package:flutter/foundation.dart';
import '../models/bin_model.dart';
import '../services/bin_service.dart';

class BinProvider with ChangeNotifier {
  final BinService _binService = BinService();
  
  List<BinModel> _bins = [];
  Map<String, dynamic> _stats = {};
  bool _isLoading = false;
  String? _error;
  String? _esp32Ip;

  List<BinModel> get bins => _bins;
  Map<String, dynamic> get stats => _stats;
  bool get isLoading => _isLoading;
  String? get error => _error;
  String? get esp32Ip => _esp32Ip;

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

  void startWebSocketConnection() {
    if (_esp32Ip != null) {
      final channel = _binService.connectWebSocket(_esp32Ip!);
      channel?.stream.listen((message) {
        // Handle WebSocket messages
        loadBins();
      });
    }
  }

  void stopWebSocketConnection() {
    _binService.disconnectWebSocket();
  }
}

