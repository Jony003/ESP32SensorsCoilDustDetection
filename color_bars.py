import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

ser = serial.Serial("COM5", 115200, timeout=1)

labels = ["R", "G", "B", "Clear"]
colors = ["red", "green", "blue", "gray"]
values = [0, 0, 0, 0]

fig, ax = plt.subplots()
bars = ax.bar(labels, values, color=colors)
ax.set_ylim(0, 1000)
ax.set_title("APDS9960 Color")

def update(_):
    line = ser.readline().decode(errors="ignore").strip()
    parts = line.split(",")
    if len(parts) == 4 and all(p.isdigit() for p in parts):
        nums = [int(p) for p in parts]
        for bar, v in zip(bars, nums):
            bar.set_height(v)
        top = max(nums)
        if top > ax.get_ylim()[1]:
            ax.set_ylim(0, top * 1.2)
    return bars

ani = FuncAnimation(fig, update, interval=50, cache_frame_data=False)
plt.show()