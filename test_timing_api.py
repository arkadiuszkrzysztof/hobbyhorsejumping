#!/usr/bin/env python3
"""
Simple command-line testing script for the Hobby Horse Jumping timing system.
This script allows you to test the API endpoints directly without the web interface.
"""

import requests
import json
import time
import sys
from datetime import datetime

# Configuration
API_BASE_URL = "http://127.0.0.1:3003"

def get_current_timestamp():
    """Get current timestamp in milliseconds"""
    return int(time.time() * 1000)

def test_api_connection():
    """Test basic API connectivity"""
    print("🔍 Testing API connection...")
    try:
        response = requests.get(f"{API_BASE_URL}/api/competitions/")
        if response.status_code == 200:
            competitions = response.json()
            print(f"✅ API connected! Found {len(competitions)} competitions")
            return True
        else:
            print(f"❌ API responded with status {response.status_code}")
            return False
    except requests.exceptions.ConnectionError:
        print("❌ Could not connect to API. Make sure the Django server is running.")
        return False
    except Exception as e:
        print(f"❌ Unexpected error: {e}")
        return False

def get_active_competitor():
    """Get the currently active competitor"""
    try:
        response = requests.get(f"{API_BASE_URL}/api/active-competitor/")
        if response.status_code == 200:
            data = response.json()
            competitor = data.get('active_competitor')
            if competitor:
                print(f"✅ Active competitor: #{competitor['starting_order']} {competitor['participant']['name']}")
                return competitor
            else:
                print("⚠️ No active competitor set")
                return None
        else:
            print(f"❌ Failed to get active competitor: {response.status_code}")
            return None
    except Exception as e:
        print(f"❌ Error getting active competitor: {e}")
        return None

def send_timing_signal(time_mark, sensor_time=None):
    """Send a timing signal to the API"""
    if sensor_time is None:
        sensor_time = get_current_timestamp()
    
    payload = {
        "time_mark": time_mark,
        "sensor_time": str(sensor_time)
    }
    
    print(f"📡 Sending {time_mark} signal (timestamp: {sensor_time})...")
    
    try:
        response = requests.post(
            f"{API_BASE_URL}/timereadings/",
            headers={"Content-Type": "application/json"},
            json=payload
        )
        
        if response.status_code in [200, 201]:
            print(f"✅ {time_mark} signal sent successfully!")
            try:
                result = response.json()
                print(f"   Server response: {json.dumps(result, indent=2)}")
            except:
                print(f"   Server response: {response.text}")
        else:
            print(f"❌ Failed to send {time_mark} signal: {response.status_code}")
            print(f"   Response: {response.text}")
            
        return response.status_code in [200, 201]
    except Exception as e:
        print(f"❌ Error sending {time_mark} signal: {e}")
        return False

def simulate_complete_run():
    """Simulate a complete timing run"""
    print("🏁 Simulating complete timing run...")
    
    # Send START signal
    if not send_timing_signal("START"):
        print("❌ Failed to send START signal, aborting run")
        return False
    
    print("⏳ Waiting 3 seconds...")
    time.sleep(3)
    
    # Send FINISH signal
    if not send_timing_signal("FINISH"):
        print("❌ Failed to send FINISH signal")
        return False
    
    print("✅ Complete run simulation finished!")
    return True

def interactive_mode():
    """Interactive testing mode"""
    print("\n🧪 Interactive Testing Mode")
    print("Available commands:")
    print("  1 - Send START signal")
    print("  2 - Send FINISH signal")
    print("  3 - Simulate complete run")
    print("  4 - Check active competitor")
    print("  5 - Test API connection")
    print("  q - Quit")
    
    while True:
        choice = input("\nEnter command: ").strip().lower()
        
        if choice == 'q':
            print("👋 Goodbye!")
            break
        elif choice == '1':
            send_timing_signal("START")
        elif choice == '2':
            send_timing_signal("FINISH")
        elif choice == '3':
            simulate_complete_run()
        elif choice == '4':
            get_active_competitor()
        elif choice == '5':
            test_api_connection()
        else:
            print("❌ Invalid command. Please try again.")

def main():
    """Main function"""
    print("🐎 Hobby Horse Jumping - API Testing Tool")
    print("=========================================")
    
    # Test API connection first
    if not test_api_connection():
        sys.exit(1)
    
    # Check for active competitor
    get_active_competitor()
    
    # Check command line arguments
    if len(sys.argv) > 1:
        command = sys.argv[1].upper()
        if command == "START":
            send_timing_signal("START")
        elif command == "FINISH":
            send_timing_signal("FINISH")
        elif command == "RUN":
            simulate_complete_run()
        else:
            print(f"❌ Unknown command: {command}")
            print("Available commands: START, FINISH, RUN")
    else:
        # Enter interactive mode
        interactive_mode()

if __name__ == "__main__":
    main()
