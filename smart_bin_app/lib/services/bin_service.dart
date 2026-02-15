import 'dart:convert';

import 'package:http/http.dart' as http;
import 'package:web_socket_channel/web_socket_channel.dart';

import '../models/bin_model.dart';
import '../utils/constants.dart';

class BinService {
  final String baseUrl;
  final String wsUrl;
  WebSocketChannel? _channel;

  BinService({
    this.baseUrl = AppConstants.defaultBackendUrl,
    this.wsUrl = 'ws://${AppConstants.defaultLocalIp}:81',
  });

  // HTTP API Methods
  Future<List<BinModel>> getAllBins() async {
    try {
      final response = await http.get(Uri.parse('$baseUrl/api/bins'));
      if (response.statusCode == 200) {
        final data = json.decode(response.body);
        final List<dynamic> bins = data['bins'];
        return bins.map((bin) => BinModel.fromJson(bin)).toList();
      }
      throw Exception('Failed to load bins');
    } catch (e) {
      throw Exception('Error: $e');
    }
  }

  Future<BinModel> getBinStatus(String binId) async {
    try {
      final response = await http.get(Uri.parse('$baseUrl/api/bins/$binId'));
      if (response.statusCode == 200) {
        return BinModel.fromJson(json.decode(response.body));
      }
      throw Exception('Failed to load bin status');
    } catch (e) {
      throw Exception('Error: $e');
    }
  }

  Future<Map<String, dynamic>> getStatistics() async {
    try {
      final response = await http.get(Uri.parse('$baseUrl/api/stats'));
      if (response.statusCode == 200) {
        return json.decode(response.body);
      }
      throw Exception('Failed to load statistics');
    } catch (e) {
      throw Exception('Error: $e');
    }
  }

  /// Get recent detections (for simulation mode to show last result).
  Future<List<Map<String, dynamic>>> getDetections({int limit = 1}) async {
    try {
      final response = await http.get(
        Uri.parse('$baseUrl/api/detections').replace(queryParameters: {'limit': limit.toString()}),
      );
      if (response.statusCode == 200) {
        final data = json.decode(response.body);
        final list = data['detections'] as List<dynamic>? ?? [];
        return list.cast<Map<String, dynamic>>();
      }
      return [];
    } catch (e) {
      return [];
    }
  }

  Future<bool> openBin(String binType, {String? esp32Ip}) async {
    try {
      if (esp32Ip != null) {
        // Local Direct Control
        final url = 'http://$esp32Ip/api/open';
        final response = await http.post(
          Uri.parse(url),
          headers: {'Content-Type': 'application/json'},
          body: json.encode({'bin': binType}),
        );
        return response.statusCode == 200;
      } else {
        // Remote Command Queue
        // Map type to ID (Hardcoded to match Firmware)
        final binId = binType == 'organic' ? '0x001' : '0x002';
        final url = '$baseUrl/api/bins/$binId/command';
        final response = await http.post(
          Uri.parse(url),
          headers: {'Content-Type': 'application/json'},
          body: json.encode({'command': 'OPEN', 'params': {}}),
        );
        return response.statusCode == 200;
      }
    } catch (e) {
      return false;
    }
  }

  Future<bool> closeBin(String binType, {String? esp32Ip}) async {
    try {
      if (esp32Ip != null) {
        // Local Direct Control
        final url = 'http://$esp32Ip/api/close';
        final response = await http.post(
          Uri.parse(url),
          headers: {'Content-Type': 'application/json'},
          body: json.encode({'bin': binType}),
        );
        return response.statusCode == 200;
      } else {
        // Remote Command Queue
        final binId = binType == 'organic' ? '0x001' : '0x002';
        final url = '$baseUrl/api/bins/$binId/command';
        final response = await http.post(
          Uri.parse(url),
          headers: {'Content-Type': 'application/json'},
          body: json.encode({'command': 'CLOSE', 'params': {}}),
        );
        return response.statusCode == 200;
      }
    } catch (e) {
      return false;
    }
  }

  Future<bool> resetBin(String binId) async {
    try {
      final response = await http.post(
        Uri.parse('$baseUrl/api/bins/$binId/reset'),
      );
      return response.statusCode == 200;
    } catch (e) {
      return false;
    }
  }

  Future<bool> toggleMaintenanceMode({String? esp32Ip}) async {
    try {
      final url = esp32Ip != null
          ? 'http://$esp32Ip/api/maintenance'
          : '$baseUrl/api/maintenance';
      final response = await http.post(Uri.parse(url));
      return response.statusCode == 200;
    } catch (e) {
      return false;
    }
  }

  // WebSocket Methods
  WebSocketChannel? connectWebSocket(String ip) {
    try {
      _channel = WebSocketChannel.connect(Uri.parse('ws://$ip:81'));
      return _channel;
    } catch (e) {
      return null;
    }
  }

  void disconnectWebSocket() {
    _channel?.sink.close();
    _channel = null;
  }

  void sendWebSocketMessage(String message) {
    _channel?.sink.add(message);
  }

  Stream<dynamic>? getWebSocketStream() {
    return _channel?.stream;
  }
}
