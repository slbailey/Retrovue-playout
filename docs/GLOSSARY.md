_Metadata: Status=Canonical; Scope=Terminology; Owner=@runtime-platform_

# Glossary

## Purpose

Record canonical terms used across RetroVue playout documentation so teams share the same vocabulary.

## Terms

**Channel**  
Logical playout worker responsible for decoding and rendering a single broadcast stream. Identified by `channel_id`.

**ChannelManager**  
Python service in RetroVue Core that issues playout plans and orchestrates channels through the gRPC control plane.

**FrameProducer**  
Component that decodes media assets (FFmpeg-backed or stub) and pushes frames into `FrameRingBuffer`.

**FrameRingBuffer**  
Lock-free circular buffer staging decoded frames for Renderer consumption while enforcing depth invariants.

**FrameRenderer**  
Consumer that retrieves frames from the buffer and delivers them to output targets (headless, preview, or downstream pipeline).

**MetricsExporter**  
Telemetry layer responsible for aggregating channel metrics and exposing Prometheus-compatible output.

**MetricsHTTPServer**  
Lightweight HTTP endpoint (`/metrics`) serving telemetry data to Prometheus/Grafana.

**MasterClock**  
Authoritative timing source from RetroVue Core that ensures channels remain synchronized.

**PlayoutControl**  
gRPC service defined in `proto/retrovue/playout.proto` for managing channel lifecycle.

**Playout plan**  
Opaque handle referencing scheduled media assets; supplied by ChannelManager via gRPC requests.

## See also

- `_standards/documentation-standards.md`
- `docs/contracts/PlayoutEngineContract.md`
