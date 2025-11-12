import argparse
import threading
from datetime import datetime, timezone
from .frame_ring_buffer import FrameRingBuffer
from .video_file_decoder import VideoFileDecoder
from .renderer_stub import RendererStub
from .station_time_mapper import StationTimeMapper
from .master_clock import MasterClock


def main():
    ap = argparse.ArgumentParser(description="RetroVue Air Core Timing Test Harness")
    ap.add_argument("--video", required=True, help="Path to input mp4/mkv file")
    ap.add_argument("--seconds", type=float, default=5.0, help="Duration to run (seconds)")
    ap.add_argument("--fps", type=float, default=30.0, help="Target render FPS")
    ap.add_argument("--capacity", type=int, default=120, help="Ring buffer capacity (frames)")
    ap.add_argument("--verbose", action="store_true", help="Verbose output (show all late frames)")
    ap.add_argument("--no-late-summary", action="store_true", help="Don't show late frame summary")
    args = ap.parse_args()

    # Core components
    clock = MasterClock()
    ring = FrameRingBuffer(capacity_frames=args.capacity)
    stop_event = threading.Event()
    mapper = StationTimeMapper(datetime.now(timezone.utc))

    # Start the decoder in its own thread
    decoder = VideoFileDecoder(args.video, ring, mapper, stop_event)
    decoder.start()

    # Run the renderer in the main thread
    renderer = RendererStub(ring, clock, target_fps=args.fps, verbose=args.verbose)
    renderer.run_for(args.seconds)

    # Signal decoder to stop and wait for it
    stop_event.set()
    decoder.join()

    # Gather metrics
    metrics = renderer.metrics()
    stats = {
        "rendered": metrics.get("rendered", 0),
        "late": metrics.get("late", 0),
        "avg_skew_s": metrics.get("avg_skew_s", 0.0),
        "ring_pushes": getattr(ring, "push_count", 0),
        "ring_drops": getattr(ring, "drop_count", 0),
        "decoded_frames": decoder.decoded,
    }
    
    # Add late frame statistics if available
    if metrics.get("late_frames"):
        stats["late_frames"] = metrics["late_frames"]

    print("\n" + "="*60)
    print("Test Harness Results")
    print("="*60)
    print(stats)
    
    # Print late frame summary (unless disabled)
    if not args.no_late_summary:
        renderer.print_late_frame_summary()


if __name__ == "__main__":
    main()
