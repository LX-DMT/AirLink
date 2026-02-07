import http.server
import socketserver
import urllib.parse
import subprocess
import os
import sys

PORT = 80

HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>WiFi Configuration</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; }
        .container { background-color: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        h2 { color: #333; }
        label { display: block; margin-top: 10px; color: #666; }
        input[type=text], input[type=password] { width: 100%; padding: 12px 20px; margin: 8px 0; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
        input[type=submit] { width: 100%; background-color: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        input[type=submit]:hover { background-color: #45a049; }
    </style>
</head>
<body>
    <div class="container">
        <h2>WiFi 配置</h2>
        <form method="POST">
            <label for="ssid">SSID (WiFi 名称):</label>
            <input type="text" id="ssid" name="ssid" required>
            <label for="pass">密码:</label>
            <input type="password" id="pass" name="pass">
            <input type="submit" value="保存并连接">
        </form>
    </div>
</body>
</html>
"""

class WiFiConfigHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        # Redirect all requests to the root path to handle captive portal checks
        if self.path != "/" and not self.path.startswith("/?"):
             self.send_response(302)
             self.send_header('Location', '/')
             self.end_headers()
             return

        self.send_response(200)
        self.send_header("Content-type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(HTML_TEMPLATE.encode('utf-8'))

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length).decode('utf-8')
        params = urllib.parse.parse_qs(post_data)
        
        ssid = params.get('ssid', [''])[0]
        password = params.get('pass', [''])[0]
        
        self.send_response(200)
        self.send_header("Content-type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write("<html><body><div style='text-align:center; margin-top:50px;'><h2>设置已保存，正在切换到 STA 模式并连接...</h2><p>请断开此热点并重新连接您的 WiFi。</p></div></body></html>".encode('utf-8'))
        
        print(f"Received SSID: {ssid}")
        # Trigger mode switch in the background after a short delay to allow the response to be sent
        subprocess.Popen(["sh", "-c", f"sleep 2 && /usr/bin/wifi_mode_switch.sh sta '{ssid}' '{password}'"])

if __name__ == "__main__":
    # Ensure we are running as root to bind to port 80
    if os.getuid() != 0:
        print("Error: This script must be run as root.")
        sys.exit(1)
        
    os.chdir("/tmp")
    # Allow port reuse to avoid "Address already in use" errors on restart
    socketserver.TCPServer.allow_reuse_address = True
    try:
        with socketserver.TCPServer(("", PORT), WiFiConfigHandler) as httpd:
            print(f"WiFi Config Server started on port {PORT}")
            httpd.serve_forever()
    except Exception as e:
        print(f"Error starting server: {e}")
        sys.exit(1)
