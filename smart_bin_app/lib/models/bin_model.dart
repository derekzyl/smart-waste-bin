class BinModel {
  final String id;
  final String type;
  final double weight;
  final int level;
  final bool full;
  final DateTime? lastUpdate;

  BinModel({
    required this.id,
    required this.type,
    required this.weight,
    required this.level,
    required this.full,
    this.lastUpdate,
  });

  factory BinModel.fromJson(Map<String, dynamic> json) {
    return BinModel(
      id: json['id'] ?? '',
      type: json['type'] ?? '',
      weight: (json['weight'] ?? 0.0).toDouble(),
      level: json['level'] ?? 0,
      full: json['full'] ?? false,
      lastUpdate: json['last_update'] != null
          ? DateTime.tryParse(json['last_update'])
          : null,
    );
  }

  Map<String, dynamic> toJson() {
    return {
      'id': id,
      'type': type,
      'weight': weight,
      'level': level,
      'full': full,
      'last_update': lastUpdate?.toIso8601String(),
    };
  }
}

