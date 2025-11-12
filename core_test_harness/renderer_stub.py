import time
from datetime import datetime, timezone
from collections import namedtuple

# Structure to hold late frame information
LateFrame = namedtuple('LateFrame', ['frame_number', 'pts', 'station_ts', 'render_ts', 'lateness_ms', 'lateness_s'])


class RendererStub:
    def __init__(self, ring, clock, target_fps=30.0, verbose=False):
        self.ring = ring
        self.clock = clock
        self.target_fps = target_fps
        self.verbose = verbose
        self._rendered = 0
        self._late = 0
        self._skew_sum = 0.0
        self._late_frames = []  # List of LateFrame tuples
        self.frame_threshold_s = 1.0 / target_fps

    def run_for(self, seconds):
        start_wall = time.time()
        end_wall = start_wall + seconds

        while time.time() < end_wall:
            frame = self.ring.pop(timeout=0.1)
            if frame is None:
                continue

            now = datetime.now(timezone.utc)
            station_ts = frame["station_ts"]
            
            # Extract frame metadata if available
            pts = frame.get("pts", None)
            frame_number = frame.get("frame_number", self._rendered + 1)

            # Wait until it's time to show this frame
            delay = (station_ts - now).total_seconds()
            if delay > 0:
                time.sleep(delay)

            # After sleeping, measure skew
            render_ts = datetime.now(timezone.utc)
            skew = (render_ts - station_ts).total_seconds()
            skew_ms = skew * 1000.0

            self._rendered += 1
            self._skew_sum += skew
            
            # Check if frame is late
            if skew > self.frame_threshold_s:
                self._late += 1
                late_frame = LateFrame(
                    frame_number=frame_number,
                    pts=pts,
                    station_ts=station_ts,
                    render_ts=render_ts,
                    lateness_ms=skew_ms,
                    lateness_s=skew
                )
                self._late_frames.append(late_frame)
                
                # Log late frame details
                self._log_late_frame(late_frame)

        print(f"[RendererStub] Rendered {self._rendered} frames")

    def _log_late_frame(self, late_frame):
        """Log details about a late frame."""
        lateness_ms = late_frame.lateness_ms
        frame_info = f"Frame #{late_frame.frame_number}"
        if late_frame.pts is not None:
            frame_info += f" (PTS={late_frame.pts})"
        
        # Determine severity
        if lateness_ms < 50:
            severity = "INFO"
        elif lateness_ms < 500:
            severity = "WARNING"
        elif lateness_ms < 5000:
            severity = "ERROR"
        else:
            severity = "CRITICAL"
        
        # Format timestamps
        station_str = late_frame.station_ts.strftime("%H:%M:%S.%f")[:-3]
        render_str = late_frame.render_ts.strftime("%H:%M:%S.%f")[:-3]
        
        message = (
            f"[RendererStub] [{severity}] {frame_info} was LATE:\n"
            f"  Scheduled: {station_str}\n"
            f"  Rendered:  {render_str}\n"
            f"  Lateness:  {lateness_ms:.2f} ms ({late_frame.lateness_s:.6f} s)"
        )
        
        if self.verbose:
            print(message)
        elif severity in ("ERROR", "CRITICAL"):
            print(message)

    def metrics(self):
        avg_skew = self._skew_sum / max(1, self._rendered)
        
        # Calculate late frame statistics
        late_stats = {}
        if self._late_frames:
            lateness_ms_list = [f.lateness_ms for f in self._late_frames]
            late_stats = {
                "max_lateness_ms": max(lateness_ms_list),
                "min_lateness_ms": min(lateness_ms_list),
                "avg_lateness_ms": sum(lateness_ms_list) / len(lateness_ms_list),
                "late_frame_count": len(self._late_frames),
            }
        
        return {
            "rendered": self._rendered,
            "late": self._late,
            "avg_skew_s": avg_skew,
            "late_frames": late_stats,
        }
    
    def get_late_frames(self):
        """Get list of all late frames with details."""
        return self._late_frames.copy()
    
    def print_late_frame_summary(self):
        """Print a summary of late frames."""
        if not self._late_frames:
            print("[RendererStub] No late frames detected.")
            return
        
        print(f"\n[RendererStub] Late Frame Summary:")
        print(f"  Total late frames: {len(self._late_frames)}")
        print(f"  Late frame percentage: {100.0 * len(self._late_frames) / max(1, self._rendered):.2f}%")
        
        if self._late_frames:
            lateness_ms_list = [f.lateness_ms for f in self._late_frames]
            print(f"  Max lateness: {max(lateness_ms_list):.2f} ms")
            print(f"  Min lateness: {min(lateness_ms_list):.2f} ms")
            print(f"  Avg lateness: {sum(lateness_ms_list) / len(lateness_ms_list):.2f} ms")
            
            # Group by severity
            critical = [f for f in self._late_frames if f.lateness_ms >= 5000]
            error = [f for f in self._late_frames if 500 <= f.lateness_ms < 5000]
            warning = [f for f in self._late_frames if 50 <= f.lateness_ms < 500]
            info = [f for f in self._late_frames if f.lateness_ms < 50]
            
            if critical:
                print(f"  CRITICAL (>5s): {len(critical)} frames")
            if error:
                print(f"  ERROR (500ms-5s): {len(error)} frames")
            if warning:
                print(f"  WARNING (50-500ms): {len(warning)} frames")
            if info:
                print(f"  INFO (<50ms): {len(info)} frames")
            
            # Show worst offenders
            worst = sorted(self._late_frames, key=lambda f: f.lateness_ms, reverse=True)[:5]
            print(f"\n  Top 5 worst late frames:")
            for i, frame in enumerate(worst, 1):
                frame_info = f"Frame #{frame.frame_number}"
                if frame.pts is not None:
                    frame_info += f" (PTS={frame.pts})"
                print(f"    {i}. {frame_info}: {frame.lateness_ms:.2f} ms late")
