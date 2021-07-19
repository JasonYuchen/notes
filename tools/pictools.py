import imageio
import os
import gc
from moviepy.editor import *


def pics2gif(out_name = None, duration = 1):
    if not out_name:
        out_name = "out/1.gif"
    frames = []
    for frame in os.listdir("src"):
        frames.append(imageio.imread("src/{}".format(frame)))
    imageio.mimsave(out_name, frames, 'GIF', duration=duration)
    gc.collect()


def mp42gif(out_path = None):
    if not out_path:
        out_path = "out"
    for video in os.listdir("src"):
        clip = VideoFileClip("src/{}".format(video)).resize(0.4)\
            .write_gif("{}/{}".format(out_path, video.replace(video.split('.')[-1], "gif")), fps=1)
    gc.collect()
