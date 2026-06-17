import sys
import socket
import time
import numpy as np
from PyQt5.QtWidgets import QApplication, QMainWindow, QVBoxLayout, QHBoxLayout, QWidget, QPushButton, QGridLayout, QLabel, QCheckBox, QGroupBox, QLineEdit, QMessageBox
from PyQt5.QtCore import QThread, pyqtSignal
from PyQt5.QtGui import QFont
import pyqtgraph as pg

TCP_PORT = 8080

class TCPClientThread(QThread):
    new_data = pyqtSignal(int, float, float, float, float, float)
    connection_error = pyqtSignal(str)
    
    def __init__(self, ip, start_index=0):
        super().__init__()
        self.ip = ip
        self.start_index = start_index
        self.running = True
        self.sock = None

    def run(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(3.0)  # Timeout so it doesn't freeze on bad IP
        try:
            self.sock.connect((self.ip, TCP_PORT))
            self.sock.sendall(f"SYNC:{self.start_index}\n".encode('utf-8'))
            self.sock.settimeout(None)  # Remove timeout for normal blocking recv
            
            buffer = ""
            while self.running:
                try:
                    data = self.sock.recv(1024).decode('utf-8')
                    if not data:
                        break
                    
                    buffer += data
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        if line:
                            parts = line.split(',')
                            # Format expected: Index, Voltage, Current, FanTemp, Temp1, Temp2
                            if len(parts) == 6:
                                idx, volt, curr, fan_t, t1, t2 = map(float, parts)
                                self.new_data.emit(int(idx), volt, curr, fan_t, t1, t2)
                except Exception as e:
                    if self.running:
                        print(f"Receive error: {e}")
                    break
        except Exception as e:
            self.connection_error.emit(str(e))

    def reset_device(self):
        if self.sock:
            try:
                self.sock.sendall(b"RESET\n")
            except:
                pass

    def stop(self):
        self.running = False
        if self.sock:
            try:
                self.sock.shutdown(socket.SHUT_RDWR) # Unblock recv cleanly
                self.sock.close()
            except:
                pass

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("DC Load Telemetry Dashboard")
        self.resize(1200, 800)

        # --- Data Arrays ---
        self.indices = []
        self.volt_data = []
        self.current_data, self.power_data = [], []
        self.fan_t_data, self.t1_data, self.t2_data = [], [], []
        
        # --- Statistics Tracking ---
        self.start_time = time.time()
        self.count = 0
        
        # FIXED: Updated dictionary keys to match new names
        self.stats = {
            'Volt': {'min': float('inf'), 'max': float('-inf'), 'sum': 0},
            'Current': {'min': float('inf'), 'max': float('-inf'), 'sum': 0},
            'Power': {'min': float('inf'), 'max': float('-inf'), 'sum': 0},
            'MOSFET_Temp': {'min': float('inf'), 'max': float('-inf'), 'sum': 0},
            'Temperature 1': {'min': float('inf'), 'max': float('-inf'), 'sum': 0},
            'Temperature 2': {'min': float('inf'), 'max': float('-inf'), 'sum': 0},
        }

        # --- Main Layout ---
        main_widget = QWidget()
        self.setCentralWidget(main_widget)
        main_layout = QHBoxLayout(main_widget)

        # --- LEFT PANEL (Stats & Controls) ---
        left_panel = QVBoxLayout()
        left_panel.setContentsMargins(10, 10, 10, 10)
        
        # Connection Settings
        conn_group = QGroupBox("Connection Settings")
        conn_layout = QHBoxLayout()
        conn_group.setLayout(conn_layout)
        
        self.ip_input = QLineEdit("192.168.1.23") # Default IP
        self.connect_btn = QPushButton("Connect / Update IP")
        self.connect_btn.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;")
        self.connect_btn.clicked.connect(self.reconnect)
        
        conn_layout.addWidget(QLabel("ESP IP:"))
        conn_layout.addWidget(self.ip_input)
        conn_layout.addWidget(self.connect_btn)
        
        left_panel.addWidget(conn_group)

        # Time Display
        self.time_label = QLabel("Run Time: 0 Secs")
        self.time_label.setFont(QFont("Arial", 14, QFont.Bold))
        left_panel.addWidget(self.time_label)

        # Stats Table
        stats_group = QGroupBox("Real-Time Statistics")
        self.stats_grid = QGridLayout()
        stats_group.setLayout(self.stats_grid)
        
        headers = ["Metric", "Current", "Avg", "Min", "Max"]
        for col, h in enumerate(headers):
            lbl = QLabel(h)
            lbl.setFont(QFont("Arial", 10, QFont.Bold))
            self.stats_grid.addWidget(lbl, 0, col)

        self.stat_labels = {}
        # FIXED: Updated metrics list to match the UI row headers and dictionary keys
        metrics = ["Volt", "Current", "Power", "MOSFET_Temp", "Temperature 1", "Temperature 2"]
        for row, metric in enumerate(metrics, start=1):
            self.stats_grid.addWidget(QLabel(metric), row, 0)
            self.stat_labels[metric] = {
                'cur': QLabel("0.0"), 'avg': QLabel("0.0"),
                'min': QLabel("0.0"), 'max': QLabel("0.0")
            }
            self.stats_grid.addWidget(self.stat_labels[metric]['cur'], row, 1)
            self.stats_grid.addWidget(self.stat_labels[metric]['avg'], row, 2)
            self.stats_grid.addWidget(self.stat_labels[metric]['min'], row, 3)
            self.stats_grid.addWidget(self.stat_labels[metric]['max'], row, 4)
            
        left_panel.addWidget(stats_group)

        # Plot Toggles
        toggle_group = QGroupBox("Graph Controls (Enable/Disable)")
        toggle_layout = QVBoxLayout()
        toggle_group.setLayout(toggle_layout)
        
        def create_check(name, checked, callback):
            chk = QCheckBox(name)
            chk.setChecked(checked)
            chk.toggled.connect(callback)
            return chk

        toggle_layout.addWidget(QLabel("<b>Graph 1 (Voltage):</b>"))
        self.chk_g1_v = create_check("Show Voltage", True, lambda c: self.g1_v_curve.setVisible(c))
        toggle_layout.addWidget(self.chk_g1_v)

        toggle_layout.addWidget(QLabel("<b>Graph 3 (Power):</b>"))
        self.chk_g3_p = create_check("Show Power", True, lambda c: self.g3_p_curve.setVisible(c))
        toggle_layout.addWidget(self.chk_g3_p)

        toggle_layout.addWidget(QLabel("<b>Graph 4 (Temperatures):</b>"))
        # FIXED: Updated checkbox labels
        self.chk_g4_fan = create_check("Show MOSFET Temp", True, lambda c: self.g4_fan_curve.setVisible(c))
        self.chk_g4_t1 = create_check("Show Temp 1", True, lambda c: self.g4_t1_curve.setVisible(c))
        self.chk_g4_t2 = create_check("Show Temp 2", True, lambda c: self.g4_t2_curve.setVisible(c))
        toggle_layout.addWidget(self.chk_g4_fan)
        toggle_layout.addWidget(self.chk_g4_t1)
        toggle_layout.addWidget(self.chk_g4_t2)

        left_panel.addWidget(toggle_group)
        left_panel.addStretch()

        # Reset Button
        self.reset_btn = QPushButton("RESET DEVICE BUFFER & DASHBOARD")
        self.reset_btn.setStyleSheet("background-color: red; color: white; font-weight: bold; padding: 10px;")
        self.reset_btn.clicked.connect(self.send_reset)
        left_panel.addWidget(self.reset_btn)

        # --- RIGHT PANEL (Graphs) ---
        pg.setConfigOptions(antialias=True)
        self.graphWidget = pg.GraphicsLayoutWidget()
        
        # Graph 1: Voltage
        self.g1 = self.graphWidget.addPlot(title="Voltage (V)")
        self.g1.addLegend()
        self.g1_v_curve = self.g1.plot(pen='y', name="Voltage")
        self.graphWidget.nextRow()

        # Graph 2: Current
        self.g2 = self.graphWidget.addPlot(title="Current (A)")
        self.g2_curr_curve = self.g2.plot(pen='g', name="Current")
        self.graphWidget.nextRow()

        # Graph 3: Power
        self.g3 = self.graphWidget.addPlot(title="Power (W)")
        self.g3.addLegend()
        self.g3_p_curve = self.g3.plot(pen='r', name="Power")
        self.graphWidget.nextRow()

        # Graph 4: Temperatures
        self.g4 = self.graphWidget.addPlot(title="Temperatures (°C)")
        self.g4.addLegend()
        # FIXED: Updated plot legend names
        self.g4_fan_curve = self.g4.plot(pen='c', name="MOSFET Temp")    # Cyan
        self.g4_t1_curve = self.g4.plot(pen='m', name="Temp 1")          # Magenta
        self.g4_t2_curve = self.g4.plot(pen=(255, 165, 0), name="Temp 2") # Orange

        # Combine Layouts
        main_layout.addLayout(left_panel, stretch=1)
        main_layout.addWidget(self.graphWidget, stretch=3)

        # Initialize TCP Thread placeholder
        self.tcp_thread = None
        
        # Auto-connect on startup
        self.reconnect()

    def reconnect(self):
        # Stop existing thread if running
        if self.tcp_thread and self.tcp_thread.isRunning():
            self.tcp_thread.stop()
            self.tcp_thread.wait() # Wait for thread to finish safely

        ip = self.ip_input.text().strip()
        if not ip:
            return

        self.connect_btn.setText("Connecting...")
        self.connect_btn.setEnabled(False)
        QApplication.processEvents() # Force UI update

        self.tcp_thread = TCPClientThread(ip=ip, start_index=0)
        self.tcp_thread.new_data.connect(self.update_data)
        self.tcp_thread.connection_error.connect(self.show_connection_error)
        self.tcp_thread.start()
        
        self.connect_btn.setText("Connect / Update IP")
        self.connect_btn.setEnabled(True)

    def show_connection_error(self, error_msg):
        print(f"Failed to connect: {error_msg}")
        self.connect_btn.setText("Connect / Update IP")
        self.connect_btn.setEnabled(True)

    def update_stats_dict(self, key, val):
        self.stats[key]['sum'] += val
        if val < self.stats[key]['min']: self.stats[key]['min'] = val
        if val > self.stats[key]['max']: self.stats[key]['max'] = val
        
        avg = self.stats[key]['sum'] / self.count
        
        self.stat_labels[key]['cur'].setText(f"{val:.2f}")
        self.stat_labels[key]['avg'].setText(f"{avg:.2f}")
        self.stat_labels[key]['min'].setText(f"{self.stats[key]['min']:.2f}")
        self.stat_labels[key]['max'].setText(f"{self.stats[key]['max']:.2f}")

    def update_data(self, idx, volt, curr, fan_t, t1, t2):
        self.count += 1
        power = volt * curr

        # Time Calculation
        elapsed = time.time() - self.start_time
        if elapsed > 600:
            self.time_label.setText(f"Run Time: {elapsed/60.0:.2f} Mins")
        else:
            self.time_label.setText(f"Run Time: {elapsed:.0f} Secs")

        # FIXED: Applied your requested names
        self.update_stats_dict('Volt', volt)
        self.update_stats_dict('Current', curr)
        self.update_stats_dict('Power', power)
        self.update_stats_dict('MOSFET_Temp', fan_t)
        self.update_stats_dict('Temperature 1', t1)
        self.update_stats_dict('Temperature 2', t2)

        # Append Data
        self.indices.append(idx)
        self.volt_data.append(volt)
        self.current_data.append(curr)
        self.power_data.append(power)
        self.fan_t_data.append(fan_t)
        self.t1_data.append(t1)
        self.t2_data.append(t2)

        # Update Plots
        self.g1_v_curve.setData(self.indices, self.volt_data)
        self.g2_curr_curve.setData(self.indices, self.current_data)
        self.g3_p_curve.setData(self.indices, self.power_data)
        
        self.g4_fan_curve.setData(self.indices, self.fan_t_data)
        self.g4_t1_curve.setData(self.indices, self.t1_data)
        self.g4_t2_curve.setData(self.indices, self.t2_data)

    def send_reset(self):
        if self.tcp_thread:
            self.tcp_thread.reset_device()
        self.count = 0
        self.start_time = time.time()
        
        # Clear Data
        self.indices.clear()
        self.volt_data.clear()
        self.current_data.clear()
        self.power_data.clear()
        self.fan_t_data.clear()
        self.t1_data.clear()
        self.t2_data.clear()

        # Clear Plots
        self.g1_v_curve.clear()
        self.g2_curr_curve.clear()
        self.g3_p_curve.clear()
        self.g4_fan_curve.clear()
        self.g4_t1_curve.clear()
        self.g4_t2_curve.clear()

        # Reset Stats Dictionary
        for key in self.stats:
            self.stats[key] = {'min': float('inf'), 'max': float('-inf'), 'sum': 0}
            self.stat_labels[key]['cur'].setText("0.0")
            self.stat_labels[key]['avg'].setText("0.0")
            self.stat_labels[key]['min'].setText("0.0")
            self.stat_labels[key]['max'].setText("0.0")
            
        self.time_label.setText("Run Time: 0 Secs")

    def closeEvent(self, event):
        if self.tcp_thread:
            self.tcp_thread.stop()
            self.tcp_thread.wait()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec_())