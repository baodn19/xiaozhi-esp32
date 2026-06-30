import socket
import wave
import argparse


'''
  Create a UDP socket and bind it to the server's IP:8000.
  Listen for incoming messages and print them to the console.
  Save the audio to a WAV file.
'''
def main(samplerate, channels):
    # Create a UDP socket
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_socket.bind(('0.0.0.0', 8000))

    # Create WAV file with parameters
    filename = f"{samplerate}_{channels}.wav"
    wav_file = wave.open(filename, "wb")
    wav_file.setnchannels(channels)     # channels parameter
    wav_file.setsampwidth(2)            # 2 bytes per sample (16-bit)
    wav_file.setframerate(samplerate)   # samplerate parameter

    print(f"Start saving audio from 0.0.0.0:8000 to {filename}...")

    try:
        while True:
            # Receive a message from the client
            message, address = server_socket.recvfrom(8000)
            
            # Write PCM data to WAV file
            wav_file.writeframes(message)

            # Print length of the message
            print(f"Received {len(message)} bytes from {address}")
    
    except KeyboardInterrupt:
        print("\nStopping recording...")
    
    finally:
        # Close files and socket
        wav_file.close()
        server_socket.close()
        print(f"WAV file '{filename}' saved successfully")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='UDP audio data receiver that saves to WAV file')
    parser.add_argument('--samplerate', '-s', type=int, default=16000, 
                        help='Sample rate (default: 16000)')
    parser.add_argument('--channels', '-c', type=int, default=2, 
                        help='Number of channels (default: 2)')
    
    args = parser.parse_args()
    main(args.samplerate, args.channels)
