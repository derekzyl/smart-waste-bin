import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/bin_model.dart';
import '../providers/bin_provider.dart';

class BinCard extends StatelessWidget {
  final BinModel bin;

  const BinCard({super.key, required this.bin});

  Color getBinColor() {
    if (bin.full) return Colors.red;
    if (bin.level > 75) return Colors.orange;
    if (bin.level > 50) return Colors.yellow;
    return Colors.green;
  }

  IconData getBinIcon() {
    return bin.type == 'organic' ? Icons.restaurant : Icons.recycling;
  }

  @override
  Widget build(BuildContext context) {
    final provider = context.read<BinProvider>();
    
    return Card(
      elevation: 4,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(
                  getBinIcon(),
                  size: 32,
                  color: getBinColor(),
                ),
                const SizedBox(width: 12),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        bin.type.toUpperCase(),
                        style: const TextStyle(
                          fontSize: 18,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      Text(
                        'ID: ${bin.id}',
                        style: TextStyle(
                          fontSize: 12,
                          color: Colors.grey.shade600,
                        ),
                      ),
                    ],
                  ),
                ),
                Chip(
                  label: Text(
                    bin.full ? 'FULL' : '${bin.level}%',
                    style: const TextStyle(
                      color: Colors.white,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                  backgroundColor: getBinColor(),
                ),
              ],
            ),
            const SizedBox(height: 16),
            
            // Progress Bar
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text(
                      'Level: ${bin.level}%',
                      style: const TextStyle(fontWeight: FontWeight.w500),
                    ),
                    Text(
                      'Weight: ${bin.weight.toStringAsFixed(2)} kg',
                      style: const TextStyle(fontWeight: FontWeight.w500),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                LinearProgressIndicator(
                  value: bin.level / 100,
                  backgroundColor: Colors.grey.shade300,
                  valueColor: AlwaysStoppedAnimation<Color>(getBinColor()),
                  minHeight: 8,
                ),
              ],
            ),
            
            if (bin.lastUpdate != null) ...[
              const SizedBox(height: 8),
              Text(
                'Last update: ${_formatDateTime(bin.lastUpdate!)}',
                style: TextStyle(
                  fontSize: 12,
                  color: Colors.grey.shade600,
                ),
              ),
            ],
            
            const SizedBox(height: 16),
            
            // Action Buttons
            Row(
              children: [
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: bin.full
                        ? null
                        : () {
                            provider.openBin(bin.type);
                          },
                    icon: const Icon(Icons.open_in_new),
                    label: const Text('Open'),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.green,
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  child: OutlinedButton.icon(
                    onPressed: () {
                      provider.closeBin(bin.type);
                    },
                    icon: const Icon(Icons.close),
                    label: const Text('Close'),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Colors.blue,
                    ),
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  onPressed: () {
                    showDialog(
                      context: context,
                      builder: (context) => AlertDialog(
                        title: const Text('Reset Bin'),
                        content: Text(
                          'Are you sure you want to reset ${bin.type} bin?',
                        ),
                        actions: [
                          TextButton(
                            onPressed: () => Navigator.pop(context),
                            child: const Text('Cancel'),
                          ),
                          TextButton(
                            onPressed: () {
                              provider.resetBin(bin.id);
                              Navigator.pop(context);
                            },
                            child: const Text('Reset'),
                          ),
                        ],
                      ),
                    );
                  },
                  icon: const Icon(Icons.refresh),
                  color: Colors.orange,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  String _formatDateTime(DateTime dateTime) {
    return '${dateTime.day}/${dateTime.month}/${dateTime.year} ${dateTime.hour}:${dateTime.minute.toString().padLeft(2, '0')}';
  }
}

