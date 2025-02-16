#define SSID_ARRAY_LEN 1

// 2D Array of WiFi SSIDs and passwords to try and store multiple networks
char ssids[SSID_ARRAY_LEN][32] = {{""}};
char passes[SSID_ARRAY_LEN][32] = {{""}};

// Pishock API Credentials, obtain them from the pishock webpage
String username = "";
String api_key = "";
String dev_code = "";

// Give the device a name! required for PiShock API function
String dev_name = "";