# Centrifuge GUI - Controls motor via Arduino
# Music player + real-time monitoring
import serial
import time
import pygame
import random
import os
import tkinter as tk
from tkinter import ttk
import threading

# Config
PORT = 'COM6'
BAUD = 9600

# Colors
PINK = "#FF1493"
GREEN = "#10B981"
RED = "#EF4444"
GRAY = "#718096"
WHITE = "#FFFFFF"
LIGHT_GRAY = "#F8F9FA"

# Sound setup
pygame.mixer.init()
pygame.mixer.music.set_volume(0.7)

SCRIPT_DIR = os.path.dirname(__file__)
HAPPY_SOUND = os.path.join(SCRIPT_DIR, "sounds", "happy.mp3")
SAD_SOUND = os.path.join(SCRIPT_DIR, "sounds", "sad.mp3")

# Load playlist
playlist = []
try:
    playlist = [os.path.join("playlist", f) for f in os.listdir("playlist") 
                if f.endswith(('.mp3', '.wav'))]
    print(f"Loaded {len(playlist)} songs")
except:
    print("No playlist folder")

# RPM to PWM conversion (from testing/calibration)
RPM_MAP = [(10, 1100), (20, 1400), (30, 1900), (40, 2300), (50, 2600),
           (60, 2800), (70, 3000), (80, 3100), (90, 3200), (100, 3300)]

def rpm_to_duty(rpm):
    if rpm <= 1100: return 10
    if rpm >= 3300: return 100
    
    for i in range(len(RPM_MAP) - 1):
        d1, r1 = RPM_MAP[i]
        d2, r2 = RPM_MAP[i + 1]
        if r1 <= rpm <= r2:
            return round(d1 + (d2 - d1) * (rpm - r1) / (r2 - r1), 1)
    return 50

class CentrifugeGUI:
    def __init__(self, root):
        self.root = root
        root.title("Centrifuge Control")
        root.geometry("1000x700")
        root.configure(bg=LIGHT_GRAY)
        
        self.ser = None
        self.connected = False
        self.playing = False
        self.current_song = None
        
        self.build_ui()
        self.connect()
        
    def build_ui(self):
        # Header
        header = tk.Frame(self.root, bg=PINK, height=60)
        header.pack(fill=tk.X)
        
        tk.Label(header, text="⚡ Centrifuge Control", font=("Arial", 24, "bold"),
                bg=PINK, fg="white").pack(side=tk.LEFT, padx=20, pady=10)
        
        self.status_dot = tk.Label(header, text="●", font=("Arial", 20),
                                   bg=PINK, fg=RED)
        self.status_dot.pack(side=tk.LEFT)
        
        self.status_text = tk.Label(header, text="Disconnected", font=("Arial", 12),
                                    bg=PINK, fg="white")
        self.status_text.pack(side=tk.LEFT, padx=5)
        
        # Main area
        main = tk.Frame(self.root, bg=LIGHT_GRAY)
        main.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        # Left - Controls
        left = tk.Frame(main, bg=WHITE, width=300)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=(0, 10))
        
        tk.Label(left, text="⚙️ Configuration", font=("Arial", 14, "bold"),
                bg=WHITE).pack(pady=10)
        
        # RPM input
        tk.Label(left, text="Target RPM:", bg=WHITE, fg=GRAY).pack(pady=(10,0))
        self.rpm_entry = tk.Entry(left, font=("Arial", 16), width=15)
        self.rpm_entry.pack(pady=5)
        self.rpm_entry.insert(0, "2300")
        self.rpm_entry.bind("<KeyRelease>", self.update_duty)
        
        self.duty_label = tk.Label(left, text="(40% duty)", bg=WHITE, fg=GRAY)
        self.duty_label.pack()
        
        # Duration input
        tk.Label(left, text="Duration (seconds):", bg=WHITE, fg=GRAY).pack(pady=(10,0))
        self.dur_entry = tk.Entry(left, font=("Arial", 16), width=15)
        self.dur_entry.pack(pady=5)
        self.dur_entry.insert(0, "30")
        
        tk.Label(left, text="💡 Press Arduino button to start", 
                bg="#FFE5EC", fg=PINK, wraplength=250).pack(pady=20, padx=10)
        
        # Middle - Status
        mid = tk.Frame(main, bg=WHITE)
        mid.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=10)
        
        # State
        self.state_label = tk.Label(mid, text="IDLE", font=("Arial", 28, "bold"),
                                    bg=WHITE, fg=GRAY)
        self.state_label.pack(pady=10)
        
        # Timer with progress ring
        canvas_frame = tk.Frame(mid, bg=WHITE)
        canvas_frame.pack(pady=10)
        
        self.progress = tk.Canvas(canvas_frame, width=200, height=200, bg=WHITE, highlightthickness=0)
        self.progress.pack()
        
        # Background circle
        self.progress.create_oval(20, 20, 180, 180, outline="#E2E8F0", width=8)
        # Progress arc
        self.arc = self.progress.create_arc(20, 20, 180, 180, start=90, extent=0,
                                            outline=PINK, width=8, style=tk.ARC)
        
        self.timer = tk.Label(self.progress, text="--:--", font=("Arial", 36, "bold"),
                             bg=WHITE, fg=PINK)
        self.progress.create_window(100, 100, window=self.timer)
        
        # Console
        tk.Label(mid, text="📜 Console", font=("Arial", 12, "bold"),
                bg=WHITE).pack(pady=(20,5))
        
        console_frame = tk.Frame(mid, bg=WHITE)
        console_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        scroll = tk.Scrollbar(console_frame)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        
        self.console = tk.Text(console_frame, height=10, font=("Consolas", 9),
                              bg="#F7FAFC", fg="#2D3748", state=tk.DISABLED,
                              yscrollcommand=scroll.set)
        self.console.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll.config(command=self.console.yview)
        
        # Right - Music
        right = tk.Frame(main, bg=WHITE, width=250)
        right.pack(side=tk.LEFT, fill=tk.Y)
        
        tk.Label(right, text="🎵 Music", font=("Arial", 14, "bold"),
                bg=WHITE).pack(pady=10)
        
        # Music icon
        icon_frame = tk.Frame(right, bg="#FFE5EC", height=150)
        icon_frame.pack(fill=tk.X, padx=20, pady=10)
        
        self.music_icon = tk.Label(icon_frame, text="♪", font=("Arial", 60),
                                   bg="#FFE5EC", fg="#FF69B4")
        self.music_icon.pack(expand=True)
        
        self.song_label = tk.Label(right, text="No song", font=("Arial", 10, "bold"),
                                   bg=WHITE, wraplength=200)
        self.song_label.pack(pady=5)
        
        tk.Label(right, text=f"{len(playlist)} songs", bg=WHITE, fg=GRAY).pack()
        
        # Volume
        tk.Label(right, text="🔊 Volume", bg=WHITE, fg=GRAY).pack(pady=(20,5))
        
        self.vol_slider = ttk.Scale(right, from_=0, to=100, orient=tk.HORIZONTAL,
                                    command=self.set_volume)
        self.vol_slider.set(70)
        self.vol_slider.pack(fill=tk.X, padx=20)
        
        self.vol_label = tk.Label(right, text="70%", bg=WHITE, fg=GRAY)
        self.vol_label.pack()
        
        # Footer
        self.footer = tk.Label(self.root, text="💡 Press Arduino button to start",
                              bg=LIGHT_GRAY, fg=GRAY)
        self.footer.pack(pady=10)
        
    def log(self, msg):
        self.console.config(state=tk.NORMAL)
        self.console.insert(tk.END, f"[{time.strftime('%H:%M:%S')}] {msg}\n")
        self.console.see(tk.END)
        self.console.config(state=tk.DISABLED)
        
    def update_duty(self, e=None):
        try:
            rpm = int(self.rpm_entry.get())
            duty = rpm_to_duty(rpm)
            self.duty_label.config(text=f"({duty}% duty)")
        except:
            self.duty_label.config(text="(--% duty)")
            
    def set_volume(self, val):
        pygame.mixer.music.set_volume(float(val) / 100)
        self.vol_label.config(text=f"{int(float(val))}%")
        
    def update_ring(self, current, total):
        if total > 0:
            angle = (current / total) * 360
            self.progress.itemconfig(self.arc, extent=-angle)
            
    def send_params(self):
        try:
            rpm = int(self.rpm_entry.get())
            duty = rpm_to_duty(rpm)
            dur = self.dur_entry.get()
            
            self.log(f"Sending: {rpm} RPM ({duty}%), {dur}s")
            self.ser.write(f"{duty}\n".encode())
            time.sleep(0.1)
            self.ser.write(f"{dur}\n".encode())
        except Exception as e:
            self.log(f"Error: {e}")
            
    def play_song(self):
        if not playlist: return
        
        available = [s for s in playlist if s != self.current_song]
        self.current_song = random.choice(available if available else playlist)
        
        name = os.path.splitext(os.path.basename(self.current_song))[0]
        self.song_label.config(text=name)
        
        try:
            pygame.mixer.music.load(self.current_song)
            pygame.mixer.music.play()
            self.playing = True
            self.log(f"Playing: {name}")
        except Exception as e:
            self.log(f"Music error: {e}")
            
    def stop_music(self):
        if self.playing:
            pygame.mixer.music.stop()
            self.playing = False
            self.song_label.config(text="No song")
            
    def play_sound(self, path):
        if os.path.exists(path):
            try:
                pygame.mixer.Sound(path).play()
            except:
                pass
                
    def connect(self):
        def try_connect():
            try:
                self.log(f"Connecting to {PORT}...")
                self.ser = serial.Serial(PORT, BAUD, timeout=1)
                time.sleep(2)
                self.connected = True
                self.status_dot.config(fg=GREEN)
                self.status_text.config(text="Connected")
                self.log("✓ Connected")
                self.read_loop()
            except Exception as e:
                self.log(f"Connection failed: {e}")
                
        threading.Thread(target=try_connect, daemon=True).start()
        
    def read_loop(self):
        def read():
            while self.connected:
                try:
                    if self.playing and not pygame.mixer.music.get_busy():
                        self.play_song()
                        
                    if self.ser.in_waiting:
                        line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                        self.process(line)
                except Exception as e:
                    self.log(f"Read error: {e}")
                    
        threading.Thread(target=read, daemon=True).start()
        
    def process(self, line):
        self.log(line)
        
        # Send params when Arduino asks
        if "PWM duty cycle" in line:
            self.send_params()
            
        # Parse timer
        if "[RUN]" in line and "s" in line:
            try:
                time_str = line.split("[RUN]")[1].split("|")[0].strip()
                self.timer.config(text=time_str)
                
                current = int(time_str.replace('s', ''))
                total = int(self.dur_entry.get())
                self.update_ring(total - current, total)
            except:
                pass
                
        # State changes
        if line.strip() == "RUNNING":
            self.state_label.config(text="RUNNING", fg=GREEN)
            self.footer.config(text="⚡ Running...")
            self.play_song()
            
        elif line.strip() == "COMPLETE":
            self.state_label.config(text="COMPLETE", fg=GREEN)
            self.footer.config(text="✓ Done!")
            self.timer.config(text="DONE")
            self.stop_music()
            self.play_sound(HAPPY_SOUND)
            
        elif "EMERGENCY" in line:
            self.state_label.config(text="EMERGENCY", fg=RED)
            self.footer.config(text="⚠️ Emergency stop!")
            self.timer.config(text="STOP")
            self.stop_music()
            self.play_sound(SAD_SOUND)
            
        elif line.strip() == "IDLE":
            self.state_label.config(text="IDLE", fg=GRAY)
            self.timer.config(text="--:--")
            self.footer.config(text="💡 Press Arduino button to start")

if __name__ == "__main__":
    root = tk.Tk()
    app = CentrifugeGUI(root)
    root.mainloop()