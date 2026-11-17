import sys
import math
from collections import deque

import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

PORT = "COM5"
if len(sys.argv) > 1:
    PORT = sys.argv[1]

ser = serial.Serial(PORT, 115200, timeout=1)

apds_labels = ["R", "G", "B", "Clear"]
apds_colors = ["red", "green", "blue", "gray"]

as_labels = ["405", "425", "450", "475", "515", "550", "555",
             "600", "640", "690", "745", "855", "VIS"]
as_colors = ["#7f00ff", "#3f00ff", "#0040ff", "#00a0ff", "#00ff80",
             "#80ff00", "#a0ff00", "#ffa000", "#ff5000", "#ff0000",
             "#a00000", "#600000", "gray"]

HISTORY = 200

fig, (ax1, ax2, ax3) = plt.subplots(
    3, 1, figsize=(11, 10),
    gridspec_kw={"height_ratios": [1, 1.2, 0.9]},
)
bars1 = ax1.bar(apds_labels, [0] * 4, color=apds_colors)
bars2 = ax2.bar(as_labels, [0] * 13, color=as_colors)
ax1.set_title("APDS9960")
ax2.set_title("AS7343 Spectrum (nm)")
ax3.set_title("SDP810 Differential Pressure")
ax3.set_ylabel("Pa")
ax3.set_xlabel("samples")
ax3.axhline(0, color="gray", linewidth=0.8)
sdp_line, = ax3.plot([], [], color="steelblue", linewidth=1.5)
sdp_hist = deque(maxlen=HISTORY)
sdp_text = ax3.text(
    0.02, 0.95, "SDP810: --",
    transform=ax3.transAxes, va="top", fontsize=11,
)

# 每根柱子顶上的数值
texts1 = [ax1.text(b.get_x() + b.get_width() / 2, 0, "0",
                   ha="center", va="bottom", fontsize=10) for b in bars1]
texts2 = [ax2.text(b.get_x() + b.get_width() / 2, 0, "0",
                   ha="center", va="bottom", fontsize=8) for b in bars2]
plt.tight_layout()


def is_number(s):
    try:
        float(s)
        return True
    except ValueError:
        return False


def fmt(v):
    return str(int(v)) if float(v).is_integer() else f"{v:.2f}"


def draw(ax, bars, texts, vals):
    top = max(max(vals), 10)
    ax.set_ylim(0, top * 1.25)          # 纵轴跟着当前最大值走
    for bar, txt, v in zip(bars, texts, vals):
        bar.set_height(v)
        txt.set_y(v)
        txt.set_text(fmt(v))


def update_sdp(dp, temp):
    if math.isnan(dp) or math.isnan(temp):
        sdp_text.set_text("SDP810: --")
        return
    sdp_hist.append(dp)
    xs = list(range(len(sdp_hist)))
    sdp_line.set_data(xs, list(sdp_hist))
    ax3.set_xlim(0, max(len(sdp_hist) - 1, 1))
    mag = max((abs(v) for v in sdp_hist), default=0.5)
    mag = max(mag, 0.5)
    ax3.set_ylim(-mag * 1.2, mag * 1.2)  # 差压可正可负，对称自适应
    sdp_text.set_text(f"{dp:.2f} Pa    {temp:.2f} °C")


def update(_):
    line = None
    while ser.in_waiting:               # 只取最新的一行，避免延迟
        line = ser.readline()
    if line is None:
        line = ser.readline()
    parts = [p.strip() for p in line.decode(errors="ignore").strip().split(",")]
    if len(parts) not in (17, 19) or not all(is_number(p) for p in parts):
        return
    nums = [float(p) for p in parts]
    draw(ax1, bars1, texts1, nums[:4])
    draw(ax2, bars2, texts2, nums[4:17])
    if len(nums) >= 19:
        update_sdp(nums[17], nums[18])


ani = FuncAnimation(fig, update, interval=50, cache_frame_data=False)
plt.show()
