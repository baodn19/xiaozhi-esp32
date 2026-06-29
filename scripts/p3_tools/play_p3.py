# Play P3 format audio files
import opuslib
import struct
import numpy as np
import sounddevice as sd
import argparse

def play_p3_file(input_file):
    """
    Play a P3 format audio file.
    P3 format: [1 byte type, 1 byte reserved, 2 byte length, Opus data]
    """
    # Initialize Opus decoder
    sample_rate = 16000  # Sample rate fixed at 16000Hz
    channels = 1  # Mono
    decoder = opuslib.Decoder(sample_rate, channels)
    
    # Frame size (60ms)
    frame_size = int(sample_rate * 60 / 1000)
    
    # Open audio stream
    stream = sd.OutputStream(
        samplerate=sample_rate,
        channels=channels,
        dtype='int16'
    )
    stream.start()
    
    try:
        with open(input_file, 'rb') as f:
            print(f"Playing: {input_file}")
            
            while True:
                # Read header (4 bytes)
                header = f.read(4)
                if not header or len(header) < 4:
                    break
                
                # Parse header
                packet_type, reserved, data_len = struct.unpack('>BBH', header)
                
                # Read Opus data
                opus_data = f.read(data_len)
                if not opus_data or len(opus_data) < data_len:
                    break
                
                # Decode Opus data
                pcm_data = decoder.decode(opus_data, frame_size)
                
                # Convert bytes to numpy array
                audio_array = np.frombuffer(pcm_data, dtype=np.int16)
                
                # Play audio
                stream.write(audio_array)
                
    except KeyboardInterrupt:
        print("\nPlayback stopped")
    finally:
        stream.stop()
        stream.close()
        print("Playback complete")

def main():
    parser = argparse.ArgumentParser(description='Play P3 format audio files')
    parser.add_argument('input_file', help='Input P3 file path')
    args = parser.parse_args()
    
    play_p3_file(args.input_file)

if __name__ == "__main__":
    main() 
