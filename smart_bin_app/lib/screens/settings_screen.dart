import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../providers/bin_provider.dart';
import '../utils/constants.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final TextEditingController _backendUrlController = TextEditingController();
  final TextEditingController _esp32IpController = TextEditingController();
  bool _isLoading = false;

  @override
  void initState() {
    super.initState();
    _loadSettings();
  }

  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    setState(() {
      _backendUrlController.text =
          prefs.getString('backend_url') ?? AppConstants.defaultBackendUrl;
      _esp32IpController.text = prefs.getString('esp32_ip') ?? '';
    });
  }

  Future<void> _saveSettings() async {
    setState(() => _isLoading = true);

    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('backend_url', _backendUrlController.text);
    await prefs.setString('esp32_ip', _esp32IpController.text);

    // Update provider
    context.read<BinProvider>().setEsp32Ip(
      _esp32IpController.text.isEmpty ? null : _esp32IpController.text,
    );

    setState(() => _isLoading = false);

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Settings saved successfully')),
      );
    }
  }

  Future<void> _testMotor(String type) async {
    setState(() => _isLoading = true);

    if (!mounted) return;

    // 1. Open
    ScaffoldMessenger.of(
      context,
    ).showSnackBar(SnackBar(content: Text('Opening $type bin...')));

    final openSuccess = await context.read<BinProvider>().openBin(type);

    if (!openSuccess) {
      if (mounted) {
        setState(() => _isLoading = false);
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Failed to open bin. Check connection.'),
          ),
        );
      }
      return;
    }

    // 2. Wait
    await Future.delayed(const Duration(seconds: 2));

    // 3. Close
    if (mounted) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('Closing $type bin...')));

      final closeSuccess = await context.read<BinProvider>().closeBin(type);

      setState(() => _isLoading = false);

      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            closeSuccess ? 'Motor test complete!' : 'Failed to close bin.',
          ),
        ),
      );
    }
  }

  @override
  void dispose() {
    _backendUrlController.dispose();
    _esp32IpController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Settings'),
        backgroundColor: Colors.green.shade700,
        foregroundColor: Colors.white,
      ),
      body: _isLoading
          ? const Center(child: CircularProgressIndicator())
          : ListView(
              padding: const EdgeInsets.all(16),
              children: [
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          'Connection Settings',
                          style: TextStyle(
                            fontSize: 18,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const SizedBox(height: 16),
                        TextField(
                          controller: _backendUrlController,
                          decoration: const InputDecoration(
                            labelText: 'Backend URL',
                            hintText: 'http://192.168.1.100:8000',
                            border: OutlineInputBorder(),
                            prefixIcon: Icon(Icons.cloud),
                          ),
                        ),
                        const SizedBox(height: 16),
                        TextField(
                          controller: _esp32IpController,
                          decoration: const InputDecoration(
                            labelText: 'ESP32 IP Address (Optional)',
                            hintText: '192.168.1.101',
                            border: OutlineInputBorder(),
                            prefixIcon: Icon(Icons.wifi),
                          ),
                          keyboardType: TextInputType.number,
                        ),
                        const SizedBox(height: 8),
                        TextButton.icon(
                          onPressed: () {
                            setState(() {
                              _esp32IpController.text = '192.168.4.1';
                            });
                          },
                          icon: const Icon(Icons.wifi_tethering),
                          label: const Text(
                            'Connect to Local AP (192.168.4.1)',
                          ),
                          style: TextButton.styleFrom(
                            foregroundColor: Colors.blue.shade700,
                            padding: EdgeInsets.zero,
                            alignment: Alignment.centerLeft,
                          ),
                        ),
                        const Padding(
                          padding: EdgeInsets.only(bottom: 16),
                          child: Text(
                            'Note: Join "SmartBin_AP" WiFi if internet is down.',
                            style: TextStyle(fontSize: 12, color: Colors.grey),
                          ),
                        ),
                        const SizedBox(height: 16),
                        SizedBox(
                          width: double.infinity,
                          child: ElevatedButton(
                            onPressed: _saveSettings,
                            style: ElevatedButton.styleFrom(
                              backgroundColor: Colors.green,
                              foregroundColor: Colors.white,
                              padding: const EdgeInsets.symmetric(vertical: 16),
                            ),
                            child: const Text('Save Settings'),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          'Diagnostics',
                          style: TextStyle(
                            fontSize: 18,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const SizedBox(height: 16),
                        ListTile(
                          leading: const Icon(
                            Icons.build,
                            color: Colors.orange,
                          ),
                          title: const Text('Test Organic Motor'),
                          subtitle: const Text('Cycle Open/Close'),
                          trailing: IconButton(
                            icon: const Icon(Icons.play_circle_fill),
                            onPressed: () => _testMotor('organic'),
                          ),
                        ),
                        const Divider(),
                        ListTile(
                          leading: const Icon(Icons.build, color: Colors.blue),
                          title: const Text('Test One-Organic Motor'),
                          subtitle: const Text('Cycle Open/Close'),
                          trailing: IconButton(
                            icon: const Icon(Icons.play_circle_fill),
                            onPressed: () => _testMotor('non_organic'),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                const SizedBox(height: 16),
                Card(
                  child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          'About',
                          style: TextStyle(
                            fontSize: 18,
                            fontWeight: FontWeight.bold,
                          ),
                        ),
                        const SizedBox(height: 16),
                        const ListTile(
                          leading: Icon(Icons.info),
                          title: Text('Version'),
                          subtitle: Text('1.0.0'),
                        ),
                        const ListTile(
                          leading: Icon(Icons.code),
                          title: Text('Smart Waste Bin'),
                          subtitle: Text('IoT Waste Management System'),
                        ),
                      ],
                    ),
                  ),
                ),
              ],
            ),
    );
  }
}
