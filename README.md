# QR-Code-Based-Loyalty-System-for-Cuddle-and-Bean-Cafe-Montalban
A Wi-Fi-based loyalty system using ESP32 to help businesses retain customers and simplify management. Features include QR code scanning, local storage, staff login, and a simple web interface. Ideal for cafés, shops, and service businesses to encourage repeat visits and improve customer experience.
Features

QR Code Scanning: Customers are identified via QR codes scanned through serial communication.
Local Data Storage: Customer data and scan history are stored persistently using LittleFS.
Staff Login & Management: Secure login for staff to view, remove, or redeem rewards.
Web Dashboard: Real-time system overview, customer records, and export options.
Wi-Fi Connectivity: Access point and station mode for flexible network setup.
Touch Authentication: Touch sensor for quick staff login.
Reward System: Earn points per scan, redeem after reaching certain points.


Hardware Requirements

ESP32 Development Board
LCD with I2C interface
Touch sensor (connected to GPIO 4)
Serial QR code scanner (connected to GPIO 16 & 17)
Power supply


Network Setup

Access Point SSID: ******** (Password: ***********)
Station Mode (Wi-Fi): Connects to your existing Wi-Fi network


User Roles

Staff: Can login via PIN, remove customers, redeem rewards, and export data.
Admin: Access via staff login, with full control over customer data.


How It Works

System Initialization:

Connects to Wi-Fi and sets up web server.
Loads customer data from FS.


Customer Scan:

QR code data received via serial.
Creates or updates customer record.
Adds points and logs scan history.
Provides visual feedback on LCD.


Staff Authentication:

Touch sensor or PIN input grants access.
Staff can remove customers, redeem rewards, or export CSV.


Dashboard & Control:

Access via web browser.
View customer stats, history, and system status.
Export data as CSV.




Usage

Scanning Customers:

Scan QR code from customer device.
Customer record updates automatically.
Rewards become available after reaching point threshold.


Staff Operations:

Touch sensor for quick login.
Access web dashboard for management.
Remove or redeem customers as needed.


Admin Actions:

Login with PIN (default PIN: ****).
Remove customers or export CSV.




Customization

Change Wi-Fi credentials in the code.
Adjust reward points or thresholds.
Modify PINs for staff/admin login.
Customize LCD messages and interface.


Notes

Ensure the QR scanner outputs data in the expected format.
Use a stable power supply to prevent resets.
Adjust MAX_CUSTOMERS and MAX_HISTORY as needed.
