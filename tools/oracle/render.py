"""Render DXF files to PNG with ezdxf's matplotlib backend: render.py out.png a.dxf [b.dxf]"""
import sys, ezdxf, matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from ezdxf.addons.drawing import RenderContext, Frontend
from ezdxf.addons.drawing.matplotlib import MatplotlibBackend
from ezdxf.addons.drawing.config import Configuration, BackgroundPolicy, ColorPolicy
files = sys.argv[2:]
fig, axes = plt.subplots(1, len(files), figsize=(9 * len(files), 7), dpi=110)
if len(files) == 1: axes = [axes]
for ax, f in zip(axes, files):
    from ezdxf import recover
    doc, _ = recover.readfile(f)
    cfg = Configuration(background_policy=BackgroundPolicy.WHITE, color_policy=ColorPolicy.BLACK)
    Frontend(RenderContext(doc), MatplotlibBackend(ax), config=cfg).draw_layout(doc.modelspace())
    ax.set_title(f.split('/')[-1][:40]); ax.set_aspect('equal')
fig.savefig(sys.argv[1]); print(sys.argv[1])
