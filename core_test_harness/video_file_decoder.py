import threading
import cv2
from datetime import datetime, timezone, timedelta


class VideoFileDecoder(threading.Thread):
    def __init__(self, path, ring, mapper, stop_event):
        super().__init__(daemon=True)
        self.path = path
        self.ring = ring
        self.mapper = mapper
        self.stop_event = stop_event
        self.decoded = 0

    def run(self):
        cap = cv2.VideoCapture(self.path)
        if not cap.isOpened():
            print(f"[VideoFileDecoder] Failed to open {self.path}")
            return

        print(f"[VideoFileDecoder] Started decoding {self.path}")
        fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
        frame_period_s = 1.0 / fps
        next_station_ts = datetime.now(timezone.utc)

        while not self.stop_event.is_set():
            ret, frame = cap.read()
            if not ret:
                print("[VideoFileDecoder] End of stream or read failed.")
                break

            self.decoded += 1

            # Create proper frame structure for renderer
            packet = {
                "img": frame,
                "station_ts": next_station_ts,
                "frame_number": self.decoded,
                "pts": cap.get(cv2.CAP_PROP_POS_FRAMES) - 1,  # 0-indexed frame number
            }

            self.ring.push(packet)

            # Increment virtual station time for next frame
            next_station_ts = next_station_ts + (
                self.mapper.timedelta_for_duration(frame_period_s)
                if hasattr(self.mapper, "timedelta_for_duration")
                else timedelta(seconds=frame_period_s)
            )

        cap.release()
        print(f"[VideoFileDecoder] Finished decoding. Total frames: {self.decoded}")
