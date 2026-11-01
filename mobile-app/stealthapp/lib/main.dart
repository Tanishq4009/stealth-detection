import 'package:cached_network_image/cached_network_image.dart';
import 'package:flutter/material.dart';
import 'package:flutter_dotenv/flutter_dotenv.dart';
import 'package:onesignal_flutter/onesignal_flutter.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await dotenv.load(fileName: ".env");

  OneSignal.Debug.setLogLevel(OSLogLevel.verbose);
  OneSignal.initialize(dotenv.env['ONESIGNAL_APP_ID']!);
  OneSignal.Notifications.requestPermission(true);

  runApp(const StealthApp());
}

class StealthApp extends StatelessWidget {
  const StealthApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData(brightness: Brightness.dark),
      home: const DashboardScreen(),
    );
  }
}

class DashboardScreen extends StatefulWidget {
  const DashboardScreen({super.key});

  @override
  State<DashboardScreen> createState() =>
      _DashboardScreenState();
}

class _DashboardScreenState extends State<DashboardScreen> {
  String? imageUrl; // latest capture, notification ke data se aata hai

  void _updateFrom(Map<String, dynamic>? data) {
    final url = data?['image_url'];
    if (url is String && url.isNotEmpty && mounted) {
      setState(() => imageUrl = url);
    }
  }

  @override
  void initState() {
    super.initState();

    // User notification tap kare
    OneSignal.Notifications.addClickListener((event) {
      _updateFrom(event.notification.additionalData);
    });

    // App khula ho tab notification aaye
    OneSignal
        .Notifications.addForegroundWillDisplayListener((
      event,
    ) {
      _updateFrom(event.notification.additionalData);
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text("Stealth Intrusion Monitor"),
        backgroundColor: Colors.redAccent,
      ),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            const Text(
              "Latest Intrusion Capture",
              style: TextStyle(fontSize: 20),
            ),
            const SizedBox(height: 20),
            Container(
              height: 300,
              width: 300,
              decoration: BoxDecoration(
                border: Border.all(
                  color: Colors.redAccent,
                  width: 2,
                ),
                borderRadius: BorderRadius.circular(12),
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(10),
                child: imageUrl == null
                    ? const Center(
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            Icon(Icons.security, size: 50),
                            SizedBox(height: 8),
                            Text(
                              "No intrusion captured yet",
                            ),
                          ],
                        ),
                      )
                    : CachedNetworkImage(
                        imageUrl: imageUrl!,
                        placeholder: (c, u) => const Center(
                          child:
                              CircularProgressIndicator(),
                        ),
                        errorWidget: (c, u, e) =>
                            const Center(
                              child: Icon(
                                Icons.broken_image,
                                size: 50,
                              ),
                            ),
                        fit: BoxFit.cover,
                      ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
