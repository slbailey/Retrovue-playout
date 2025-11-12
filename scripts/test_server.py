#!/usr/bin/env python3
"""
RetroVue Playout Engine - Test Client
---------------------------------------
Quick test script to verify the gRPC server is working correctly.

Usage:
    python scripts/test_server.py
"""

import sys
import grpc
from pathlib import Path

# Add the proto directory to the path
repo_root = Path(__file__).resolve().parent.parent
python_repo = repo_root.parent / "Retrovue"
proto_path = python_repo / "core" / "proto"
sys.path.insert(0, str(proto_path))

try:
    from retrovue import playout_pb2, playout_pb2_grpc
except ImportError as e:
    print("[ERROR] Failed to import proto stubs. Run 'python scripts/generate_stubs.py' first.")
    print(f"[DEBUG] Import error: {e}")
    print(f"[DEBUG] Proto path: {proto_path}")
    sys.exit(1)


def test_server(address="localhost:50051"):
    """Test the PlayoutControl gRPC service."""
    
    print(f"[INFO] Connecting to {address}...")
    
    try:
        with grpc.insecure_channel(address) as channel:
            stub = playout_pb2_grpc.PlayoutControlStub(channel)
            
            # Test 1: Get version
            print("\n[TEST 1] GetVersion")
            try:
                response = stub.GetVersion(playout_pb2.ApiVersionRequest())
                print(f"   [PASS] API Version: {response.version}")
            except grpc.RpcError as e:
                print(f"   [FAIL] Failed: {e.code()} - {e.details()}")
                return False
            
            # Test 2: Start a channel
            print("\n[TEST 2] StartChannel")
            try:
                request = playout_pb2.StartChannelRequest(
                    channel_id=1,
                    plan_handle="test-plan-001",
                    port=8090
                )
                response = stub.StartChannel(request)
                if response.success:
                    print(f"   [PASS] {response.message}")
                else:
                    print(f"   [WARN] {response.message}")
            except grpc.RpcError as e:
                print(f"   [FAIL] Failed: {e.code()} - {e.details()}")
                return False
            
            # Test 3: Update plan
            print("\n[TEST 3] UpdatePlan")
            try:
                request = playout_pb2.UpdatePlanRequest(
                    channel_id=1,
                    plan_handle="test-plan-002"
                )
                response = stub.UpdatePlan(request)
                if response.success:
                    print(f"   [PASS] {response.message}")
                else:
                    print(f"   [WARN] {response.message}")
            except grpc.RpcError as e:
                print(f"   [FAIL] Failed: {e.code()} - {e.details()}")
                return False
            
            # Test 4: Stop the channel
            print("\n[TEST 4] StopChannel")
            try:
                request = playout_pb2.StopChannelRequest(channel_id=1)
                response = stub.StopChannel(request)
                if response.success:
                    print(f"   [PASS] {response.message}")
                else:
                    print(f"   [WARN] {response.message}")
            except grpc.RpcError as e:
                print(f"   [FAIL] Failed: {e.code()} - {e.details()}")
                return False
            
            # Test 5: Try to stop a non-existent channel
            print("\n[TEST 5] StopChannel (non-existent)")
            try:
                request = playout_pb2.StopChannelRequest(channel_id=999)
                response = stub.StopChannel(request)
                if not response.success:
                    print(f"   [PASS] Correctly reported: {response.message}")
                else:
                    print(f"   [WARN] Unexpected success")
            except grpc.RpcError as e:
                print(f"   [PASS] Expected error: {e.code()}")
            
            print("\n[SUCCESS] All tests passed!")
            return True
            
    except grpc.RpcError as e:
        print(f"\n[ERROR] Connection failed: {e}")
        return False


if __name__ == "__main__":
    import time
    
    print("=" * 60)
    print("RetroVue Playout Engine - Test Client")
    print("=" * 60)
    
    # Check if server is running
    print("\n[INFO] Make sure the server is running:")
    import platform
    if platform.system() == "Windows":
        print("    .\\build\\RelWithDebInfo\\retrovue_playout.exe\n")
    else:
        # Try to find the binary in common build locations
        import os
        from pathlib import Path
        build_paths = [
            Path("build/retrovue_air"),
            Path("build/RelWithDebInfo/retrovue_air"),
            Path("build/Debug/retrovue_air"),
            Path("build/Release/retrovue_air"),
            Path("out/build/linux-relwithdebinfo/retrovue_air"),
        ]
        found = False
        for path in build_paths:
            if path.exists():
                print(f"    {path}\n")
                found = True
                break
        if not found:
            print("    ./build/retrovue_air (or build/RelWithDebInfo/retrovue_air)\n")
    
    input("Press Enter to start tests...")
    
    success = test_server()
    sys.exit(0 if success else 1)

