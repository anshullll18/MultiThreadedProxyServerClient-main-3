#!/usr/bin/env python3
"""
Log Server for Proxy Admin Dashboard
This script serves the admin dashboard and provides API endpoints for retrieving proxy server logs.
"""

import os
import re
import json
import time
from datetime import datetime
from http.server import HTTPServer, SimpleHTTPRequestHandler
import argparse

# Configuration
DEFAULT_PORT = 8081
LOG_FILE_PATH = "proxy_server.log"  # Path to your proxy server log file
DASHBOARD_FILE = "admin_dashboard.html"  # The HTML file to serve

class LogServerHandler(SimpleHTTPRequestHandler):
    """Handler for serving the admin dashboard and log API"""
    
    def do_GET(self):
        """Handle GET requests"""
        if self.path == "/":
            # Serve the admin dashboard
            self.serve_dashboard()
        elif self.path == "/api/logs":
            # Serve log data as JSON
            self.serve_logs()
        elif self.path == "/api/stats":
            # Serve statistics as JSON
            self.serve_stats()
        else:
            # Serve static files
            super().do_GET()
    
    def serve_dashboard(self):
        """Serve the admin dashboard HTML file"""
        try:
            with open(DASHBOARD_FILE, "r") as f:
                content = f.read()
                
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            self.wfile.write(content.encode())
        except FileNotFoundError:
            self.send_error(404, f"Dashboard file not found: {DASHBOARD_FILE}")
    
    def serve_logs(self):
        """Parse and serve the log file as JSON"""
        try:
            logs = self.parse_log_file()
            
            self.send_response(200)
            self.send_header("Content-type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")  # For cross-origin requests
            self.end_headers()
            self.wfile.write(json.dumps(logs).encode())
        except Exception as e:
            self.send_error(500, f"Error processing logs: {str(e)}")
    
    def serve_stats(self):
        """Calculate and serve statistics from logs as JSON"""
        try:
            logs = self.parse_log_file()
            stats = self.calculate_stats(logs)
            
            self.send_response(200)
            self.send_header("Content-type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(stats).encode())
        except Exception as e:
            self.send_error(500, f"Error calculating stats: {str(e)}")
    
    def parse_log_file(self):
        """Parse the log file and return structured log entries"""
        logs = []
        log_pattern = r'\[(.*?)\] \[(.*?)\] (.*)'
        
        try:
            # Check if log file exists
            if not os.path.exists(LOG_FILE_PATH):
                return logs
            
            # Get file size
            file_size = os.path.getsize(LOG_FILE_PATH)
            
            # If file is very large, only read the last 100KB
            max_size = 100 * 1024  # 100KB
            start_position = max(0, file_size - max_size)
            
            with open(LOG_FILE_PATH, "r") as f:
                if start_position > 0:
                    f.seek(start_position)
                    # Skip possibly incomplete first line
                    f.readline()
                
                for line in f:
                    match = re.match(log_pattern, line.strip())
                    if match:
                        timestamp, level, message = match.groups()
                        logs.append({
                            "timestamp": timestamp,
                            "level": level,
                            "message": message
                        })
            
            # Sort logs by timestamp, newest first
            logs.sort(key=lambda x: x["timestamp"], reverse=True)
            
            return logs
        except Exception as e:
            print(f"Error parsing log file: {e}")
            return []
    
    def calculate_stats(self, logs):
        """Calculate statistics from logs"""
        stats = {
            "totalRequests": 0,
            "cacheHits": 0,
            "cacheSize": 0,
            "errors": 0,
            "lastUpdated": datetime.now().isoformat()
        }
        
        # Count requests
        stats["totalRequests"] = sum(
            1 for log in logs if 
            "Handling request" in log["message"] or
            "Cache HIT" in log["message"] or
            "Cache MISS" in log["message"]
        )
        
        # Count cache hits
        stats["cacheHits"] = sum(1 for log in logs if "Cache HIT" in log["message"])
        
        # Get latest cache size
        cache_size_logs = [log for log in logs if "Added element to cache" in log["message"]]
        if cache_size_logs:
            match = re.search(r'new total: (\d+) bytes', cache_size_logs[0]["message"])
            if match:
                stats["cacheSize"] = int(match.group(1))
        
        # Count errors
        stats["errors"] = sum(1 for log in logs if log["level"] == "ERROR")
        
        return stats


def run_server(port=DEFAULT_PORT):
    """Run the HTTP server"""
    server_address = ('', port)
    httpd = HTTPServer(server_address, LogServerHandler)
    print(f"Starting admin dashboard server on port {port}")
    print(f"Open http://localhost:{port}/ in your browser")
    print("Press Ctrl+C to stop the server")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down server...")
        httpd.shutdown()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Proxy Server Admin Dashboard")
    parser.add_argument("-p", "--port", type=int, default=DEFAULT_PORT,
                        help=f"Port to run the server on (default: {DEFAULT_PORT})")
    parser.add_argument("-l", "--log", type=str, default=LOG_FILE_PATH,
                        help=f"Path to the proxy server log file (default: {LOG_FILE_PATH})")
    args = parser.parse_args()
    
    LOG_FILE_PATH = args.log
    
    # Verify log file exists or warn
    if not os.path.exists(LOG_FILE_PATH):
        print(f"Warning: Log file not found at {LOG_FILE_PATH}")
        print("The dashboard will work but won't show any logs until the file is created.")
    
    # Extract dashboard HTML from artifact and save to file
    with open(DASHBOARD_FILE, "w") as f:
        # Complete HTML with API calls
        dashboard_html = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Proxy Server Admin Dashboard</title>
    <style>
        :root {
            --primary: #4a6fa5;
            --secondary: #6b8cae;
            --success: #28a745;
            --danger: #dc3545;
            --warning: #ffc107;
            --info: #17a2b8;
            --light: #f8f9fa;
            --dark: #343a40;
            --gray: #6c757d;
            --gray-light: #e9ecef;
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        }
        
        body {
            background-color: #f5f7fa;
            color: #333;
            line-height: 1.6;
        }
        
        header {
            background-color: var(--primary);
            color: white;
            padding: 1rem;
            box-shadow: 0 2px 5px rgba(0,0,0,0.1);
        }
        
        h1 {
            font-size: 1.5rem;
            display: flex;
            align-items: center;
        }

        h1 svg {
            margin-right: 10px;
        }
        
        main {
            max-width: 1200px;
            margin: 0 auto;
            padding: 1rem;
        }
        
        .dashboard {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 1rem;
            margin-bottom: 1rem;
        }
        
        .stat-card {
            background-color: white;
            padding: 1rem;
            border-radius: 8px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.05);
            text-align: center;
        }
        
        .stat-card h3 {
            color: var(--gray);
            font-size: 0.9rem;
            margin-bottom: 5px;
            text-transform: uppercase;
            letter-spacing: 1px;
        }
        
        .stat-card .value {
            font-size: 1.8rem;
            font-weight: bold;
            color: var(--primary);
        }
        
        .controls {
            display: flex;
            flex-wrap: wrap;
            gap: 1rem;
            background-color: white;
            padding: 1rem;
            border-radius: 8px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.05);
            margin-bottom: 1rem;
        }
        
        .control-group {
            display: flex;
            flex-direction: column;
            gap: 0.25rem;
        }
        
        label {
            font-size: 0.8rem;
            color: var(--gray);
            font-weight: 500;
        }
        
        select, input, button {
            padding: 0.5rem;
            border: 1px solid var(--gray-light);
            border-radius: 4px;
            font-size: 0.9rem;
        }
        
        button {
            background-color: var(--primary);
            color: white;
            border: none;
            cursor: pointer;
            transition: background-color 0.2s;
            min-width: 100px;
        }
        
        button:hover {
            background-color: var(--secondary);
        }
        
        .logs-container {
            background-color: white;
            border-radius: 8px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.05);
            overflow: hidden;
        }
        
        .logs-header {
            background-color: var(--dark);
            color: white;
            padding: 0.75rem 1rem;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        
        .logs-title {
            font-size: 1rem;
            font-weight: 500;
        }
        
        .logs-actions {
            display: flex;
            gap: 0.5rem;
        }
        
        .logs-actions button {
            background-color: transparent;
            border: 1px solid var(--gray-light);
            min-width: auto;
            padding: 0.25rem 0.5rem;
            font-size: 0.8rem;
        }
        
        .logs {
            height: 500px;
            overflow-y: auto;
            padding: 0;
            font-family: monospace;
            font-size: 0.9rem;
            background-color: #1e1e1e;
            color: #d4d4d4;
        }
        
        .log-entry {
            padding: 0.25rem 1rem;
            border-bottom: 1px solid #333;
            white-space: pre-wrap;
            word-break: break-all;
        }
        
        .log-entry:hover {
            background-color: #2a2a2a;
        }
        
        .log-ERROR {
            color: #f14c4c;
        }
        
        .log-WARN {
            color: #e5e510;
        }
        
        .log-INFO {
            color: #3794ff;
        }
        
        .log-DEBUG {
            color: #89d185;
        }
        
        .log-timestamp {
            color: #9cdcfe;
        }
        
        .log-level {
            font-weight: bold;
            padding: 0.1rem 0.25rem;
            border-radius: 3px;
            margin-right: 0.25rem;
        }
        
        .level-ERROR {
            background-color: rgba(220, 53, 69, 0.2);
        }
        
        .level-WARN {
            background-color: rgba(255, 193, 7, 0.2);
        }
        
        .level-INFO {
            background-color: rgba(23, 162, 184, 0.2);
        }
        
        .level-DEBUG {
            background-color: rgba(40, 167, 69, 0.2);
        }
        
        footer {
            text-align: center;
            padding: 1rem;
            font-size: 0.8rem;
            color: var(--gray);
            border-top: 1px solid var(--gray-light);
            margin-top: 2rem;
        }
        
        .loading {
            display: flex;
            justify-content: center;
            padding: 2rem;
        }
        
        .spinner {
            border: 4px solid rgba(0, 0, 0, 0.1);
            width: 36px;
            height: 36px;
            border-radius: 50%;
            border-left-color: var(--primary);
            animation: spin 1s linear infinite;
        }
        
        @keyframes spin {
            0% {
                transform: rotate(0deg);
            }
            100% {
                transform: rotate(360deg);
            }
        }
        
        /* Responsive adjustments */
        @media (max-width: 768px) {
            .dashboard {
                grid-template-columns: 1fr 1fr;
            }
            
            .controls {
                flex-direction: column;
            }
        }
        
        @media (max-width: 480px) {
            .dashboard {
                grid-template-columns: 1fr;
            }
        }
    </style>
</head>
<body>
    <header>
        <h1>
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
                <rect x="2" y="3" width="20" height="14" rx="2" ry="2"></rect>
                <line x1="8" y1="21" x2="16" y2="21"></line>
                <line x1="12" y1="17" x2="12" y2="21"></line>
            </svg>
            Proxy Server Admin Dashboard
        </h1>
    </header>
    
    <main>
        <div class="dashboard">
            <div class="stat-card">
                <h3>Total Requests</h3>
                <div class="value" id="total-requests">--</div>
            </div>
            <div class="stat-card">
                <h3>Cache Hits</h3>
                <div class="value" id="cache-hits">--</div>
            </div>
            <div class="stat-card">
                <h3>Cache Size</h3>
                <div class="value" id="cache-size">--</div>
            </div>
            <div class="stat-card">
                <h3>Error Rate</h3>
                <div class="value" id="error-rate">--</div>
            </div>
        </div>
        
        <div class="controls">
            <div class="control-group">
                <label for="log-level">Log Level</label>
                <select id="log-level">
                    <option value="ALL">All Levels</option>
                    <option value="ERROR">ERROR</option>
                    <option value="WARN">WARN</option>
                    <option value="INFO">INFO</option>
                    <option value="DEBUG">DEBUG</option>
                </select>
            </div>
            
            <div class="control-group">
                <label for="search">Search</label>
                <input type="text" id="search" placeholder="Filter logs...">
            </div>
            
            <div class="control-group">
                <label for="refresh-rate">Auto Refresh</label>
                <select id="refresh-rate">
                    <option value="0">Off</option>
                    <option value="5000">5 seconds</option>
                    <option value="15000" selected>15 seconds</option>
                    <option value="30000">30 seconds</option>
                    <option value="60000">1 minute</option>
                </select>
            </div>
            
            <div class="control-group">
                <label>&nbsp;</label>
                <button id="refresh-now">Refresh Now</button>
            </div>
        </div>
        
        <div class="logs-container">
            <div class="logs-header">
                <div class="logs-title">Server Logs</div>
                <div class="logs-actions">
                    <button id="clear-logs">Clear View</button>
                    <button id="download-logs">Download</button>
                </div>
            </div>
            <div class="logs" id="logs">
                <div class="loading">
                    <div class="spinner"></div>
                </div>
            </div>
        </div>
    </main>
    
    <footer>
        Proxy Server Admin Dashboard &copy; 2025
    </footer>
    
    <script>
        // Log data will be fetched from the server API
        let allLogs = [];
        
        // Stats counters
        let stats = {
            totalRequests: 0,
            cacheHits: 0,
            cacheSize: 0,
            errors: 0
        };
        
        // DOM Elements
        const logsContainer = document.getElementById('logs');
        const logLevelSelect = document.getElementById('log-level');
        const searchInput = document.getElementById('search');
        const refreshRateSelect = document.getElementById('refresh-rate');
        const refreshNowButton = document.getElementById('refresh-now');
        const clearLogsButton = document.getElementById('clear-logs');
        const downloadLogsButton = document.getElementById('download-logs');
        
        // Stats elements
        const totalRequestsEl = document.getElementById('total-requests');
        const cacheHitsEl = document.getElementById('cache-hits');
        const cacheSizeEl = document.getElementById('cache-size');
        const errorRateEl = document.getElementById('error-rate');
        
        // Auto-refresh timer
        let refreshTimer = null;
        
        // Initialize the dashboard
        function init() {
            // Set up event listeners
            logLevelSelect.addEventListener('change', filterLogs);
            searchInput.addEventListener('input', filterLogs);
            refreshRateSelect.addEventListener('change', setAutoRefresh);
            refreshNowButton.addEventListener('click', fetchLogs);
            clearLogsButton.addEventListener('click', clearLogs);
            downloadLogsButton.addEventListener('click', downloadLogs);
            
            // Initial fetch
            fetchLogs();
            fetchStats();
            
            // Set up auto-refresh based on default select value
            setAutoRefresh();
        }
        
        // Fetch logs from the server API
        function fetchLogs() {
            logsContainer.innerHTML = '<div class="loading"><div class="spinner"></div></div>';
            
            fetch('/api/logs')
                .then(response => {
                    if (!response.ok) {
                        throw new Error('Network response was not ok');
                    }
                    return response.json();
                })
                .then(data => {
                    allLogs = data;
                    filterLogs();
                })
                .catch(error => {
                    console.error('Error fetching logs:', error);
                    logsContainer.innerHTML = '<div class="log-entry">Error fetching logs. Please try again.</div>';
                });
        }
        
        // Fetch statistics from the server API
        function fetchStats() {
            fetch('/api/stats')
                .then(response => {
                    if (!response.ok) {
                        throw new Error('Network response was not ok');
                    }
                    return response.json();
                })
                .then(data => {
                    stats = data;
                    updateStatsDisplay();
                })
                .catch(error => {
                    console.error('Error fetching stats:', error);
                });
        }
        
        // Filter logs based on level and search term
        function filterLogs() {
            const level = logLevelSelect.value;
            const searchTerm = searchInput.value.toLowerCase();
            
            let filteredLogs = allLogs;
            
            // Filter by level
            if (level !== 'ALL') {
                filteredLogs = filteredLogs.filter(log => log.level === level);
            }
            
            // Filter by search term
            if (searchTerm) {
                filteredLogs = filteredLogs.filter(log => 
                    log.message.toLowerCase().includes(searchTerm) ||
                    log.timestamp.toLowerCase().includes(searchTerm)
                );
            }
            
            // Display filtered logs
            displayLogs(filteredLogs);
        }
        
        // Display logs in the container
        function displayLogs(logs) {
            if (logs.length === 0) {
                logsContainer.innerHTML = '<div class="log-entry">No logs match your criteria.</div>';
                return;
            }
            
            logsContainer.innerHTML = logs.map(log => {
                return `
                    <div class="log-entry log-${log.level}">
                        <span class="log-timestamp">[${log.timestamp}]</span>
                        <span class="log-level level-${log.level}">${log.level}</span>
                        ${log.message}
                    </div>
                `;
            }).join('');
        }
        
        // Set auto-refresh timer
        function setAutoRefresh() {
            // Clear existing timer
            if (refreshTimer) {
                clearInterval(refreshTimer);
                refreshTimer = null;
            }
            
            // Set new timer if not off
            const interval = parseInt(refreshRateSelect.value);
            if (interval > 0) {
                refreshTimer = setInterval(() => {
                    fetchLogs();
                    fetchStats();
                }, interval);
            }
        }
        
        // Clear logs from view
        function clearLogs() {
            logsContainer.innerHTML = '<div class="log-entry">Logs cleared. Click "Refresh Now" to fetch logs.</div>';
        }
        
        // Download logs as text file
        function downloadLogs() {
            // Prepare log content
            const logContent = allLogs.map(log => 
                `[${log.timestamp}] [${log.level}] ${log.message}`
            ).join('\\n');
            
            // Create blob and download link
            const blob = new Blob([logContent], { type: 'text/plain' });
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = `proxy_logs_${new Date().toISOString().slice(0,10)}.txt`;
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
        }
        
        // Update stats display
        function updateStatsDisplay() {
            totalRequestsEl.textContent = stats.totalRequests;
            cacheHitsEl.textContent = stats.cacheHits > 0 
                ? `${stats.cacheHits} (${Math.round((stats.cacheHits/stats.totalRequests)*100)}%)` 
                : '0';
            cacheSizeEl.textContent = formatBytes(stats.cacheSize);
            errorRateEl.textContent = stats.totalRequests > 0 
                ? `${Math.round((stats.errors/stats.totalRequests)*100)}%` 
                : '0%';
        }
        
        // Format bytes to human-readable format
        function formatBytes(bytes, decimals = 2) {
            if (bytes === 0) return '0 Bytes';
            
            const k = 1024;
            const dm = decimals < 0 ? 0 : decimals;
            const sizes = ['Bytes', 'KB', 'MB', 'GB'];
            
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            
            return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
        }
        
        // Initialize on page load
        window.addEventListener('load', init);
    </script>
</body>
</html>"""
        f.write(dashboard_html)
    
    print(f"Dashboard HTML written to {DASHBOARD_FILE}")
    run_server(args.port)