#!/usr/bin/env python3
"""
Vosk Speech Recognition Worker
Receives audio via stdin, outputs transcripts to stdout
"""

import sys
import json
import struct
from vosk import Model, KaldiRecognizer

def main():
    if len(sys.argv) < 2:
        print(json.dumps({"error": "Usage: vosk_worker.py <model_path> [sample_rate]"}), flush=True)
        sys.exit(1)

    model_path = sys.argv[1]
    # Accept sample rate as optional argument, default to 48000 (common for Windows)
    sample_rate = int(sys.argv[2]) if len(sys.argv) > 2 else 48000

    try:
        # Load model
        model = Model(model_path)
        recognizer = KaldiRecognizer(model, sample_rate)
        recognizer.SetWords(True)

        # Signal ready with sample rate info
        print(json.dumps({"status": "ready", "sample_rate": sample_rate}), flush=True)

        # Read audio chunks from stdin
        while True:
            # Read chunk size (4 bytes, little-endian)
            size_bytes = sys.stdin.buffer.read(4)
            if not size_bytes or len(size_bytes) < 4:
                break

            chunk_size = struct.unpack('<I', size_bytes)[0]
            if chunk_size == 0:
                break

            # Read audio data (int16 PCM)
            audio_data = sys.stdin.buffer.read(chunk_size)
            if len(audio_data) < chunk_size:
                break

            # Process audio
            if recognizer.AcceptWaveform(audio_data):
                # Final result
                result = json.loads(recognizer.Result())
                if result.get("text"):
                    output = {
                        "type": "final",
                        "text": result["text"]
                    }
                    print(json.dumps(output), flush=True)
            else:
                # Partial result
                partial = json.loads(recognizer.PartialResult())
                if partial.get("partial"):
                    output = {
                        "type": "partial",
                        "text": partial["partial"]
                    }
                    print(json.dumps(output), flush=True)

    except Exception as e:
        print(json.dumps({"error": str(e)}), flush=True)
        sys.exit(1)

if __name__ == "__main__":
    main()
